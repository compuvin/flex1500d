# PowerSDR application launch/exit USB capture

Capture: `pcaps/flex1500-program-start-stop.pcapng`

This capture records opening and closing PowerSDR, rather than pressing its
radio Start/Stop button. The FLEX-1500 is USB device address 2. Analysis is
offline and does not access the radio.

## Main finding

There is no single initialize/stop command pair:

- Opcode 1219 (`INITIALIZE`) does not appear.
- Opcode 1286 (`SET_POWER_OFF`) does not appear.
- Application startup is a sequence of discovery reads, EEPROM reads, codec or
  audio setup, antenna/filter selection, amplifier selection, preamp selection,
  and status reads.
- Application exit sends only `SET_TR(0)` and `SET_PA_FILTER(0)`, then cancels
  the interrupt-IN listener several seconds later.

The audible clicks are most plausibly the electromechanical relay commands in
the startup and exit clusters.

Subsequent permission-gated hardware tests excluded `SET_RX1_FILTER` (no click
in two runs) and confirmed `SET_PA_FILTER` (audible clicks for filter 5 and
filter 0, three seconds apart). The PA filter is therefore the identified click
source.

## Timeline

### Discovery: 8.105–11.131 seconds

PowerSDR reads:

- Serial number four times during device discovery.
- TRX serial and revision.
- PA serial and revision.
- Eight EEPROM bytes at offset `0x1820`.
- Regulatory region.
- Serial number once more.
- Firmware revision, returning `0x00050318` (0.5.3.24).

These are requests sent on endpoint `0x04`, with normal or EEPROM responses on
endpoint `0x83`.

### Initial hardware setup: 18.007–18.124 seconds

Seven as-yet-unlabeled opcodes 1375–1380 configure values `1`, `60`, and `160`.
Their repetition and values suggest codec/audio path setup, but that name is an
inference; the available decompiled wrapper predates these six opcode labels.

The known operations that follow are:

| Time | Operation |
|---:|---|
| 18.061751 | `SET_RX1_ANT(0)` |
| 18.069729 | `SET_TX_ANT(0)` |
| 18.079335 | `SET_RX1_FILTER(6)` |
| 18.089723 | `SET_PA_FILTER(5)` |
| 18.095629 | `SET_AMP_TX1(0)` |
| 18.102946 | Read firmware revision |
| 18.123549 | `SET_XREF(0)` |

The RX and PA filter writes are the first strong candidates for the audible
startup relay click.

### Restore saved settings: 19.208–22.041 seconds

PowerSDR repeats the seven 1375–1380 setup writes, then:

| Time | Operation |
|---:|---|
| 20.365764 | `SET_AMP_TX1(1)` |
| 20.394927–20.474354 | Six one-byte EEPROM reads |
| 20.493385 | `SET_RX1_ANT(0)` |
| 20.495118 | `SET_TX_ANT(0)` |
| 20.509563 | `SET_RX1_FILTER(2)` |
| 20.515594 | `SET_PA_FILTER(2)` |
| 20.547013 | `SET_TRX_PREAMP(3)` |
| 22.038201 | Read status |

Filter index 2 corresponds to the 25.3–37.6 MHz range, consistent with restoring
the 28.475 MHz setting represented in the accompanying frequency-change
capture. This second filter pair is another likely startup click source.

### Application exit: 40.535 seconds

Only two normal commands are sent:

| Time | Operation |
|---:|---|
| 40.534759 | `SET_TR(0)` |
| 40.536271 | `SET_PA_FILTER(0)` |

These commands are only 1.512 ms apart and would plausibly sound like one click.
`SET_PA_FILTER(0)` returns the PA filter bank to bypass/default. The exact relay
effect of `SET_TR(0)` should be treated as hardware routing until confirmed from
schematics or a dedicated capture.

The last endpoint-`0x83` record occurs at 44.111164 seconds, consistent with
PowerSDR subsequently cancelling its asynchronous status listener. No additional
radio command accompanies that cancellation.

## Traffic totals

- 41 normal 20-byte commands.
- Seven EEPROM reads and zero EEPROM writes.
- Twenty decoded responses: fourteen normal results and six additional
  one-byte EEPROM results; the initial eight-byte EEPROM result is included
  among the fourteen longer responses.
- No isochronous endpoint-`0x01` or endpoint-`0x82` traffic, because the radio
  Start button was not engaged during this capture.
