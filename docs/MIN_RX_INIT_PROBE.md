# Minimum RX initialization probe

Status: implemented, tested offline, authorized, and executed once.

## Why opcode 1219 is the minimum candidate

The recovered PowerSDR wrapper names opcode 1219 `USB_OP_INITIALIZE` and exposes
it as `USBHID.Initialize()`. The inspected PowerSDR application calls it after a
firmware-update path, then sets the preamp to zero. No smaller or more specific
start-ADC/start-CODEC command appears in the wrapper.

The command's firmware implementation is unavailable, so its exact internal
effects are unknown. This test deliberately sends it alone before adding any
frequency, filter, gain, routing, or CODEC-register command.

## Exact state-changing command

```text
02 00 00 00 00 00 04 c3 00 00 00 00 00 00 00 00 00 00 00 00
```

- Request index: 2
- Opcode: 1219 / `0x000004c3` (`INITIALIZE`)
- Parameter 1: 0
- Parameter 2: 0
- Result placeholder: 0

The executable has no opcode or parameter command-line option. The packet is
built from the fixed opcode and zero parameters, and a unit test verifies every
byte.

## Exact armed behavior

1. Initialize libusb and open only `2192:1502`.
2. Refuse if interface 3 has an active kernel driver.
3. Claim interface 3 without detaching a driver.
4. Send the one 20-byte `INITIALIZE` packet above to interrupt OUT endpoint
   `0x04` and require a complete transfer.
5. Submit one isochronous IN transfer on `0x82`: 64 packets of 192 bytes, or
   64 ms at 1,000 packets per second.
6. Report packet counts, bytes, the first payload, complex-sample count,
   nonzero-byte count, and signed I/Q ranges.
7. Release interface 3 and close the device.

## Explicit exclusions

- No other opcode.
- No endpoint `0x01` TX/sample-OUT traffic.
- No endpoint `0x83` status/response traffic.
- No control or bulk transfer.
- No reset, USB configuration, alternate-setting, or driver-detach operation.
- No frequency, gain, preamp, filter, relay, routing, PTT, firmware-update, or
  EEPROM command.
- No network listener or capture file.

## Risk and recovery

`INITIALIZE` is state-changing, and its internal firmware behavior is not fully
documented. Nothing in the recovered call site indicates PTT or transmission,
but we cannot prove every internal side effect from host code alone. The radio
may change CODEC, clock, routing, or relay state and may retain that state after
the USB handle closes.

If the radio behaves unexpectedly, stop further testing and power-cycle the
radio itself. Disconnecting only USB may not reset all radio hardware state.

## Interpretation

- Varying I/Q after `INITIALIZE` means one command is sufficient to activate
  the receive sample path.
- Continued uniform `0xffff` samples mean additional RX configuration is
  required.
- A USB timeout or stall means the recovered command needs a different host
  sequence or the installed firmware handles it differently.

## Arming guard

Without arguments, the executable prints this plan and does not open USB. The
exact hardware command is:

```sh
./build/flex1500-min-rx-init-probe --execute-approved-min-rx-init-probe
```

It must not be executed without explicit permission from KB1JDX.

## First execution result

Date: 2026-08-27

KB1JDX explicitly authorized one execution. The exact `INITIALIZE` packet
documented above was sent once and completed successfully:

```text
command status: 0
command bytes transferred: 20
```

The subsequent receive-only sample transfer produced:

- Overall transfer status: 0
- 64 completed packets
- 0 packet errors or timeouts
- 12,288 bytes
- 3,072 complex sample frames
- 6,543 nonzero bytes
- I range: -4096 to 109
- Q range: -5 to 22

The first displayed 192-byte packet still contained the earlier uniform `0xff`
idle pattern. Samples later in the same 64 ms transfer varied, as demonstrated
by the I/Q ranges and nonzero-byte count. This captures the sample path
transitioning from its pre-initialization sentinel to active data.

This result proves that one opcode 1219 `INITIALIZE` command is sufficient to
activate RX sample production on firmware 0.5.3.24. No frequency, gain, filter,
preamp, routing, companion sample-OUT, PTT, or other command was required for
the transition. No excluded operation was performed.

The wide negative I minimum may include the transition out of the `0xffff`
state rather than settled receiver data. A later, separately approved capture
should discard initial packets and analyze a longer settled receive window.
