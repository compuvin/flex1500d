<!-- SPDX-License-Identifier: GPL-3.0-only -->

# PowerSDR software feature gaps

This checklist records useful operator features found in PowerSDR that are not
yet fully implemented or exposed by `flex1500d`. It complements the
[radio-capability backlog](UNIMPLEMENTED_FEATURES.md), which covers FLEX-1500
hardware, connectors, USB commands, and protocol research.

PowerSDR combines radio control, DSP, station integration, and a desktop user
interface. Reproducing every screen is not a project goal. Features should be
placed in the daemon/API only when they represent shared radio state, safety
policy, persistent station configuration, or a generally useful stream. Visual
display and operator-workflow features can instead be provided by browser,
SoapySDR, or other client applications.

An item should be checked only after its intended ownership behavior is clear,
the implementation and API are documented, and relevant offline and live tests
have passed.

Checklist scope labels indicate where an item would most naturally belong:
`(daemon)` means `flex1500d` and its API, `(client)` means a possible future
companion bridge or another compatible client application, and
`(daemon+client)` means coordinated work on both sides. A client label records
an architectural boundary only; it is not a promise that this project will
develop or distribute that client software.

## Receiver DSP and metering

- [ ] Add selectable receive AGC modes such as off, fast, medium, slow, and
  long, with sensible mode-specific defaults. (client)
  Threshold, decay, hang, and maximum gain should initially remain tested
  internal parameters of those modes rather than ordinary exposed controls.
  Advanced adjustment can be reconsidered if a demonstrated use requires it.
- [ ] Add calibrated or characterized signal-strength reporting suitable for
  an S-meter and squelch decisions. (daemon+client)
- [ ] Add impulse-noise blanking with controls appropriate to the FLEX-1500's
  48 kHz I/Q stream. (client)
- [ ] Add filter shift or passband tuning in addition to the implemented mode
  and bandwidth controls. (client)

Per-listener audio DSP should remain independent when practical. One listener's
noise reduction, notch filters, volume, or mute selection should not change
another listener's audio or retune the physical radio.

## Frequency and operating state

- [ ] Add RIT so the controlling station can offset receive without changing
  the transmit frequency. (daemon+client)
- [ ] Add XIT so the controlling station can offset transmit without changing
  the receive frequency. (daemon+client)
- [ ] Add reviewed split-frequency operation with distinct RX and TX
  frequencies, ownership enforcement, amateur-band validation, and safe state
  restoration. (daemon+client)

Any operation that changes the FLEX-1500's physical center frequency, RF
filter, or transmit frequency remains restricted to the station owner.
Secondary listeners may tune only within the I/Q window made available by the
owner.

## Transmit audio and operating aids

- [ ] Add adjustable transmit filter low and high edges with safe defaults for
  each supported emission mode. (daemon+client)

These are software features and do not imply that the radio supplies forward
power, reflected power, SWR, PA temperature, or hardware ALC telemetry.

## Memories and persistent profiles

Memories, band stacks, favorites, operating profiles, and their import/export
are intentionally outside this project. Neither `flex1500d` nor the companion
bridge stores or manages them. Established applications may provide those
features and recall settings through ordinary, ownership-aware rig control.

The daemon persists only service configuration and safety policy. Any external
application request that physically retunes the radio remains subject to
station ownership.

## Recording, playback, and automation

- [x] Document a recommended client-side method for recording demodulated
  audio from the API. (See the
  [companion client design](COMPANION_CLIENT_DESIGN.md).) (client)
- [x] Document a recommended client-side method for recording and replaying raw
  I/Q, including sample format and metadata needed for correct tuning. (See the
  [raw I/Q interoperability guide](RAW_IQ_INTEROPERABILITY.md).)
  (daemon+client)

## Integration with established station software

- [ ] Add or document a Hamlib-compatible rig-control bridge for logging,
  contest, and digital-mode applications. (client)
- [ ] Evaluate whether a limited CAT compatibility layer would materially
  improve support for software that cannot use Hamlib, SoapySDR, or the native
  HTTP API. (client)
- [ ] Document PipeWire or PulseAudio integration as the Linux equivalent of
  PowerSDR's Virtual Audio Cable workflow. (client)
- [ ] Document representative digital-mode operation with external software;
  modulation, decoding, logging, and message automation remain client duties.
  (daemon+client)
- [ ] Test additional established SDR applications for receive, transmit, and
  station-ownership interoperability. (daemon+client)

## Features intentionally left to clients

These are useful PowerSDR capabilities, but they do not need to become daemon
API features unless a later use case demonstrates otherwise:

- [x] Treat panadapter, waterfall, spectrum, and FFT rendering as client-side
  views derived from the existing I/Q stream.
- [x] Treat digital-mode decoding, logging, DX-cluster display, spotting, and
  desktop layout or skinning as client-application responsibilities.
- [x] Do not expose automatic antenna-tuner controls: the FLEX-1500 has no
  internal antenna tuner, and the implemented Tune carrier is not an ATU.
- [x] Do not imply independent dual-receiver operation: multiple clients share
  the radio's single physical center frequency and 48 kHz I/Q window.

## Source and related documentation

This backlog was developed by comparing `flex1500d` with the open-source
[PowerSDR KE9NS codebase](https://github.com/ke9ns/PowerSDR-KE9NS-v2.8.0),
including its DSP, CAT, audio/VAC, memory, band-stack, scanning, and recording
components. Features present in that codebase are not automatically applicable
to the FLEX-1500; each item still requires model-specific review.

- [Identified FLEX-1500 features not yet implemented](UNIMPLEMENTED_FEATURES.md)
- [Receive DSP](RECEIVE_DSP.md)
- [Network API](NETWORK_API.md)
- [API and SoapySDR authentication design](API_AUTHENTICATION_DESIGN.md)
- [Transmit enablement checklist](TRANSMIT_ENABLEMENT_CHECKLIST.md)
