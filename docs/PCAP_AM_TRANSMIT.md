<!-- SPDX-License-Identifier: GPL-3.0-only -->

# PowerSDR AM transmit capture

Capture: `pcaps/flex1500-am-transmit.pcapng`

KB1JDX recorded a PowerSDR AM voice transmission at a displayed frequency of
28.475 MHz. This analysis is offline and does not itself authorize a live radio
operation.

## Key and unkey sequence

PowerSDR used the established transition-mute and `SET_TR` sequence. The
frequency commands distinguish this AM transmission from the earlier SSB
captures:

| Relative time | Operation |
| ---: | --- |
| 19.497557 s | transition mute register `0x25` = `0x00` |
| 19.504109 s | `SET_RX1_FREQ_TW(0x25f3b416)` = 28,463,974.989 Hz |
| 19.505359 s | `SET_TR(1)` |
| 19.708631 s | transition mute register `0x25` = `0xc0` |
| 24.426734 s | transition mute register `0x25` = `0x00` |
| 24.428321 s | `SET_TR(0)` |
| 24.524413 s | restore receive word `0x25f60000` = 28,470,703.132 Hz |
| 24.631773 s | transition mute register `0x25` = `0xc0` |

The keyed hardware center is approximately 11,025.011 Hz below the requested
28.475 MHz carrier.

## Endpoint `0x01` AM waveform

Four seconds of steady keyed endpoint-`0x01` data contain 192,000 complex
signed-16-bit samples. Spectral analysis at 48 kHz finds:

- a dominant complex carrier at exactly −11,025 Hz;
- symmetric voice sidebands around that carrier;
- essentially no carrier energy at complex DC;
- mean complex magnitude approximately 10,639 counts and maximum magnitude
  approximately 11,759 counts in the analyzed interval; and
- nearly identical I and Q RMS values of approximately 7,551 counts.

The −11,025 Hz complex carrier and the hardware center 11,025 Hz below the dial
frequency cancel at RF, placing the transmitted carrier at approximately
28,475,000 Hz. PowerSDR therefore deliberately avoids sending an AM carrier at
complex DC through the FLEX-1500 transmit path.

## Implementation consequence

The daemon's initial AM implementation instead places its carrier at complex
DC and uses the exact dial frequency as the hardware center. Live tests found
poor AM audio from both that path and an SDR Oxide raw-I/Q AM signal, while
USB/LSB reception remained intelligible. The capture supplies direct evidence
that a PowerSDR-compatible AM path needs coordinated digital translation and
hardware-frequency compensation.

Daemon-generated AM can use the captured −11,025 Hz complex translation and a
hardware word 11,025 Hz below the requested carrier. Applying the same scheme
to arbitrary Soapy/raw-IQ input requires a separate interface decision because
the daemon does not know the client's modulation mode and translating a full
48 kHz raw-IQ span can wrap spectrum across the Nyquist boundary.

The daemon subsequently implemented this capture-matched translation. Live AM
voice testing changed the result from poor, SSB-like audio to dramatically
improved AM audio, confirming that carrier placement was the principal defect
in the original daemon-generated path.

A temporary, untracked SoapySDR diagnostic then transmitted the same fixed
1 kHz AM tone twice at 28.475 MHz and 50% drive. With an ordinary DC-centered
carrier, the tone was present but sounded very poor. With a −11.025 kHz
complex carrier and equal hardware-frequency compensation, the tone sounded
substantially better. This isolates the behavior from SDR Oxide and confirms
that Soapy raw-I/Q AM needs the same coordinated translation. The diagnostic
source and binary were removed after the test and were never added to the
project or release artifacts.

The same correction was then made automatic for raw-I/Q clients when their
prebuffer contains a dominant coherent DC carrier. Four live SDR Oxide AM
transmissions each measured a `1.000` DC-carrier ratio, enabled translation,
and produced good received audio at the requested 28.475 MHz frequency. All
four keyed and unkeyed cleanly with zero clipped or limited frames.
An SDR Oxide USB transmission provided a negative control: its prebuffer
measured a DC-carrier ratio of `0.011`, so automatic translation correctly
remained disabled and the ordinary USB raw-I/Q path was unchanged.
An SDR Oxide CW transmission likewise measured `0.000`, remained untranslated,
and produced clean received CW.
