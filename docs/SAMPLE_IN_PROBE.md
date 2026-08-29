# Isochronous sample-IN probe

Status: implemented, tested offline, authorized, and executed once.

## Question answered by this probe

Will the FLEX-1500 produce RX I/Q when the Linux host schedules only
isochronous IN traffic on endpoint `0x82`?

A successful result establishes that receive streaming can begin without first
sending a command or scheduling the companion OUT endpoint. A zero-data or
timeout result indicates that another prerequisite exists. Based on the
PowerSDR source, likely prerequisites would be a normal configuration command
sequence, a concurrently scheduled zero-filled endpoint `0x01` stream, or both.

## Exact armed behavior

1. Initialize libusb.
2. Open only USB device `2192:1502`.
3. Check interface 3 and refuse to continue if a kernel driver is active.
4. Claim interface 3 without detaching a driver.
5. Allocate one receive-only isochronous transfer.
6. Submit that transfer to IN endpoint `0x82` with:
   - 64 packets
   - 192 requested bytes per packet
   - 12,288 bytes total
   - 1,000 ms transfer timeout
7. Process libusb events until the transfer completes or times out.
8. Report packet counts, byte count, first payload bytes, complex-sample count,
   nonzero-byte count, and signed I/Q ranges.
9. Release interface 3 and close the device.

The 64 packets correspond to 64 ms at the documented 1,000 USB packets per
second. If all packets are full, they contain 3,072 complex I/Q sample frames.

## Explicit exclusions

- No endpoint `0x01`, `0x04`, or `0x83` transfer.
- No USB control or bulk transfer.
- No device reset.
- No configuration or alternate-setting change.
- No kernel-driver detach.
- No command, frequency, gain, relay, PTT, firmware, or EEPROM operation.
- No capture file and no network listener.

If host-side event handling fails unexpectedly, the program may cancel its own
pending IN transfer before cleanup. This cancellation does not submit a radio
command or an OUT payload.

## Arming guard

With no arguments, the executable prints its plan and exits without opening a
USB device. Hardware access requires the exact flag:

```sh
./build/flex1500-sample-in-probe --execute-approved-sample-in-probe
```

The flag must not be used until KB1JDX explicitly approves this probe run.

## First execution result

Date: 2026-08-27

KB1JDX explicitly authorized one execution. The transfer completed with
overall status 0 and produced:

- 64 completed packets
- 0 packet errors or timeouts
- 192 bytes in every packet
- 12,288 bytes total
- 3,072 decoded complex sample frames
- Every byte equal to `0xff`
- Every decoded I sample equal to -1
- Every decoded Q sample equal to -1

No excluded operation was performed.

This proves that endpoint `0x82` can stream on Linux with an IN transfer alone;
the companion OUT endpoint is not required merely to receive full-sized USB
packets. However, the uniform `0xffff` sample words are not live ADC data. They
are consistent with an uninitialized or idle sample-path sentinel. A radio
configuration/initialization command sequence is therefore still required to
obtain useful I/Q.

## Offline verification

- The endpoint policy permits only isochronous `0x82` and interrupt `0x83` IN.
- I/Q decoding is tested with known little-endian signed-16-bit values.
- The default offline executable path is part of the CTest suite.
- A clean build completes with compiler warnings enabled.
- The binary/source audit finds no control, bulk, reset, configuration,
  alternate-setting, or driver-detach call.
