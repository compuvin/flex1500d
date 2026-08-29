# Firmware revision read probe

Status: implemented, tested offline, authorized, and executed once.

## Purpose

Validate the PowerSDR command request/response protocol by asking the radio for
its firmware revision with opcode 1200 (`GET_FIRMWARE_REV`). The command is
semantically read-only: the published PowerSDR source uses it to retrieve a
revision value and does not associate it with a configuration change.

This probe necessarily sends a packet to interrupt OUT endpoint `0x04`.
Therefore it remains permission-gated even though the command's documented
meaning is read-only.

## Exact request

```text
01 00 00 00 00 00 04 b0 00 00 00 00 00 00 00 00 00 00 00 00
```

Decoded fields:

- Request index: 1
- Opcode: 1200 / `0x000004b0`
- Parameter 1: 0
- Parameter 2: 0
- Result placeholder: 0

The encoder rejects every other opcode and rejects nonzero parameters.

## Exact armed behavior

1. Initialize libusb and open only USB device `2192:1502`.
2. Refuse to proceed if interface 3 has an active kernel driver.
3. Claim interface 3 without detaching a driver.
4. Submit one interrupt-IN response buffer on endpoint `0x83`.
5. Submit the exact 20-byte request above on interrupt OUT endpoint `0x04`.
6. Process events for up to one second per transfer.
7. Require a normal type-1 response carrying request index 1.
8. Decode bytes 4 through 7 as the big-endian firmware revision.
9. Release interface 3 and close the device.

## Explicit exclusions

- No opcode other than 1200.
- No sample endpoint `0x82` or `0x01` transfer.
- No USB control or bulk transfer.
- No reset, configuration, alternate-setting, or driver-detach operation.
- No frequency, gain, relay, PTT, firmware-update, or EEPROM operation.
- No repeated request, capture file, or network listener.

If cleanup is required, the program may cancel its own pending interrupt
transfer. This is a host-side cancellation, not a radio command.

## Offline safety checks

- The request builder is tested byte-for-byte against the exact packet above.
- Tests verify rejection of opcode 1219 (`INITIALIZE`) and nonzero parameters.
- Response decoding tests require message type 1 and the matching request index.
- The default executable path prints the plan without opening USB.
- The clean build is warning-free.
- Binary/source audit finds no control, bulk, reset, configuration,
  alternate-setting, or driver-detach call.

## Arming guard

The exact hardware command is:

```sh
./build/flex1500-firmware-read-probe --execute-approved-firmware-read-probe
```

It must not be executed without explicit permission from KB1JDX.

## First execution result

Date: 2026-08-27

KB1JDX explicitly authorized one execution. The exact request documented
above was sent once. Both interrupt transfers completed successfully:

```text
Request sent:      01 00 00 00 00 00 04 b0 00 00 00 00 00 00 00 00 00 00 00 00
Response received: 39 01 01 00 00 05 03 18
```

- Request transfer: status 0, 20 bytes
- Response transfer: status 0, 8 bytes
- Response type: 1 (normal result)
- Matching request index: 1
- Raw result: `0x00050318`
- Decoded firmware version: `0.5.3.24`

This confirms the 20-byte interrupt-OUT command encoding, indexed
interrupt-IN response mechanism, result byte order, and opcode 1200 behavior.
No excluded operation was performed.
