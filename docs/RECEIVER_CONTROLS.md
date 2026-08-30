<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Receiver controls research

This document separates controls supported by capture/source evidence from
ideas that still require controlled hardware testing. None of the researched
controls below are exposed by the daemon or network API yet.

| Control | Current finding | Confidence |
| --- | --- | --- |
| Receive gain/attenuation | `SET_TRX_PREAMP` (1247), values 0 through 4 select -10, 0, +10, +20, and +30 dB | High |
| Receive signal path | `SET_RX1_ANT` (1278): 0 PA/main antenna, 1 XVRX, 2 XVTX/COM, 3 BITE | High mapping; only PA observed in captures |
| RF bandwidth | `SET_RX1_FILTER` (1257), values 0 through 11, selects the frequency-dependent RF preselector | Confirmed |
| Demodulator bandwidth | Implemented in host DSP; it is not the RF preselector setting | Confirmed architecture |
| User audio mute | PowerSDR's captured mute/unmute produced no radio command, so it appears to be host-side | High for the captured action |
| Receive switching | `SET_TR(0)` selects receive during T/R transitions; it is not needed as a routine audio-mute control | Confirmed, but TX-related |

## Gain and attenuation

The FLEX-1500 exposes these as one control even though the lowest setting is
attenuation. PowerSDR names the operation `SET_TRX_PREAMP`; its values are:

- 0: -10 dB
- 1: 0 dB
- 2: +10 dB
- 3: +20 dB
- 4: +30 dB

The existing captures contain value 3 (+20 dB). The protocol library now has
an offline, range-checked packet builder for all five values. It is not called
by the USB receiver, daemon, API, or SoapySDR adapter.

### Controlled gain validation

On 2026-08-29, KB1JDX approved a receive-only comparison at 10 MHz. Two
one-second captures each contained 47,616 valid complex samples with no USB
packet errors. After removing the I/Q DC means, the complex RMS amplitudes
were:

- +20 dB setting: 601.471 counts
- 0 dB setting: 43.083 counts

The observed amplitude ratio was about 13.96, or 22.9 dB. Over separate
shortwave captures the received signal and noise can vary, so this is not a
calibrated gain measurement. It nevertheless confirms that opcode 1247 value
3 produces substantially more receive gain than value 1. The radio was
restored to +20 dB after the test.

## Receive signal paths

KB1JDX confirmed the radio has one ordinary antenna connector. The normal
PA/main-antenna path is value 0. Values 1 and 2 select the radio's specialized
XVRX and XVTX/COM transverter connections; they are not additional general-
purpose antenna inputs. The separate 10 MHz reference input is unrelated to
this selector.

Value 3 is PowerSDR's internal BITE path. The normal packet builder deliberately
rejects BITE. Any eventual user-facing control should be described as receive
signal-path or transverter routing, not as a generic antenna selector. It
should default to and ordinarily remain on PA/main antenna. No transverter-path
test should be attempted until KB1JDX has confirmed the appropriate physical
connections and test setup.

## Bandwidth

`SET_RX1_FILTER` chooses one of twelve broad RF preselector ranges as the radio
is tuned. It is not an adjustable SSB, AM, FM, or CW bandwidth. Those narrower
passbands belong in the daemon's host DSP and may be changed without another
radio-control opcode.

The PowerSDR opcode list also names manual/bypass RX-filter operations, but the
current captures do not establish their parameters or a need for them. They
must not be sent based on their names alone.

## Mute and receive switching

The full captured sequence included PowerSDR audio mute and unmute, but no
corresponding endpoint-0x04 command appeared. The safest current design is
therefore host-side audio mute. The codec mute writes already documented in
the TX research are specifically part of transmit/receive transitions and
must not be repurposed as a general receiver control without evidence.

## Proposed hardware validation

The initial +20 dB versus 0 dB gain test is complete. Testing every gain step
or calibrating their exact gain remains optional future work and would require
new approval. Transverter-path testing is not currently planned. Unknown mute
or manual-filter opcodes are not ready for a hardware test.
