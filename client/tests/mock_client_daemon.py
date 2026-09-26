#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Exercise the CLI bridge ownership lifecycle against a loopback API."""

from __future__ import annotations

import http.server
import shutil
import signal
import socket
import socketserver
import struct
import subprocess
import sys
import threading
import time


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    acquired = 0
    keepalives = 0
    releases = 0
    iq_requests = 0
    frequency_sets = 0
    mode_sets = 0
    bandwidth_sets = 0

    def reply(self, status: int, body: bytes) -> None:
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802
        if self.path == "/v1/status":
            self.reply(200, b'{"software_version":"1.2.3",'
                            b'"git_revision":"abc12345"}\n')
        elif self.path == "/v1/radio":
            self.reply(200, b'{"model":"FLEX-1500",'
                            b'"frequency_hz":null,"rx_gain_db":20,'
                            b'"rx_mode":"usb",'
                            b'"rx_bandwidth_hz":2700}\n')
        elif self.path == "/v1/stream/iq":
            Handler.iq_requests += 1
            samples = b"".join(struct.pack("<ff", 100.0, -50.0)
                               for _ in range(256))
            frame = struct.pack(">4sBBHIII", b"F15I", 1, 1, 20, 0,
                                48000, 256) + samples
            self.reply(200, frame)
        else:
            self.reply(404, b'{"error":"not found"}\n')

    def do_POST(self) -> None:  # noqa: N802
        if self.path == "/v1/control/owner":
            Handler.acquired += 1
            self.reply(201, b'{"owner":true,"lease":31337}\n')
        else:
            self.reply(404, b'{"error":"not found"}\n')

    def do_PUT(self) -> None:  # noqa: N802
        valid = self.headers.get("X-Flex1500-Control-Lease") == "31337"
        if self.path == "/v1/control/owner/keepalive" and valid:
            Handler.keepalives += 1
            self.reply(200, b'{"owner":true,"lease":31337}\n')
        elif self.path == "/v1/radio/frequency/7100000" and valid:
            Handler.frequency_sets += 1
            self.reply(200, b'{"frequency_hz":7100000,"rx_filter":6}\n')
        elif self.path == "/v1/radio/mode/lsb" and valid:
            Handler.mode_sets += 1
            self.reply(200, b'{"rx_mode":"lsb","rx_bandwidth_hz":2700}\n')
        elif self.path == "/v1/radio/bandwidth/2400" and valid:
            Handler.bandwidth_sets += 1
            self.reply(200, b'{"rx_bandwidth_hz":2400}\n')
        else:
            self.reply(410, b'{"error":"owner_stale"}\n')

    def do_DELETE(self) -> None:  # noqa: N802
        valid = self.headers.get("X-Flex1500-Control-Lease") == "31337"
        if self.path == "/v1/control/owner" and valid:
            Handler.releases += 1
            self.reply(200, b'{"owner":false,"lease":31337}\n')
        else:
            self.reply(410, b'{"error":"owner_stale"}\n')

    def log_message(self, _format: str, *_args: object) -> None:
        pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: mock_client_daemon.py CLIENT", file=sys.stderr)
        return 2
    with Server(("127.0.0.1", 0), Handler) as server:
        with socket.socket() as reservation:
            reservation.bind(("127.0.0.1", 0))
            rigctl_port = reservation.getsockname()[1]
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        process = subprocess.Popen(
            [sys.argv[1], "--host", "127.0.0.1", "--port",
             str(server.server_address[1]), "--rigctl-port",
             str(rigctl_port)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        rig = None
        for _ in range(30):
            try:
                rig = socket.create_connection(("127.0.0.1", rigctl_port),
                                               timeout=0.2)
                break
            except OSError:
                time.sleep(0.1)
        if rig is None:
            process.terminate()
            stdout, stderr = process.communicate(timeout=3)
            print("rig-control listener did not start", stdout, stderr,
                  file=sys.stderr)
            return 1
        with rig, rig.makefile("rwb", buffering=0) as wire:
            exchanges = (
                (b"\\chk_vfo\n", [b"CHKVFO 1\n"]),
                (b"\\dump_state\n", [
                    b"0\n", b"2\n", b"1\n",
                    b"100000 54000000 0x2f -1 -1 0x1 0x0\n",
                    b"0 0 0 0 0 0 0\n", b"0 0 0 0 0 0 0\n",
                    b"0x2f 1\n", b"0 0\n", b"0x1 6000\n",
                    b"0x20 12000\n", b"0xc 2700\n", b"0x2 500\n",
                    b"0 0\n", b"0\n", b"0\n", b"0\n", b"0\n",
                    b"\n", b"\n", b"0\n", b"0\n", b"0\n",
                    b"0\n", b"0\n", b"0\n",
                ]),
                (b"f\n", [b"RPRT -11\n"]),
                (b"m\n", [b"USB\n", b"2700\n"]),
                (b"F 7100000\n", [b"RPRT 0\n"]),
                (b"M LSB 2400\n", [b"RPRT 0\n"]),
                (b"m\n", [b"LSB\n", b"2400\n"]),
                (b"t\n", [b"0\n"]),
                (b"F VFOA 7100000\n", [b"RPRT 0\n"]),
                (b"M VFOA LSB 2400\n", [b"RPRT 0\n"]),
                (b"T VFOA 1\n", [b"RPRT -4\n"]),
            )
            for request, expected_lines in exchanges:
                wire.write(request)
                for expected in expected_lines:
                    actual = wire.readline()
                    if actual != expected:
                        process.terminate()
                        stdout, stderr = process.communicate(timeout=3)
                        print("unexpected rig response:", request, actual,
                              "expected", expected, stdout, stderr,
                              file=sys.stderr)
                        return 1
        rigctl = shutil.which("rigctl")
        if rigctl is not None:
            check = subprocess.run(
                [rigctl, "-m", "2", "-r",
                 f"127.0.0.1:{rigctl_port}", "f"],
                capture_output=True, text=True, timeout=3,
            )
            if check.returncode != 0 or check.stdout.strip() != "7100000":
                process.terminate()
                stdout, stderr = process.communicate(timeout=3)
                print("Hamlib NET rigctl interoperability failed:",
                      check.returncode, check.stdout, check.stderr,
                      stdout, stderr, file=sys.stderr)
                return 1
        time.sleep(5.5)
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=3)
        server.shutdown()
        thread.join()

    if process.returncode != 0:
        print(stdout, stderr, file=sys.stderr)
        return process.returncode
    if (Handler.acquired != 1 or Handler.keepalives < 1 or
            Handler.releases != 1 or Handler.iq_requests < 1 or
            Handler.frequency_sets != 2 or Handler.mode_sets != 2 or
            Handler.bandwidth_sets != 2):
        print("unexpected ownership lifecycle:", Handler.acquired,
              Handler.keepalives, Handler.releases, Handler.iq_requests,
              Handler.frequency_sets, Handler.mode_sets,
              Handler.bandwidth_sets,
              file=sys.stderr)
        return 1
    required = ("connected to 127.0.0.1", "FLEX-1500 usb",
                "station control acquired", "RX IQ stream connected",
                "Hamlib-compatible control ready", "frequency set to 7100000",
                "mode set to LSB; bandwidth 2400",
                "station control released")
    if any(text not in stdout for text in required):
        print("unexpected client output:\n" + stdout, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
