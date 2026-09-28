# Companion Client Testing

This document records practical validation of the experimental
`flex1500-client` companion bridge. It supplements the design and operating
documentation; it is not a claim that every application, network, mode, or
station configuration has been validated.

## September 27, 2026 validation

The client was tested from an Ubuntu workstation against `flex1500d` running
on a Raspberry Pi 3 attached by USB to the FLEX-1500. The final daemon and
client source revision was `2321dfae` on the `dev` branch.

### Receive and local integration

- The client connected to the daemon across the local network and acquired the
  exclusive station-control lease.
- Its `FLEX-1500 RX` PipeWire source appeared as a normal local audio input.
- WWV was received and heard through that PipeWire source.
- WSJT-X connected through the client's loopback Hamlib NET rigctl service.
- WSJT-X successfully queried and changed frequency and mode.
- Correcting complex-sample orientation in the client restored the expected
  tuning direction and allowed WSJT-X to receive numerous 20-meter digital
  signals.
- The first-run state default of 28.475 MHz USB was applied successfully.
  After tuning to 20 meters and stopping the client cleanly, a daemon/radio
  restart followed by a client reconnect restored the last frequency and mode.

### Transmit control and audio

- WSJT-X uses Hamlib PTT value `3`; the client now maps Hamlib's supported
  PTT-on variants to the same owned audio-transmit state machine.
- Early testing exposed a daemon policy that unkeyed after one second without
  a new audio upload. This was removed because asserted PTT and the TX control
  lease—not audio-packet arrival—govern whether the transmitter remains keyed.
  Explicit unkey, disconnect, lease expiry, maximum-key timeout, shutdown, and
  hardware-error cleanup remain active.
- Repeated `POST /v1/tx/audio` uploads produced bursty delivery. The client was
  changed to the daemon's persistent `CONNECT /v1/tx/stream` PCM transport,
  with partial-write handling, independent lease renewal, and bounded 48 kHz
  pacing.
- A race between prebuffer delivery and PTT readiness produced an initial
  `409 tx_not_ready`. The client now performs a bounded retry only for that
  readiness response.
- Monitoring showed continuous PipeWire audio but downstream transmitter
  underruns. The daemon's general live-audio path was found to pre-submit
  approximately 1.07 seconds of large USB transfers. Live microphone/network
  audio now uses a smaller approximately 213 ms USB pipeline aligned with the
  existing 200 ms muted transition. The separate capture-matched 5 W Tune
  implementation retains its original transfer geometry.

### Final dummy-load results

With the transmitter connected to a suitable dummy load:

- WSJT-X Tune produced clean, continuous received audio.
- A simultaneous local PipeWire monitor capture contained no gaps of 5 ms or
  longer after the Tune audio began.
- During active Tune, the daemon reported zero TX underruns, a bounded queue,
  and no watchdog, USB, cleanup, clipping, or limiting errors.
- Timed WSJT-X FT8 transmissions produced clean, recognizable FT8 audio.
- A 30-second local monitor capture contained continuous audio throughout its
  active FT8 section, with no gaps of 5 ms or longer.
- FT8 queue depth remained bounded; the observed peak was 7,496 frames, or
  approximately 156 ms.
- No watchdog stops, cleanup failures, clipped frames, limited frames, or
  discarded graceful-drain frames occurred during the final FT8 test.

The daemon's cumulative `tx_underruns` counter can include harmless zero-filled
PTT lead and tail intervals. It increased after audio stopped even when the
active transmission was clean, so it must not currently be interpreted as a
direct count of audible defects without correlating it with timing and queue
state.

### Live 20-meter result

After the dummy-load checks, KB1JDX performed a live 20-meter FT8 transmission.
The operator reported that the Reverse Beacon Network showed reception by
WA7LNW at -2 dB. This validates the complete tested path:

```text
WSJT-X
  -> local Hamlib and PipeWire interfaces
  -> flex1500-client
  -> persistent native flex1500d network API
  -> Raspberry Pi daemon
  -> USB
  -> FLEX-1500
  -> 20-meter RF transmission
```

This single report demonstrates successful end-to-end operation but does not
replace broader interoperability, spectral-quality, band, mode, or long-term
reliability testing.

### First completed QSO

Later on September 27, 2026, KB1JDX completed the project's first two-way
contact using both `flex1500d` and `flex1500-client`. The QSO was with KC9YTT
on 40-meter FT8. KC9YTT received KB1JDX at -13 dB, and KB1JDX received KC9YTT
at -11 dB. The FLEX-1500 was operating at its 5 W maximum output.

This contact exercised the complete bidirectional station path during normal
operation: network IQ reception, client-side demodulated PipeWire audio,
Hamlib frequency/mode/PTT control, PipeWire-generated FT8 transmit audio, the
persistent PCM network stream, daemon modulation and USB scheduling, RF
transmission, normal unkey, and return to receive. It is the first confirmed
QSO made through the combined daemon and companion client.

## Offline coverage

Revision `2321dfae` passed all 57 repository tests. The companion lifecycle
test covers station ownership, state restoration, Hamlib frequency/mode/PTT,
the persistent PCM connection and prebuffer, bounded `tx_not_ready` retry,
normal PTT stop, session release, and clean shutdown against a mock daemon.

Offline tests do not replace live checks of RF output, audio quality, timing,
USB behavior, or external safety equipment.
