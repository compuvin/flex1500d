#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Exercise the CLI bridge ownership lifecycle against a loopback API."""

from __future__ import annotations

import http.server
import signal
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
                            b'"frequency_hz":14225000,"rx_mode":"usb"}\n')
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
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        process = subprocess.Popen(
            [sys.argv[1], "--host", "127.0.0.1", "--port",
             str(server.server_address[1])],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True,
        )
        time.sleep(5.5)
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=3)
        server.shutdown()
        thread.join()

    if process.returncode != 0:
        print(stdout, stderr, file=sys.stderr)
        return process.returncode
    if (Handler.acquired != 1 or Handler.keepalives < 1 or
            Handler.releases != 1 or Handler.iq_requests < 1):
        print("unexpected ownership lifecycle:", Handler.acquired,
              Handler.keepalives, Handler.releases, Handler.iq_requests,
              file=sys.stderr)
        return 1
    required = ("connected to 127.0.0.1", "FLEX-1500 at 14225000 Hz usb",
                "station control acquired", "RX IQ stream connected",
                "station control released")
    if any(text not in stdout for text in required):
        print("unexpected client output:\n" + stdout, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
