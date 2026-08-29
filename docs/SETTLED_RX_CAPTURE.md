# Settled receive capture

Status: implemented, tested offline, authorized, and executed once.

## Purpose

Capture approximately one second of already-initialized RX I/Q without sending
another initialization or configuration command. This tests sustained USB
reception and produces a small raw sample file for offline analysis.

## Exact armed behavior

1. Refuse to overwrite `captures/rx-settled.iq16le`.
2. Open only USB device `2192:1502`.
3. Refuse unless interface 3 is free of a kernel driver.
4. Claim interface 3 without detaching a driver.
5. Queue eight isochronous IN transfers on endpoint `0x82`.
6. Each transfer requests 128 packets of 192 bytes.
7. Receive 1,024 packets, corresponding to approximately 1.024 seconds.
8. Discard the first 32 packets (32 ms).
9. Write the remaining packets to `captures/rx-settled.iq16le`.
10. Report packet errors, sample/sentinel counts, I/Q ranges, and I/Q means.
11. Release the interface and close the radio.

If every retained packet is complete, the output contains 190,464 bytes and
47,616 complex sample frames, or 0.992 seconds at 48 kHz.

## Raw file format

The file contains no header. Each complex sample is four bytes:

```text
I low byte, I high byte, Q low byte, Q high byte
```

Both values are little-endian signed 16-bit integers at 48,000 complex samples
per second.

## Explicit exclusions

- No endpoint `0x01`, `0x04`, or `0x83` transfer.
- No radio command or initialization packet.
- No control or bulk transfer.
- No reset, configuration, alternate-setting, or driver-detach operation.
- No PTT, TX, frequency, gain, filter, relay, routing, firmware, or EEPROM work.
- No network listener.

The only possible USB payload direction is endpoint `0x82` IN. Cleanup may
cancel an incomplete host-side IN transfer if an error occurs.

## Offline verification

- All six tests pass.
- The clean build is warning-free.
- The default path does not open USB or create the output file.
- Symbol/source audit finds no command, control, bulk, reset, configuration,
  alternate-setting, or driver-detach operation.

## Arming guard

The exact hardware command is:

```sh
./build/flex1500-settled-rx-capture --execute-approved-settled-rx-capture
```

It must not be executed without explicit permission from KB1JDX.

## First execution result

Date: 2026-08-27

KB1JDX explicitly authorized one execution. The receive-only capture
completed successfully:

- 1,024 completed packets
- 0 packet errors or timeouts
- 32 discarded startup packets
- 190,464 retained bytes
- 47,616 retained complex sample frames
- 47,616 non-sentinel frames
- I range: 95 to 103
- Q range: 15 to 22
- I mean: 98.666394
- Q mean: 18.124706

The file was written to `captures/rx-settled.iq16le` with SHA-256:

```text
9601a17b25757add94a4b090e4d81390119cbad86f54677766f1380503df3538
```

The exact expected file size and zero packet errors confirm sustained 48 kHz
sample transport for this window. Every retained frame differs from the earlier
`I=-1, Q=-1` sentinel. The narrow sample ranges and nonzero means show a stable
DC offset that a receiver DSP path will need to remove. No excluded operation
was performed.
