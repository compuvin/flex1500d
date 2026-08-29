# Full PowerSDR transmit lifecycle capture

Capture: `pcaps/flex1500-full-tx-sequence.pcapng`

KB1JDX recorded a PowerSDR program start, radio start, a displayed-frequency
setting of exactly 28.475 MHz, drive adjustment from 49% back to 100%, one
transmit/unkey interval, receive-audio mute/unmute, radio stop, and program
close. The FLEX-1500 is rated at 5 W, so the selected 100% drive setting
nominally represents 5 W. This document is offline analysis, not authorization
for Linux transmission.

## Startup and receive setup

PowerSDR performs identity, revision, region, status, and EEPROM reads. The
state-changing startup sequence includes:

- `SET_RX1_ANT(0)` and `SET_TX_ANT(0)`
- an initial `SET_RX1_FILTER(6)` and `SET_PA_FILTER(5)`
- `SET_AMP_TX1(0)`, followed later by `SET_AMP_TX1(1)`
- `SET_XREF(0)`
- a second antenna setup followed by `SET_RX1_FILTER(2)` and
  `SET_PA_FILTER(2)` for the selected band
- `SET_TRX_PREAMP(3)`
- `SET_RX1_FREQ_TW(0x25f60000)` when streaming starts

No opcode-1219 `INITIALIZE` appears. This means the Windows lifecycle observed
here cannot establish what a newly attached, uninitialized device requires.

The exact 28.475 MHz dial value confirms the receive architecture. The receive
hardware center word `0x25f60000` is 28,470,703.132 Hz; PowerSDR supplies a
receive-side DSP offset of approximately +4,296.868 Hz.

## Drive control observation

Changing drive from 49% back to 100% produced no endpoint-`0x04` command.
Because the change occurred before transmit I/Q was flowing, this capture
cannot measure the ratio between the two settings. The evidence indicates that
PowerSDR implements drive by scaling the TX I/Q samples it generates for
endpoint `0x01`, rather than by setting a persistent radio register.

A separate controlled capture transmitting the same fixed tone once at each
drive setting would be needed to derive sample-amplitude scaling. RF output
power cannot safely be inferred from I/Q amplitude alone; it must be checked
into a dummy load with suitable measurement equipment.

That comparison is now available in `PCAP_TX_DRIVE_50.md`: the 50% capture has
approximately 0.507 times the I and Q RMS of this 100% capture, confirming
host-side linear TX sample-amplitude scaling.

## Key and unkey sequence

The transmit interval repeats the sequence from `flex1500-transmit.pcapng`:

| Time | Operation |
|---:|---|
| 36.408766 s | transition-mute I²C register `0x25` = `0x00` |
| 36.412918 s | exact TX center `0x25f77777` = 28,474,999.986 Hz |
| 36.413612 s | `SET_TR(1)` |
| 36.616200 s | transition-mute register `0x25` = `0xc0` |
| 38.175405 s | transition-mute register `0x25` = `0x00` |
| 38.176726 s | `SET_TR(0)` |
| 38.248597 s | restore RX center `0x25f60000` |
| 38.381772 s | transition-mute register `0x25` = `0xc0` |

Endpoint `0x01` changes from duplicated mono receive audio to non-mono TX I/Q
during this interval and returns to duplicated audio after unkeying.

No PA-filter, PA-bias, antenna, or `SET_AMP_TX1` operation occurs at key time;
those states are established during startup. A TX implementation therefore
must treat startup configuration and keying as one safety lifecycle, not copy
only the eight commands above.

## Receive mute and unmute

Mute/unmute produces no endpoint-`0x04` command. Instead, PowerSDR continues
endpoint-`0x01` transfers and substitutes duplicated all-zero PCM for roughly
41.2 through 43.1 seconds. Normal duplicated receive audio then returns. Thus
the mute control is host-side, but it changes the audio stream delivered to the
FLEX-1500 codec/headphone path.

## Shutdown

After streaming stops, PowerSDR eventually sends:

- `SET_TR(0)` at 124.516710 seconds
- `SET_PA_FILTER(0)` at 124.518636 seconds

This confirms that orderly radio shutdown explicitly unkeys and returns the PA
filter to its idle/bypass state. No explicit `SET_AMP_TX1(0)` or radio power-off
command appears in the captured shutdown interval.

## Remaining TX questions

This capture substantially improves the lifecycle model, but it does not yet
establish safe TX I/Q scaling, ramping, underrun behavior, PA-bias behavior,
emergency unkey after USB/process failure, or hardware-PTT behavior. Linux TX
must remain absent until these are addressed and KB1JDX separately approves any
live radio operation.
