# Copyright, attribution, and provenance

Copyright (C) 2026 KB1JDX

The original source code in this repository is licensed under the GNU General
Public License version 3 only (`GPL-3.0-only`). See `LICENSE`.

## Project provenance

This project is an independent, clean Linux implementation developed from:

- direct observation and controlled testing of a FLEX-1500 owned by KB1JDX;
- offline analysis of USB traffic captured from a working PowerSDR setup; and
- inspection of the public `ke9ns/PowerSDR-KE9NS-v2.8.0` repository at commit
  `12cdc2bb3b2a777cf4dfb5b5a0a6a78242eef275`, as detailed in
  `docs/PROTOCOL_FINDINGS.md`.

No PowerSDR, FlexRadio, Jungo, Windows driver, DLL, firmware, packet-capture,
or other proprietary binary is included in the publishable repository. The
inspected upstream source is not vendored or copied into this project.

## Dependencies

The software builds against the system-provided `libusb-1.0` library and the C
standard library. Development and CI use CMake, pkg-config, a C compiler, and
GitHub's checkout action. Those external projects remain subject to their own
licenses; they are not redistributed by this repository.

## Names and affiliation

FlexRadio, FLEX-1500, and PowerSDR are used only to identify compatible
hardware/software and describe the reverse-engineering work. This project is
not affiliated with, endorsed by, supported by, or maintained by FlexRadio
Systems. Product names and trademarks belong to their respective owners.
