<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Transmit audio signal chain

This document describes the daemon-generated USB and LSB audio path. It records
implemented behavior and offline test coverage; it does not claim that every
mode, radio, band, or client has completed live RF validation.

## Audio-to-I/Q processing

Mono signed 16-bit, little-endian audio at 48 ksample/s passes through these
stages:

1. Normalize the input and record input level statistics.
2. Apply the configured microphone gain and optional compressor.
3. Remove DC with a continuous one-pole high-pass stage.
4. Apply the default 300–3000 Hz, 129-tap FIR voice passband.
5. Generate the analytic signal and select the USB or LSB orientation.
6. Apply the requested drive magnitude and a non-disableable final limiter.
7. Apply a 10 ms raised-cosine envelope at key-up and normal graceful key-down.
8. Convert the result to interleaved signed 16-bit I/Q for the USB scheduler.

Filter, Hilbert-transform, DC-removal, and envelope state persists across input
and output chunk boundaries. Network packet or USB-transfer boundaries
therefore do not restart the DSP or introduce a deliberate phase discontinuity.
The default passband is explicit and validated when the DSP is initialized;
the accepted implementation range is 50–12000 Hz with at least 100 Hz between
the low and high edges. This is internal groundwork, not yet an adjustable TX
filter API.

## Start and stop behavior

The first transmitted sample is zero. The following 480 frames ramp to full
amplitude with a raised-cosine envelope. On a normal PTT stop, the daemon stops
accepting new audio and applies the inverse envelope to the last 480 queued
audio frames while the existing bounded graceful-drain logic runs. The final
queued sample is zero before the radio is unkeyed.

Watchdog, disconnect, USB error, shutdown, preemption, and emergency-stop paths
continue to prioritize immediate unkey over waveform shaping. They do not wait
for this normal-stop envelope.

## Raw I/Q boundary

Raw `cs16le` I/Q does not pass through the audio DC blocker, voice filter,
sideband generator, compressor, or graceful audio envelope. It is constrained
by the selected-drive limiter, but the client remains responsible for spectral
purity, bandwidth, continuity, modulation, and legality. See
[raw I/Q client guidance](RAW_IQ_INTEROPERABILITY.md).

## Offline verification

The `tx-dsp` and `tx-audio-stream` tests verify:

- USB and LSB spectral orientation and opposite-sideband rejection;
- strong DC rejection and rejection below and above the voice passband;
- passband-bound validation;
- final magnitude limiting without signed-integer clipping;
- byte-identical output when the same audio is divided at different chunk
  boundaries;
- exact zero at the start and end of the normal envelope; and
- a bounded maximum adjacent-sample step in the envelope test.

These tests can run without a radio. Dedicated live dummy-load validation is
still required before treating newly changed RF behavior as established.
