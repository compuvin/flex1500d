# FLEX-1500 PA filter relay click probe

> **TX-owned research:** This probe is excluded from the default build. Its
> arming string is a safety interlock, not access control. It does not key the
> transmitter, but it changes the transmitter's PA-filter relay state and must
> remain within the reviewed research workflow described in
> `TRANSMIT_RESEARCH_SAFETY.md`.

## Purpose and state

Opcode 1260 (`SET_PA_FILTER`) is present in every captured audible-click
cluster: application startup, the HF band change, and application exit. The RX
filter has been excluded by two successful but inaudible tests.

This probe changes only the PA output filter bank. It never enables transmit,
PTT, MOX, PA bias, or endpoint-`0x01` streaming.

## Exact proposed operation

1. Send `SET_PA_FILTER(5)`, the value observed in PowerSDR's initial 7 MHz-band
   setup.
2. Wait three seconds with no USB operation.
3. Send `SET_PA_FILTER(0)`, the idle/bypass value PowerSDR sends on exit.
4. Release the interface and close USB.

Filter 5 packet:

```text
0e 00 00 00 00 00 04 ec 00 00 00 05 00 00 00 00 00 00 00 00
```

Filter 0 packet:

```text
0f 00 00 00 00 00 04 ec 00 00 00 00 00 00 00 00 00 00 00 00
```

The fixed executable accepts no filter values. Its armed path excludes
frequency, RX filters, antennas, gain, initialization, sample streaming, PTT,
MOX, PA bias, transmit enable, firmware, EEPROM, reset, and configuration
operations. Without the exact armed flag it prints the plan and never
initializes libusb.

## First execution

Date: 2026-08-28

KB1JDX explicitly approved the fixed test. `SET_PA_FILTER(5)` transferred
20/20 bytes, followed three seconds later by `SET_PA_FILTER(0)` at 20/20 bytes.
No other radio command or endpoint was used. The PA filter ended in PowerSDR's
known idle/bypass state 0. KB1JDX heard both clicks, confirming that opcode
1260 controls the audible relay bank. No `SET_TR` experiment is needed merely
to identify the startup/shutdown click source.
