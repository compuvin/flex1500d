#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Summarize a USBPcap capture and decode FLEX-1500 command packets."""

from __future__ import annotations

import argparse
from collections import Counter
from dataclasses import dataclass
from pathlib import Path
import shutil
import struct
import subprocess
import sys


FLEX_VENDOR_ID = 0x2192
FLEX_PRODUCT_ID = 0x1502
COMMAND_ENDPOINT = 0x04
STATUS_ENDPOINT = 0x83
OPCODE_NAMES = {
    1020: "I2C_WRITE_2_VALUE",
    1200: "GET_FIRMWARE_REV",
    1202: "GET_TRX_REV",
    1203: "GET_TRX_SN",
    1205: "GET_PA_REV",
    1206: "GET_PA_SN",
    1219: "INITIALIZE",
    1220: "GET_SERIAL_NUM",
    1247: "SET_TRX_PREAMP",
    1256: "SET_XREF",
    1257: "SET_RX1_FILTER",
    1260: "SET_PA_FILTER",
    1276: "SET_TR",
    1278: "SET_RX1_ANT",
    1279: "SET_TX_ANT",
    1298: "SET_AMP_TX1",
    1347: "SET_RX1_FREQ_TW",
    1350: "GET_REGION",
    1355: "GET_STATUS",
    1382: "READ_EEPROM",
    1383: "WRITE_EEPROM",
}


@dataclass(frozen=True)
class UsbRow:
    frame: str
    time: str
    device: str
    endpoint: str
    transfer_type: str
    data_length: str
    payload: bytes


def parse_payload(text: str) -> bytes:
    compact = text.replace(":", "").replace(" ", "")
    if not compact:
        return b""
    try:
        return bytes.fromhex(compact)
    except ValueError:
        return b""


def parse_endpoint(text: str) -> int | None:
    if not text:
        return None
    first = text.split(",", 1)[0]
    try:
        return int(first, 0)
    except ValueError:
        try:
            return int(first, 16)
        except ValueError:
            return None


def decode_command(payload: bytes) -> str | None:
    if len(payload) != 20:
        return None
    index = payload[0]
    opcode = int.from_bytes(payload[4:8], "big")
    param1 = int.from_bytes(payload[8:12], "big")
    param2 = int.from_bytes(payload[12:16], "big")
    name = OPCODE_NAMES.get(opcode, "UNKNOWN")
    decoded = (
        f"index={index} opcode={opcode} (0x{opcode:04x}, {name}) "
        f"param1=0x{param1:08x} param2=0x{param2:08x}"
    )
    if opcode == 1347:
        frequency_hz = param1 * 384_000_000 / (0xFFFFFFFF * 2)
        decoded += f" hardware_center={frequency_hz:.3f} Hz"
    elif opcode == 1020:
        decoded += (
            f" i2c_addr=0x{param1 & 0xff:02x}"
            f" register=0x{(param2 >> 8) & 0xff:02x}"
            f" value=0x{param2 & 0xff:02x}"
        )
    return decoded


def decode_response(payload: bytes) -> str | None:
    if len(payload) < 4:
        return None
    message_type = payload[1]
    if message_type not in (1, 2):
        return None
    index = payload[2]
    if message_type == 2:
        return (
            f"status=0x{payload[0]:02x} type=2 index={index} "
            f"eeprom_data={payload[4:].hex()}"
        )
    if len(payload) < 8:
        return None
    result = int.from_bytes(payload[4:8], "big")
    return (
        f"status=0x{payload[0]:02x} type={message_type} index={index} "
        f"result=0x{result:08x}"
    )


def decode_eeprom_request(payload: bytes) -> str | None:
    if len(payload) < 8:
        return None
    opcode = int.from_bytes(payload[4:8], "big")
    if opcode not in (1382, 1383):
        return None
    index = payload[0]
    offset = int.from_bytes(payload[1:3], "big")
    byte_count = payload[3]
    return (
        f"index={index} opcode={opcode} ({OPCODE_NAMES[opcode]}) "
        f"offset=0x{offset:04x} bytes={byte_count}"
    )


def tshark_rows(capture: Path, display_filter: str | None) -> list[UsbRow]:
    tshark = shutil.which("tshark")
    if tshark is None:
        raise RuntimeError(
            "tshark is not installed; on Ubuntu install the 'tshark' package"
        )

    command = [
        tshark,
        "-n",
        "-r",
        str(capture),
        "-T",
        "fields",
        "-E",
        "occurrence=f",
        "-e",
        "frame.number",
        "-e",
        "frame.time_relative",
        "-e",
        "usb.device_address",
        "-e",
        "usb.endpoint_address",
        "-e",
        "usb.transfer_type",
        "-e",
        "usb.data_len",
        "-e",
        "usb.capdata",
    ]
    if display_filter:
        command.extend(["-Y", display_filter])

    completed = subprocess.run(
        command, check=False, capture_output=True, text=True, encoding="utf-8"
    )
    if completed.returncode != 0:
        raise RuntimeError(completed.stderr.strip() or "tshark failed")

    rows: list[UsbRow] = []
    for line in completed.stdout.splitlines():
        columns = line.split("\t")
        columns.extend([""] * (7 - len(columns)))
        rows.append(
            UsbRow(
                frame=columns[0],
                time=columns[1],
                device=columns[2],
                endpoint=columns[3],
                transfer_type=columns[4],
                data_length=columns[5],
                payload=parse_payload(columns[6]),
            )
        )
    return rows


def pcapng_usbpcap_rows(capture: Path) -> list[UsbRow]:
    """Read little-endian pcapng Enhanced Packet Blocks with USBPcap headers."""
    raw = capture.read_bytes()
    offset = 0
    interfaces: list[tuple[int, float]] = []
    rows: list[UsbRow] = []
    first_timestamp: float | None = None
    frame_number = 0
    endian = "<"

    while offset + 12 <= len(raw):
        block_type_le, block_length_le = struct.unpack_from("<II", raw, offset)
        if block_type_le == 0x0A0D0D0A:
            byte_order = raw[offset + 8 : offset + 12]
            endian = "<" if byte_order == b"\x4d\x3c\x2b\x1a" else ">"
        block_type, block_length = struct.unpack_from(endian + "II", raw, offset)
        if block_length < 12 or offset + block_length > len(raw):
            raise RuntimeError(f"invalid pcapng block at byte {offset}")

        if block_type == 1 and block_length >= 20:  # Interface Description
            link_type = struct.unpack_from(endian + "H", raw, offset + 8)[0]
            ts_resolution = 1e-6
            option_offset = offset + 16
            option_end = offset + block_length - 4
            while option_offset + 4 <= option_end:
                code, length = struct.unpack_from(
                    endian + "HH", raw, option_offset
                )
                option_offset += 4
                value = raw[option_offset : option_offset + length]
                option_offset += (length + 3) & ~3
                if code == 0:
                    break
                if code == 9 and value:
                    exponent = value[0]
                    ts_resolution = (
                        2.0 ** -(exponent & 0x7F)
                        if exponent & 0x80
                        else 10.0 ** -exponent
                    )
            interfaces.append((link_type, ts_resolution))

        elif block_type == 6 and block_length >= 32:  # Enhanced Packet
            interface_id, ts_high, ts_low, captured_length = struct.unpack_from(
                endian + "IIII", raw, offset + 8
            )
            frame_number += 1
            if interface_id >= len(interfaces) or interfaces[interface_id][0] != 249:
                offset += block_length
                continue
            packet = raw[offset + 28 : offset + 28 + captured_length]
            if len(packet) < 27:
                offset += block_length
                continue
            header_length = struct.unpack_from("<H", packet, 0)[0]
            if header_length < 27 or header_length > len(packet):
                offset += block_length
                continue
            bus = struct.unpack_from("<H", packet, 17)[0]
            device = struct.unpack_from("<H", packet, 19)[0]
            endpoint = packet[21]
            transfer_type = packet[22]
            data_length = struct.unpack_from("<I", packet, 23)[0]
            payload = packet[header_length : header_length + data_length]
            timestamp_ticks = (ts_high << 32) | ts_low
            timestamp = timestamp_ticks * interfaces[interface_id][1]
            if first_timestamp is None:
                first_timestamp = timestamp
            rows.append(
                UsbRow(
                    frame=str(frame_number),
                    time=f"{timestamp - first_timestamp:.6f}",
                    device=str(device),
                    endpoint=f"0x{endpoint:02x}",
                    transfer_type=str(transfer_type),
                    data_length=str(data_length),
                    payload=payload,
                )
            )
        offset += block_length
    return rows


def capture_rows(capture: Path, display_filter: str | None = None) -> list[UsbRow]:
    if shutil.which("tshark") is not None:
        return tshark_rows(capture, display_filter)
    rows = pcapng_usbpcap_rows(capture)
    if display_filter and "usb.device_address ==" in display_filter:
        device = display_filter.rsplit("==", 1)[1].strip()
        rows = [row for row in rows if row.device == device]
    return rows


def list_devices(capture: Path) -> int:
    filter_text = (
        f"usb.idVendor == 0x{FLEX_VENDOR_ID:04x} && "
        f"usb.idProduct == 0x{FLEX_PRODUCT_ID:04x}"
    )
    if shutil.which("tshark") is not None:
        rows = tshark_rows(capture, filter_text)
        devices = Counter(row.device for row in rows if row.device)
    else:
        rows = capture_rows(capture)
        descriptor_devices = {
            row.device
            for row in rows
            if b"\x92\x21\x02\x15" in row.payload
        }
        devices = Counter(
            row.device for row in rows if row.device in descriptor_devices
        )
    if not devices:
        print("No 2192:1502 descriptor frames were decoded.")
        print("Open the capture in Wireshark and inspect usb.device_address.")
        return 1
    print("Candidate FLEX-1500 USB device addresses:")
    for device, count in sorted(devices.items()):
        print(f"  {device}: {count} matching descriptor frame(s)")
    return 0


def summarize(capture: Path, device_address: int) -> int:
    rows = capture_rows(capture, f"usb.device_address == {device_address}")
    if not rows:
        print(f"No frames found for USB device address {device_address}.")
        return 1

    endpoint_counts: Counter[str] = Counter()
    endpoint_bytes: Counter[str] = Counter()
    endpoint_payload_records: Counter[str] = Counter()
    endpoint_first: dict[str, str] = {}
    endpoint_last: dict[str, str] = {}
    commands: list[tuple[UsbRow, str]] = []
    responses: list[tuple[UsbRow, str]] = []
    eeprom_requests: list[tuple[UsbRow, str]] = []

    for row in rows:
        endpoint = parse_endpoint(row.endpoint)
        endpoint_label = f"0x{endpoint:02x}" if endpoint is not None else "setup"
        endpoint_counts[endpoint_label] += 1
        endpoint_bytes[endpoint_label] += len(row.payload)
        endpoint_first.setdefault(endpoint_label, row.time)
        endpoint_last[endpoint_label] = row.time
        if row.payload:
            endpoint_payload_records[endpoint_label] += 1
        if endpoint == COMMAND_ENDPOINT:
            decoded = decode_command(row.payload)
            if decoded:
                commands.append((row, decoded))
            else:
                decoded = decode_eeprom_request(row.payload)
                if decoded:
                    eeprom_requests.append((row, decoded))
        elif endpoint == STATUS_ENDPOINT:
            decoded = decode_response(row.payload)
            if decoded:
                responses.append((row, decoded))

    print(f"Capture: {capture}")
    print(f"USB device address: {device_address}")
    print(f"Frames: {len(rows)}")
    print("Endpoints:")
    for endpoint in sorted(endpoint_counts):
        print(
            f"  {endpoint}: {endpoint_counts[endpoint]} frame(s), "
            f"{endpoint_payload_records[endpoint]} payload record(s), "
            f"{endpoint_bytes[endpoint]} captured payload byte(s), "
            f"time {endpoint_first[endpoint]}..{endpoint_last[endpoint]}"
        )

    print(f"Decoded endpoint 0x04 commands: {len(commands)}")
    for row, decoded in commands[:100]:
        print(f"  frame {row.frame} time {row.time}: {decoded}")
    if len(commands) > 100:
        print(f"  ... {len(commands) - 100} additional command(s) omitted")

    print(f"Decoded endpoint 0x04 EEPROM requests: {len(eeprom_requests)}")
    for row, decoded in eeprom_requests[:100]:
        print(f"  frame {row.frame} time {row.time}: {decoded}")

    print(f"Decoded endpoint 0x83 responses: {len(responses)}")
    for row, decoded in responses[:100]:
        print(f"  frame {row.frame} time {row.time}: {decoded}")
    if len(responses) > 100:
        print(f"  ... {len(responses) - 100} additional response(s) omitted")
    return 0


def self_test() -> int:
    request = bytes.fromhex(
        "07 00 00 00 00 00 04 b0 00 00 00 00 00 00 00 00 00 00 00 00"
    )
    expected_request = (
        "index=7 opcode=1200 (0x04b0, GET_FIRMWARE_REV) "
        "param1=0x00000000 param2=0x00000000"
    )
    assert decode_command(request) == expected_request

    response = bytes.fromhex("00 01 07 00 00 05 03 18")
    assert decode_response(response) == (
        "status=0x00 type=1 index=7 result=0x00050318"
    )
    assert parse_endpoint("0x83") == 0x83
    assert parse_endpoint("83") == 83
    assert parse_payload("00:01:ff") == b"\x00\x01\xff"
    assert decode_eeprom_request(bytes.fromhex("08 18 20 08 00 00 05 66")) == (
        "index=8 opcode=1382 (READ_EEPROM) offset=0x1820 bytes=8"
    )
    print("self-test passed")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize USBPcap traffic for a FLEX-1500"
    )
    parser.add_argument("capture", nargs="?", type=Path)
    parser.add_argument("--device-address", type=int)
    parser.add_argument("--list-devices", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test()
    if args.capture is None:
        parser.error("capture is required unless --self-test is used")
    if not args.capture.is_file():
        parser.error(f"capture does not exist: {args.capture}")

    try:
        if args.list_devices:
            return list_devices(args.capture)
        if args.device_address is None:
            parser.error("use --list-devices or provide --device-address N")
        return summarize(args.capture, args.device_address)
    except RuntimeError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
