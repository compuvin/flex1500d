<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Mockable USB command and TX-stream layer

`include/flex1500/usb_io.h` defines the narrow hardware-I/O boundary used by
the transmit lifecycle:

- submit one encoded endpoint-`0x04` command and report its exact byte count;
- start the endpoint-`0x01` TX stream;
- service that stream for a specified transition interval; and
- stop and drain the TX stream.

The production adapter in `src/usb_rx.c` implements these operations with
libusb. Initialization and every later radio command use the command operation;
Tune, physical-microphone TX, and HTTP audio/raw-IQ TX use the shared stream
operations. Packet construction, TX policy, ownership, and cleanup remain
above the interface, so tests can fail I/O without emulating libusb transfer
objects or opening a radio.

`tests/usb_io_test.c` supplies an in-memory implementation. It records exact
commands and stream calls, returns a chosen error on a deterministic operation
number, and verifies that stop/cleanup remains callable after earlier errors.
`tests/tx_failure_injection_test.c` then exercises the complete ordered I/O
matrix. It fails each of nine preparation/key operations and each of seven
cleanup operations in turn, plus a no-failure case. Every partial start derives
its required cleanup plan from the reached state. A cleanup-command failure is
recorded as the first error but never suppresses later safety actions.

Covered start operations are PA selection, amplifier enable, stream start,
pre-roll service, transition mute, exact TX frequency, `SET_TR(1)`, transition
service, and transition unmute. Covered cleanup operations are transition mute,
`SET_TR(0)`, RX-frequency restoration, transition unmute, PA reset, amplifier
disable, and stream stop.

The asynchronous endpoint-`0x82` receive-input and endpoint-`0x83` physical-
status machinery remains inside the libusb production backend. Failure tests
for those receive/status paths can extend this interface or use a separate
event-source boundary without changing TX ownership policy.
