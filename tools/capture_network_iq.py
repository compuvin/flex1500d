#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only

"""Capture an exact number of F15I network samples as raw iq_s16le."""

from __future__ import annotations

import argparse
import http.client
from pathlib import Path
import struct
import sys


HEADER = struct.Struct(">4sBBHIII")
PAIR = struct.Struct("<ff")
OUTPUT_PAIR = struct.Struct("<hh")


def clamp_s16(value: float) -> int:
    rounded = round(value)
    return max(-32768, min(32767, rounded))


def capture(host: str, port: int, sample_count: int, discard_count: int,
            output_path: Path) -> tuple[int, int, int]:
    connection = http.client.HTTPConnection(host, port, timeout=10)
    connection.request("GET", "/v1/stream/iq")
    response = connection.getresponse()
    if response.status != 200:
        raise RuntimeError(f"IQ endpoint returned HTTP {response.status}")

    retained = 0
    discarded = 0
    frames = 0
    expected_sequence: int | None = None
    with output_path.open("xb") as output:
        while retained < sample_count:
            header = response.read(HEADER.size)
            if len(header) != HEADER.size:
                raise RuntimeError("IQ stream ended inside a frame header")
            magic, version, sample_format, header_size, sequence, rate, count = (
                HEADER.unpack(header)
            )
            if (magic != b"F15I" or version != 1 or sample_format != 1 or
                    header_size != HEADER.size or rate != 48000 or count == 0):
                raise RuntimeError("invalid F15I frame header")
            if expected_sequence is not None and sequence != expected_sequence:
                raise RuntimeError(
                    f"IQ sequence discontinuity: expected {expected_sequence}, "
                    f"received {sequence}"
                )
            expected_sequence = (sequence + 1) & 0xFFFFFFFF
            payload = response.read(count * PAIR.size)
            if len(payload) != count * PAIR.size:
                raise RuntimeError("IQ stream ended inside a sample payload")
            frames += 1
            for sample_i, sample_q in struct.iter_unpack("<ff", payload):
                if discarded < discard_count:
                    discarded += 1
                    continue
                if retained == sample_count:
                    break
                output.write(OUTPUT_PAIR.pack(
                    clamp_s16(sample_i), clamp_s16(sample_q)
                ))
                retained += 1
    connection.close()
    return retained, discarded, frames


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=15000)
    parser.add_argument("--samples", type=int, default=240000)
    parser.add_argument("--discard-samples", type=int, default=12000)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("port must be 1..65535")
    if args.samples <= 0 or args.discard_samples < 0:
        parser.error("sample counts must be positive/nonnegative")
    try:
        retained, discarded, frames = capture(
            args.host, args.port, args.samples, args.discard_samples,
            args.output
        )
    except (OSError, RuntimeError) as error:
        print(f"capture failed: {error}", file=sys.stderr)
        return 1
    print(
        f"captured {retained} samples after discarding {discarded} "
        f"settling samples from {frames} F15I frames: {args.output}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
