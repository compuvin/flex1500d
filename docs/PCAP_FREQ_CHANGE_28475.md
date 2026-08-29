# PowerSDR frequency-change capture

Capture: `pcaps/flex1500-freq-change-28475.pcapng`

The capture begins with PowerSDR already receiving and records a change toward
28.475 MHz. The starting dial frequency is not encoded anywhere in this file
and must not be assumed to be exactly 7.000 MHz.

## Commands

Only five normal commands occur:

| Time | Operation |
|---:|---|
| 11.647337 s | `SET_TRX_PREAMP(3)` |
| 11.650058 s | `SET_TRX_PREAMP(3)` again |
| 11.709643 s | `SET_RX1_FREQ_TW(0x25f60000)` |
| 11.712656 s | `SET_RX1_FILTER(2)` |
| 11.714318 s | `SET_PA_FILTER(2)` |

The exact frequency packet is:

```text
3a 00 00 00 00 00 05 43 25 f6 00 00 00 00 00 00 00 00 00 00
```

With the normal 384 MHz reference, tuning word `0x25f60000` selects a measured
hardware center of 28,470,703.132 Hz. If the PowerSDR dial target was exactly
28.475 MHz, its host DSP/VFO correction accounts for the remaining
approximately 4,296.868 Hz.

The exact unoffset 28.475 MHz word would be `0x25f77777`, and merely clearing
its low 16 bits would produce `0x25f70000`. The captured lower word therefore
shows that another host-side VFO/DSP offset was active. The USB capture does not
contain enough host state to reconstruct that offset or the initial displayed
frequency.

## Relay behavior

The RX-filter command follows the frequency packet by 3.013 ms. The PA-filter
command follows it by 4.675 ms and follows the RX-filter command by 1.662 ms.
Filter index 2 is the documented 25.3–37.6 MHz selection.

These two nearly adjacent filter-bank writes are the strongest explanation for
an audible band-change click. The preamp commands occur about 60 ms earlier and
both request the same mode; they are less likely to explain a relay transition
unless the prior preamp state differed.

Later controlled tests confirmed that `SET_PA_FILTER` clicks audibly while
`SET_RX1_FILTER` does not. The PA-filter write is therefore the audible
band-change relay in this sequence.

## Streaming behavior

Both isochronous streams remain active across the change:

- Endpoint `0x82`: 2,382 payload records, 3,658,752 captured bytes.
- Endpoint `0x01`: 2,381 payload records, 3,657,216 captured bytes.
- Every payload record is 1,536 bytes, representing 384 complex or stereo
  frames (8 ms at 48 kHz).

No `INITIALIZE`, stream restart, mute, PTT, MOX, or explicit stop command occurs.
There is no endpoint-`0x83` response traffic in this capture.

## Minimal future click test

A controlled RX-only relay test could send `SET_RX1_FILTER(5)` for the 7.7–11.4
MHz range and then restore `SET_RX1_FILTER(2)`. It should exclude the PA filter,
frequency, preamp, PTT, and all transmit operations. The radio's actual current
filter state cannot be read from this capture, so the exact packets and restore
assumption must be reviewed before any execution.
