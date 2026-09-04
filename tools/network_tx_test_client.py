#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Bounded offline/live client for validating the flex1500d HTTP TX API."""

from __future__ import annotations

import argparse
import json
import math
import socket
import struct
import threading
import time
from dataclasses import dataclass


SAMPLE_RATE = 48_000
FREQUENCY_HZ = 28_475_000
TONE_HZ = 700
DRIVE_PERCENT = 50
DURATION_SECONDS = 3.0
CHUNK_FRAMES = 480
ARMING_TEXT = "I_UNDERSTAND_THIS_WILL_TRANSMIT_3_SECONDS_INTO_A_DUMMY_LOAD"
CALIBRATION_DRIVES = (25, 50, 75, 100)


@dataclass(frozen=True)
class Profile:
    mode: str
    source: str
    sample_format: str
    frame_bytes: int


PROFILES = {
    "audio": Profile("usb", "audio", "s16le", 2),
    "iq": Profile("iq", "iq", "cs16le", 4),
}


def make_chunk(kind: str, first_frame: int, amplitude: float = 0.8) -> bytes:
    output = bytearray()
    for offset in range(CHUNK_FRAMES):
        phase = 2.0 * math.pi * TONE_HZ * (first_frame + offset) / SAMPLE_RATE
        if kind == "audio":
            output += struct.pack("<h", round(amplitude * 32767 * math.sin(phase)))
        else:
            # Native flex1500d I/Q convention. A future Soapy adapter will
            # perform its boundary-orientation conversion before upload.
            output += struct.pack(
                "<hh",
                round(amplitude * 32767 * math.cos(phase)),
                round(amplitude * 32767 * math.sin(phase)),
            )
    return bytes(output)


def validate_signal(kind: str, amplitude: float = 0.8) -> None:
    profile = PROFILES[kind]
    chunk = make_chunk(kind, 0, amplitude)
    assert len(chunk) == CHUNK_FRAMES * profile.frame_bytes
    values = struct.unpack("<" + "h" * (len(chunk) // 2), chunk)
    peak = max(abs(value) for value in values) / 32768.0
    rms = math.sqrt(sum(value * value for value in values) / len(values)) / 32768.0
    if not (amplitude - 0.01 <= peak <= amplitude + 0.01 and
            amplitude / math.sqrt(2.0) - 0.01 <= rms <= amplitude + 0.01):
        raise RuntimeError(f"unexpected generated signal level: peak={peak}, rms={rms}")
    print(
        f"offline {kind}: {TONE_HZ} Hz, {SAMPLE_RATE} sample/s, "
        f"peak={peak:.3f}, rms={rms:.3f}, chunk={len(chunk)} bytes"
    )


def read_headers(connection: socket.socket) -> tuple[int, bytes]:
    data = bytearray()
    while b"\r\n\r\n" not in data:
        block = connection.recv(4096)
        if not block:
            raise RuntimeError("daemon disconnected before completing response")
        data += block
        if len(data) > 16384:
            raise RuntimeError("oversized HTTP response header")
    header, remainder = bytes(data).split(b"\r\n\r\n", 1)
    status = int(header.split(b" ", 2)[1])
    content_length = 0
    for line in header.split(b"\r\n")[1:]:
        name, separator, value = line.partition(b":")
        if separator and name.lower() == b"content-length":
            content_length = int(value.strip())
    while len(remainder) < content_length:
        block = connection.recv(content_length - len(remainder))
        if not block:
            raise RuntimeError("daemon disconnected during response body")
        remainder += block
    return status, remainder[:content_length]


def request(host: str, port: int, method: str, path: str,
            lease: int | None = None, body: dict | None = None) -> tuple[int, dict]:
    payload = b"" if body is None else json.dumps(body, separators=(",", ":")).encode()
    headers = [f"{method} {path} HTTP/1.1", f"Host: {host}:{port}", "Connection: close"]
    if lease is not None:
        headers.append(f"X-Flex1500-TX-Lease: {lease}")
    if body is not None:
        headers.extend(("Content-Type: application/json", f"Content-Length: {len(payload)}"))
    wire = ("\r\n".join(headers) + "\r\n\r\n").encode() + payload
    with socket.create_connection((host, port), timeout=3.0) as connection:
        connection.sendall(wire)
        status, response = read_headers(connection)
    parsed = json.loads(response) if response else {}
    return status, parsed


def open_stream(host: str, port: int, lease: int) -> socket.socket:
    connection = socket.create_connection((host, port), timeout=3.0)
    headers = (
        f"CONNECT /v1/tx/stream HTTP/1.1\r\nHost: {host}:{port}\r\n"
        f"X-Flex1500-TX-Lease: {lease}\r\n\r\n"
    ).encode()
    connection.sendall(headers)
    status, _ = read_headers(connection)
    if status != 200:
        connection.close()
        raise RuntimeError(f"sample tunnel returned HTTP {status}")
    connection.settimeout(2.0)
    return connection


def require(status: int, expected: int, body: dict, operation: str) -> None:
    if status != expected:
        raise RuntimeError(f"{operation} returned HTTP {status}: {body}")


def live_test(host: str, port: int, kind: str, drive_percent: int,
              amplitude: float) -> None:
    profile = PROFILES[kind]
    lease: int | None = None
    stream: socket.socket | None = None
    sender_stop = threading.Event()
    sender_error: list[BaseException] = []
    keyed = False

    try:
        status, body = request(host, port, "PUT", f"/v1/radio/frequency/{FREQUENCY_HZ}")
        require(status, 200, body, "frequency selection")
        status, body = request(host, port, "POST", "/v1/tx/sessions", body={
            "mode": profile.mode,
            "drive_percent": drive_percent,
            "source": profile.source,
            "sample_format": profile.sample_format,
            "sample_rate": SAMPLE_RATE,
            "channels": 1,
        })
        require(status, 201, body, "session acquisition")
        lease = int(body["lease"])
        stream = open_stream(host, port, lease)

        def send_samples() -> None:
            frame = 0
            deadline = time.monotonic()
            try:
                while not sender_stop.is_set():
                    stream.sendall(make_chunk(kind, frame, amplitude))
                    frame += CHUNK_FRAMES
                    deadline += CHUNK_FRAMES / SAMPLE_RATE
                    sender_stop.wait(max(0.0, deadline - time.monotonic()))
            except BaseException as error:  # reported by the controlling thread
                if not sender_stop.is_set():
                    sender_error.append(error)

        sender = threading.Thread(target=send_samples, name="tx-sample-sender")
        sender.start()
        # Raw I/Q has twice the bytes per frame of PCM. Allow extra daemon-side
        # socket-drain time beyond the common 500 ms frame requirement.
        time.sleep(1.50 if kind == "iq" else 0.60)
        status, body = request(host, port, "PUT", "/v1/tx/ptt/start", lease)
        require(status, 200, body, "PTT start")
        keyed = True
        print(f"LIVE {kind} TX keyed: {FREQUENCY_HZ} Hz, {drive_percent}% drive")
        end = time.monotonic() + DURATION_SECONDS
        while time.monotonic() < end:
            if sender_error:
                raise RuntimeError(f"sample sender failed: {sender_error[0]}")
            time.sleep(0.05)
        status, body = request(host, port, "PUT", "/v1/tx/ptt/stop", lease)
        require(status, 200, body, "PTT stop")
        keyed = False
        print(f"LIVE {kind} TX stopped normally")
        sender_stop.set()
        sender.join(timeout=2.0)
        if sender.is_alive():
            raise RuntimeError("sample sender did not stop")
        stream.shutdown(socket.SHUT_RDWR)
        stream.close()
        stream = None
        status, body = request(
            host, port, "DELETE", "/v1/tx/sessions/current", lease)
        require(status, 200, body, "session release")
        lease = None
        radio_status, radio = request(host, port, "GET", "/v1/radio")
        require(radio_status, 200, radio, "post-test radio status")
        service_status, service = request(host, port, "GET", "/v1/status")
        require(service_status, 200, service, "post-test service status")
        print(
            "post-test: "
            f"owner={radio.get('tx_owner')} state={radio.get('tx_state')} "
            f"reserved={radio.get('network_tx_reserved')} "
            f"starts={service.get('tx_starts')} stops={service.get('tx_stops')} "
            f"underruns={service.get('tx_underruns')} "
            f"dropped={service.get('tx_dropped_microphone_frames')} "
            f"USB_errors={service.get('usb_error_events')} "
            f"command_errors={service.get('radio_command_errors')} "
            f"cleanup_failures={service.get('tx_cleanup_failures')}"
        )
    finally:
        if lease is not None and keyed:
            try:
                request(host, port, "PUT", "/v1/tx/ptt/stop", lease)
            except OSError:
                pass
        sender_stop.set()
        if 'sender' in locals():
            sender.join(timeout=2.0)
        if stream is not None:
            try:
                stream.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            stream.close()
        if lease is not None:
            try:
                request(host, port, "DELETE", "/v1/tx/sessions/current", lease)
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=sorted(PROFILES))
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=15000)
    parser.add_argument("--execute-live", metavar="ARMING_TEXT")
    parser.add_argument("--calibration-drive", type=int,
                        choices=CALIBRATION_DRIVES)
    arguments = parser.parse_args()
    calibration = arguments.calibration_drive is not None
    if calibration and arguments.kind != "iq":
        parser.error("--calibration-drive requires the iq profile")
    drive_percent = arguments.calibration_drive if calibration else DRIVE_PERCENT
    amplitude = 1.0 if calibration else 0.8
    validate_signal(arguments.kind, amplitude)
    if arguments.execute_live is None:
        print("offline validation only; no socket or radio operation performed")
        return 0
    arming_text = (f"I_UNDERSTAND_THIS_WILL_TRANSMIT_3_SECONDS_AT_"
                   f"{drive_percent}_PERCENT_INTO_A_DUMMY_LOAD"
                   if calibration else ARMING_TEXT)
    if arguments.execute_live != arming_text:
        parser.error(f"live execution requires exact arming text: {arming_text}")
    live_test(arguments.host, arguments.port, arguments.kind,
              drive_percent, amplitude)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
