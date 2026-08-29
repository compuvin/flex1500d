# PowerSDR transmit capture

Capture: `pcaps/flex1500-transmit.pcapng`

This is offline protocol analysis, not authorization to implement or execute
transmission. The Linux project continues to expose no PTT, MOX, PA-bias,
transmit-enable, or endpoint-`0x01` live path.

## Command sequence

The capture contains one transmit interval and eight commands:

| Time | Operation |
|---:|---|
| 4.355160 s | I²C address `0x30`, register `0x25`, value `0x00` |
| 4.358771 s | `SET_RX1_FREQ_TW(0x25f77777)` |
| 4.359545 s | `SET_TR(1)` |
| 4.513843 s | I²C address `0x30`, register `0x25`, value `0xc0` |
| 6.039417 s | I²C address `0x30`, register `0x25`, value `0x00` |
| 6.040154 s | `SET_TR(0)` |
| 6.095360 s | `SET_RX1_FREQ_TW(0x25f60000)` |
| 6.194852 s | I²C address `0x30`, register `0x25`, value `0xc0` |

Opcode 1020 is the wrapper's `I2C_WRITE_2_VALUE`. PowerSDR source associates
these register-`0x25` writes with FLEX-1500 transition muting. The behavior
supports that interpretation: value `0x00` is applied immediately before key
and unkey, and `0xc0` restores the path approximately 154–155 ms later.

## Tuning behavior

For transmit, PowerSDR replaces the receive spur-reduced word with exact DDS
word `0x25f77777`, corresponding to 28,474,999.986 Hz on the normal 384 MHz
reference. This removes the receive-side host DSP/VFO correction from the RF
transmit center.

After unkeying, it restores receive word `0x25f60000`, corresponding to
28,470,703.132 Hz. Host DSP then supplies the receive offset.

The radio uses opcode 1276 (`SET_TR`) to enter and leave transmit. No
opcode-1292 `SET_MOX` or opcode-1349 `SET_TX_FREQ_TW` appears; the shared RX1
DDS command is used around the TR transition.

## Endpoint `0x01` changes meaning

Before transmit, every signed-16-bit channel pair is equal: endpoint `0x01`
carries duplicated mono receive audio to the radio's codec/headphone path.

The first clearly non-mono record occurs at 4.387493 seconds, approximately
27.948 ms after `SET_TR(1)`. During the steady transmit interval:

- I and Q are signed 16-bit little-endian.
- I/Q correlation is approximately -0.00046.
- Only 3.67% of I/Q pairs are accidentally equal.
- I RMS is approximately 10.57 counts and Q RMS is 11.38 counts in this
  particular capture.

This is low-level modulated TX I/Q rather than speaker audio. The last clearly
non-mono record is at 6.062008 seconds, approximately 21.854 ms after
`SET_TR(0)`, due to queued USB buffers. Duplicated receive audio then returns.

USBPcap records aggregate multiple 192-byte isochronous packets into 1,536,
3,072, or 6,144 captured bytes. The underlying radio format remains 48 frames,
192 bytes, and 1 ms per packet.

## Endpoint `0x82`

Endpoint `0x82` remains active without interruption throughout transmit: 1,633
payload records of 1,536 bytes each. The capture does not establish whether
these samples are receiver input, transmitter feedback, leakage, or a routed
monitor signal.

## Commands absent from this interval

There is no `INITIALIZE`, `SET_MOX`, PA-filter change, PA-bias change,
amplifier-output change, antenna change, firmware operation, or EEPROM access.
Those states were established before this capture. Their absence must not be
interpreted as evidence that a fresh Linux process can transmit safely using
only `SET_TR(1)`.

## Required work before Linux TX

A future TX subsystem remains absent until at least these are understood:

1. Region and allowed-frequency policy.
2. Correct PA-filter selection before keying and return to filter 0 during full
   shutdown if the daemon changed it.
3. Antenna, amplifier-output, PA-bias, and drive-calibration state.
4. TX I/Q scaling, ramping, underrun behavior, and zero-safe buffers.
5. Exact codec transition-mute meaning and timing.
6. A fail-safe unkey path prioritizing `SET_TR(0)` after USB/network failures.
7. Hardware PTT observation and explicit local authorization independent of a
   network client.
8. Proof that ordinary startup, receive controls, disconnects, and crashes
   cannot enter TX.

No live TX code or command-line option should be added from this capture alone.
The broader startup and shutdown state is analyzed separately in
`PCAP_FULL_TX_SEQUENCE.md`.
