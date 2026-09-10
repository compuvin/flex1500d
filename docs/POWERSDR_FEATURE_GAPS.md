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

## Receiver DSP and metering

- [ ] Add selectable receive AGC modes such as off, fast, medium, slow, and
  long, with sensible mode-specific defaults.
- [ ] Expose useful AGC parameters such as threshold, decay, hang, and maximum
  gain without making ordinary operation require manual DSP tuning.
- [ ] Add calibrated or characterized signal-strength reporting suitable for
  an S-meter and squelch decisions.
- [ ] Add impulse-noise blanking with controls appropriate to the FLEX-1500's
  48 kHz I/Q stream.
- [ ] Add optional DSP noise reduction and automatic-notch filtering.
- [ ] Add one or more adjustable manual notch filters.
- [ ] Add filter shift or passband tuning in addition to the implemented mode
  and bandwidth controls.
- [ ] Add optional receive equalization and per-client audio gain, mute, and
  balance controls where those are not better left entirely to the client.

Per-listener audio DSP should remain independent when practical. One listener's
noise reduction, notch filters, volume, or mute selection should not change
another listener's audio or retune the physical radio.

## Frequency and operating state

- [ ] Add RIT so the controlling station can offset receive without changing
  the transmit frequency.
- [ ] Add XIT so the controlling station can offset transmit without changing
  the receive frequency.
- [ ] Add reviewed split-frequency operation with distinct RX and TX
  frequencies, ownership enforcement, amateur-band validation, and safe state
  restoration.
- [ ] Define a VFO A/B-style API model only if it makes common clients easier
  to integrate; avoid duplicating desktop UI state without a practical use.
- [ ] Add frequency step and lock semantics useful to hardware controllers and
  rig-control clients.

Any operation that changes the FLEX-1500's physical center frequency, RF
filter, or transmit frequency remains restricted to the station owner.
Secondary listeners may tune only within the I/Q window made available by the
owner.

## Transmit audio and operating aids

- [ ] Add adjustable transmit filter low and high edges with safe defaults for
  each supported emission mode.
- [ ] Add a transmit equalizer or documented external-processing path.
- [ ] Expand speech processing beyond the current on/off compressor control
  where additional parameters have a demonstrated operator benefit.
- [ ] Add VOX with threshold, delay, anti-VOX behavior, ownership integration,
  the configured maximum-key timer, and guaranteed unkey cleanup.
- [ ] Add named microphone and transmit profiles without allowing a profile to
  bypass drive limits, band validation, or TX interlocks.
- [ ] Define local transmit-monitor audio without feeding transmitted audio
  back into the normal shared receive stream.

These are software features and do not imply that the radio supplies forward
power, reflected power, SWR, PA temperature, or hardware ALC telemetry.

## Memories and persistent profiles

- [ ] Add named memories containing frequency, mode, filter, gain, squelch, and
  other useful receive state.
- [ ] Add band-stack memories with multiple commonly used settings per amateur
  band.
- [ ] Add import, export, and backup for memories and profiles using a stable,
  documented format.
- [ ] Define which settings are global station state, owner state, or
  per-listener preferences before persisting them.

Recalling a memory that physically retunes the radio must require station
ownership. Merely browsing memories should remain read-only.

## Recording, playback, and automation

- [ ] Document a recommended client-side method for recording demodulated
  audio from the API.
- [ ] Document a recommended client-side method for recording and replaying raw
  I/Q, including sample format and metadata needed for correct tuning.
- [ ] Decide whether unattended server-side audio or I/Q recording is useful
  enough to justify storage management and an additional API.
- [ ] Design scanning as an API client or automation service using memories,
  squelch state, and the station-control lease rather than embedding UI policy
  in the USB layer.
- [ ] Consider scheduled recording only after ownership, storage limits, and
  behavior across radio or client disconnects are defined.

## Integration with established station software

- [ ] Add or document a Hamlib-compatible rig-control bridge for logging,
  contest, and digital-mode applications.
- [ ] Evaluate whether a limited CAT compatibility layer would materially
  improve support for software that cannot use Hamlib, SoapySDR, or the native
  HTTP API.
- [ ] Document PipeWire or PulseAudio integration as the Linux equivalent of
  PowerSDR's Virtual Audio Cable workflow.
- [ ] Document representative digital-mode operation with external software;
  modulation, decoding, logging, and message automation remain client duties.
- [ ] Test additional established SDR applications for receive, transmit, and
  station-ownership interoperability.

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
