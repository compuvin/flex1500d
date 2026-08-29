# Proposed first hardware probe

Status: status-IN portion implemented, reviewed, authorized, and executed once.

## Goal

Determine whether the powered FLEX-1500 emits sample or status packets without
first receiving a proprietary initialization command.

## Proposed operations

1. Initialize a libusb host context.
2. Open only USB device `2192:1502`.
3. Read and validate its standard descriptors.
4. Claim interface 3 without detaching a kernel driver.
5. Make ten 250 ms interrupt-IN attempts on endpoint `0x83` by default.
6. Print packet lengths and bytes to standard output.
7. Release the interface and close the handle.

## Explicitly excluded

- No endpoint `0x01` or `0x04` transfers.
- No vendor-specific control requests.
- No configuration or alternate-setting changes.
- No device reset.
- No frequency, gain, PTT, firmware, or EEPROM operations.
- No daemon or network listener.
- No access to sample endpoint `0x82`; that requires a later implementation,
  review, and permission request.

The executable stays offline unless passed the deliberately verbose
`--execute-approved-status-probe` flag. Builds and tests invoke it without that
flag. Running it requires explicit permission from KB1JDX.

## First execution result

Date: 2026-08-27

KB1JDX explicitly authorized the status-IN-only probe. Ten reads were
submitted to interrupt IN endpoint `0x83`, each with a 250 ms timeout. All ten
timed out and zero packets were received. The interface was then released and
the device was closed normally.

No control transfer, OUT transfer, sample-endpoint transfer, reset,
configuration change, alternate-setting change, driver detach, or radio command
was performed.

The result is consistent with either of these possibilities:

- Endpoint `0x83` reports events only when radio state changes.
- The radio does not start its status path until it receives proprietary host
  initialization.

The result does not distinguish between those possibilities.
