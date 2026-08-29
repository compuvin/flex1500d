# PowerSDR Start/Stop USB capture

Capture: `pcaps/flex1500-power-on-off.pcapng`

Recorded: 2026-08-28 with Wireshark 4.6.8 on Windows 11. The FLEX-1500 is USB
device address 2. Analysis is offline and does not access the radio.

## High-level sequence

Times are relative to the beginning of the pcapng file.

| Time | Event |
|---:|---|
| 2.943473 s | PowerSDR begins submitting endpoint-`0x82` RX transfers. |
| 2.950765 s | PowerSDR sends one `SET_RX1_FREQ_TW` command. |
| 2.951074 s | First captured RX I/Q payload completes. |
| 3.153266 s | Endpoint-`0x01` audio/TX-side scheduling begins. |
| 5.907623 s | Last captured endpoint-`0x01` payload. |
| 5.917290 s | Last endpoint-`0x01` transfer record. |
| 6.009450 s | Last captured endpoint-`0x82` payload. |
| 6.009499 s | Last endpoint-`0x82` transfer record. |

Shutdown uses transfer cancellation/completion rather than a radio command.

## Commands

Only one 20-byte endpoint-`0x04` command appears:

```text
36 00 00 00 00 00 05 43 09 53 00 00 00 00 00 00 00 00 00 00
```

- Request index: `0x36`
- Opcode: 1347 / `0x0543` (`SET_RX1_FREQ_TW`)
- Parameter 1: `0x09530000`
- Parameter 2: zero

With the normal 384 MHz FLEX-1500 reference, the hardware center is
6,993,164.064 Hz. This is consistent with PowerSDR displaying 7.000 MHz while
spur reduction is enabled: the application clears the low 16 DDS tuning-word
bits and corrects the remaining approximately 6.836 kHz in host DSP.

No opcode-1219 `INITIALIZE`, filter, gain, PTT, TX-state, firmware, or EEPROM
command appears in this Start/Stop capture. No endpoint-`0x83` response was
captured.

## Streaming

| Endpoint | USBPcap records | Payload records | Payload bytes |
|---|---:|---:|---:|
| `0x82` radio-to-host | 828 | 411 | 631,104 |
| `0x01` host-to-radio | 764 | 382 | 586,752 |

Endpoint `0x82` contains signed-16-bit little-endian interleaved I/Q, as already
confirmed by the Linux captures.

During receive, endpoint `0x01` is not an all-zero idle TX stream. Interpreted
as signed-16-bit little-endian stereo PCM at 48 kHz, it contains 146,688 frames:

- Every left sample exactly equals its corresponding right sample.
- Sample range is -2,536 through +2,429 on both channels.
- RMS is approximately 923 counts.
- The first 2,304 frames (48 ms) are zero before processed audio begins.

This demonstrates the receive audio loop used by PowerSDR:

```text
radio endpoint 0x82 I/Q
        -> PowerSDR host demodulation/DSP
        -> duplicated mono PCM on endpoint 0x01
        -> FLEX-1500 codec/headphone path
```

Endpoint `0x01` is therefore mode-dependent: during receive it carries speaker
audio, while during transmit it is expected to carry a different signal path.
The dedicated transmit capture must be analyzed before implementing any OUT
traffic.
