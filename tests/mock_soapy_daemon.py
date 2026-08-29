#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Run the SoapySDR adapter test against a loopback-only mock API daemon."""

from __future__ import annotations

import http.server
import os
from pathlib import Path
import socketserver
import struct
import subprocess
import sys
import threading


RADIO = b'''{
  "model": "FLEX-1500",
  "receive_only": true,
  "transmit_enabled": false,
  "rx_tuning_enabled": true,
  "frequency_hz": 7000000
}\n'''
STATUS = b'{"service":"flex1500d","api_version":1}\n'


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def send_body(self, status: int, body: bytes,
                  content_type: str = "application/json") -> None:
        self.send_response(status)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:  # noqa: N802 - HTTP handler API
        if self.path == "/v1/radio":
            self.send_body(200, RADIO)
        elif self.path == "/v1/status":
            self.send_body(200, STATUS)
        elif self.path == "/v1/stream/iq":
            pairs = ((100.0, -200.0), (300.0, -400.0), (500.0, -600.0))
            payload = b"".join(struct.pack("<ff", *pair) for pair in pairs)
            frame = struct.pack(">4sBBHIII", b"F15I", 1, 1, 20, 0, 48000,
                                len(pairs)) + payload
            self.send_body(200, frame, "application/octet-stream")
        else:
            self.send_body(404, b'{"error":"not found"}\n')

    def do_PUT(self) -> None:  # noqa: N802 - HTTP handler API
        if self.path == "/v1/radio/frequency/7100000":
            self.send_body(200, b'{"frequency_hz":7100000}\n')
        else:
            self.send_body(404, b'{"error":"not found"}\n')

    def log_message(self, _format: str, *_args: object) -> None:
        pass


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: mock_soapy_daemon.py TEST_EXECUTABLE MODULE_DIR",
              file=sys.stderr)
        return 2
    with Server(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        environment = os.environ.copy()
        environment["SOAPY_SDR_PLUGIN_PATH"] = str(Path(sys.argv[2]).resolve())
        result = subprocess.run(
            [sys.argv[1], str(server.server_address[1])], env=environment,
            check=False
        )
        server.shutdown()
        thread.join()
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
