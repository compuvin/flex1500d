# PowerSDR transmit capture at 50% drive

Capture: `pcaps/flex1500-tx-at-drive-50.pcapng`

KB1JDX recorded one transmit/unkey interval at a displayed drive setting of
50%. Analysis was performed offline and did not access the radio.

## Command sequence

The capture contains the same eight-command transition sequence as the earlier
100% transmission:

| Time | Operation |
|---:|---|
| 6.538450 s | transition-mute I²C register `0x25` = `0x00` |
| 6.541850 s | exact TX center `0x25f77777` = 28,474,999.986 Hz |
| 6.543041 s | `SET_TR(1)` |
| 6.744994 s | transition-mute register `0x25` = `0xc0` |
| 8.522997 s | transition-mute register `0x25` = `0x00` |
| 8.524008 s | `SET_TR(0)` |
| 8.553231 s | restore RX center `0x25f60000` |
| 8.728310 s | transition-mute register `0x25` = `0xc0` |

There is no drive-setting command. This confirms that PowerSDR applies drive
to the TX samples generated on endpoint `0x01`.

## Sample-amplitude comparison

Steady TX windows were selected after keying transitions and before unkeying
transitions. Means were removed before calculating RMS.

| Capture | Drive | I RMS | Q RMS |
|---|---:|---:|---:|
| Full lifecycle | 100% | 19.5096 | 19.6279 |
| This capture | 50% | 9.8961 | 9.9546 |

The 50%/100% ratios are approximately 0.5073 for I and 0.5072 for Q. Within the
variation of the transmitted source, this is strong evidence that the drive
percentage linearly scales host-generated TX I/Q amplitude.

This does **not** establish RF output power at 50%. I/Q sample amplitude is a
voltage-like quantity; RF power also depends on PA gain, calibration,
compression, load, and other hardware behavior. Output power must be measured
into an appropriate dummy load with suitable RF measurement equipment.

## Implementation consequence

A future Linux TX modulator would apply its drive factor before converting and
clamping complex samples to signed 16-bit endpoint-`0x01` values. It would also
need an independently established maximum safe full-scale amplitude, controlled
ramping, clipping accounting, underrun-safe output, and measured RF validation.
This finding does not authorize a live TX implementation or test.
