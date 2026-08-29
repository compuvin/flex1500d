# Hardware safety policy

Development follows an explicit permission boundary: anything that is not
demonstrably read-only to the radio requires KB1JDX's permission before it
is run.

## Allowed without additional radio permission

- Inspect project files and host software.
- Read Linux sysfs metadata cached by the kernel.
- Enumerate USB devices and request standard USB descriptors.
- Analyze previously recorded captures and data files.
- Build and run tests that do not open the USB device.

## Permission required before execution

- Claiming an interface and submitting transfers, including IN-only probes.
- Sending any control or interrupt request to the radio.
- Writing either OUT endpoint (`0x01` or `0x04`).
- Starting a TX sample stream or keying PTT.
- Changing frequency, gain, relays, routing, or configuration.
- Resetting the USB device or changing its configuration/alternate setting.
- Uploading firmware or writing EEPROM/register state.

## First-phase code restrictions

The RX-probe policy permits only:

- Isochronous IN transfers from endpoint `0x82`.
- Interrupt IN transfers from endpoint `0x83`.

It rejects every control transfer and every other endpoint, including both
known OUT endpoints. The policy is tested exhaustively across all 256 endpoint
address values.

This policy is a guardrail, not proof that an operation is harmless. A proposed
hardware probe must still be reviewed and explicitly approved before execution.

## Transmit research boundary

The default CMake build excludes all standalone TX and PA-filter research
executables. Their source and results remain in the repository for audit and
protocol research. If explicitly compiled, they are minimally tested fixed
experiments, not supported transmitter functions.

Their long command-line arming strings prevent casual or accidental execution;
they are not authentication, authorization, access control, or a security
boundary. Anyone who can run a locally compiled program can inspect or modify
those strings and the source.

No TX probe may be executed on an antenna. A suitable 50-ohm dummy load, an
independently reviewed fixed test plan, explicit authorization, and appropriate
RF observation are required. A dummy load is necessary but does not make an
otherwise unreviewed experiment safe. See `TRANSMIT_RESEARCH_SAFETY.md`.
