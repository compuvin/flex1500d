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
  "receive_only": false,
  "transmit_enabled": true,
  "rx_tuning_enabled": true,
  "frequency_hz": 7000000
  ,"rx_gain_db": 20
  ,"rx_bandwidth_hz": 6000
  ,"rx_squelch_db": -120
  ,"tx_drive_percent": 100
}\n'''
STATUS = b'{"service":"flex1500d","api_version":1}\n'


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    tx_bytes = bytearray()
    session_count = 0
    ptt_starts = 0
    ptt_stops = 0
    releases = 0
    stream_closed = threading.Event()
    owner_held = False
    owner_lock = threading.Lock()

    def tx_headers_valid(self) -> bool:
        return (self.headers.get("X-Flex1500-Control-Lease") == "31337" and
                self.headers.get("X-Flex1500-TX-Lease") == "424242")

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
        elif self.path == "/v1/radio/gain/10":
            self.send_body(200, b'{"rx_gain_db":10}\n')
        elif self.path == "/v1/radio/bandwidth/2400":
            self.send_body(200, b'{"rx_bandwidth_hz":2400}\n')
        elif self.path == "/v1/radio/squelch/-60":
            self.send_body(200, b'{"rx_squelch_db":-60}\n')
        elif self.path == "/v1/radio/tx-drive/75":
            self.send_body(200, b'{"tx_drive_percent":75}\n')
        elif self.path == "/v1/tx/ptt/start":
            if not self.tx_headers_valid():
                self.send_body(409, b'{"error":"station_owned"}\n')
                return
            Handler.ptt_starts += 1
            self.send_body(200, b'{"state":"transmitting"}\n')
        elif self.path == "/v1/tx/ptt/stop":
            if not self.tx_headers_valid():
                self.send_body(409, b'{"error":"station_owned"}\n')
                return
            Handler.ptt_stops += 1
            self.send_body(200, b'{"state":"reserved"}\n')
        elif self.path == "/v1/tx/sessions/keepalive":
            if not self.tx_headers_valid():
                self.send_body(409, b'{"error":"station_owned"}\n')
                return
            self.send_body(200, b'{"state":"reserved"}\n')
        elif self.path == "/v1/control/owner/keepalive":
            with Handler.owner_lock:
                held = Handler.owner_held
            self.send_body(200 if held else 410,
                           b'{"owner":true,"lease":31337}\n' if held else
                           b'{"error":"owner_stale"}\n')
        else:
            self.send_body(404, b'{"error":"not found"}\n')

    def do_POST(self) -> None:  # noqa: N802 - HTTP handler API
        length = int(self.headers.get("Content-Length", "0"))
        body = self.rfile.read(length)
        if self.path == "/v1/control/owner":
            with Handler.owner_lock:
                if Handler.owner_held:
                    acquired = False
                else:
                    Handler.owner_held = True
                    acquired = True
            self.send_body(201 if acquired else 409,
                           b'{"owner":true,"lease":31337}\n' if acquired else
                           b'{"error":"owner_busy"}\n')
        elif self.path == "/v1/tx/sessions" and b'"source":"iq"' in body:
            if self.headers.get("X-Flex1500-Control-Lease") != "31337":
                self.send_body(409, b'{"error":"station_owned"}\n')
                return
            Handler.session_count += 1
            self.send_body(201, b'{"lease":424242,"state":"reserved"}\n')
        else:
            self.send_body(422, b'{"error":"invalid"}\n')

    def do_CONNECT(self) -> None:  # noqa: N802 - HTTP handler API
        if self.path != "/v1/tx/stream":
            self.send_body(404, b'{"error":"not found"}\n')
            return
        if not self.tx_headers_valid():
            self.send_body(409, b'{"error":"station_owned"}\n')
            return
        self.send_response(200, "Connection Established")
        self.send_header("Content-Type", "application/octet-stream")
        self.end_headers()
        try:
            while True:
                data = self.connection.recv(65536)
                if not data:
                    break
                Handler.tx_bytes.extend(data)
        finally:
            Handler.stream_closed.set()
            self.close_connection = True

    def do_DELETE(self) -> None:  # noqa: N802 - HTTP handler API
        if self.path == "/v1/tx/sessions/current":
            if not self.tx_headers_valid():
                self.send_body(409, b'{"error":"station_owned"}\n')
                return
            Handler.releases += 1
            self.send_body(200, b'{"state":"idle"}\n')
        elif self.path == "/v1/control/owner":
            with Handler.owner_lock:
                Handler.owner_held = False
            self.send_body(200, b'{"owner":false,"lease":31337}\n')
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
        module_dir = Path(sys.argv[2]).resolve()
        environment["SOAPY_SDR_PLUGIN_PATH"] = str(module_dir)
        # Do not let a previously installed module mask the build under test.
        environment["SOAPY_SDR_ROOT"] = str(module_dir / "isolated-root")
        result = subprocess.run(
            [sys.argv[1], str(server.server_address[1])], env=environment,
            check=False
        )
        Handler.stream_closed.wait(2)
        server.shutdown()
        thread.join()
        if result.returncode != 0:
            return result.returncode
        expected_bytes = 33000 * 4
        if (Handler.session_count != 1 or Handler.ptt_starts != 1 or
                Handler.ptt_stops != 1 or Handler.releases != 1 or
                len(Handler.tx_bytes) != expected_bytes):
            print("unexpected TX lifecycle:", Handler.session_count,
                  Handler.ptt_starts, Handler.ptt_stops, Handler.releases,
                  len(Handler.tx_bytes), file=sys.stderr)
            return 1
        first_i, first_q = struct.unpack_from("<hh", Handler.tx_bytes)
        if first_i != 8192 or first_q != -16384:
            print("unexpected TX orientation:", first_i, first_q,
                  file=sys.stderr)
            return 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
