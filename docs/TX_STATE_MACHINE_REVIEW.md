<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Transmit state-machine review

This is the offline safety review of the shared ownership, key, modulation,
timeout, and cleanup path before physical microphone PTT is permitted to act on
the transmitter. It does not authorize or perform a radio write.

## Reviewed behavior

- One shared controller owns Tune, physical microphone, future HTTP TX, or
  future SoapySDR TX; two owners cannot be keyed simultaneously.
- Physical PTT is armed only after startup or USB recovery has explicitly
  established unkeyed RX state and completed TX preparation. This permits the
  first real press because endpoint `0x83` does not report an initial release.
- A physical PTT request first fully stops a keyed competing owner. A cleanup
  failure faults the controller and prevents physical-microphone startup.
- Release, maximum-key timeout, daemon shutdown, and USB recovery use the same
  owner stop callback and USB cleanup planner.
- Cleanup attempts transition mute, unkey, receive-frequency restoration,
  endpoint-`0x01` stream cancellation, and PA/amplifier cleanup as appropriate.
  The cleanup executor continues after an individual cleanup action fails.
- The microphone DSP preserves state across arbitrary USB packet boundaries,
  produces mode-correct USB or LSB I/Q, fades in, clips safely, and substitutes
  zero I/Q during an input underrun.

The first integrated physical-microphone test exposed a missing live routing
branch: endpoint-`0x82` microphone packets were still entering only the RX ring,
while TX rendered zero-IQ underruns. The corrected path routes keyed microphone
input exclusively to TX DSP and suppresses it from RX consumers. A subsequent
small-transfer experiment caused deep audio and 590,464 dropped input frames;
it was reverted because the earlier capture proves a 48 kHz microphone rate.
The known-working USB transfer geometry is restored and the FIFO enlarged to
absorb startup and callback bursts. A second integration defect was then found:
the completion callback and packet processor each enqueued the same microphone
packet. Removing that duplicate enqueue corrected the half-speed, deep audio
and eliminated its associated FIFO overflow.

## Defects corrected during review

An endpoint-`0x01` failure used to clear `tune_streaming` before cleanup. That
single Boolean could therefore hide allocated or submitted transfers and omit
stream cancellation. Cleanup now treats any allocated buffer or transfer,
submitted transfer, active transfer count, or streaming flag as a live TX
stream.

The configurable shared maximum-key timer can be shorter than Tune's fixed
60-second hard limit. It could stop the hardware owner while leaving Tune's API
lease marked active. Tune now reconciles its wrapper state whenever the shared
owner has stopped or been preempted, with an offline regression test for the
timeout case.

Daemon shutdown and USB recovery previously invoked the Tune-specific wrapper.
They now shut down the shared transmitter controller first, so the same path
will cover physical-microphone and future network owners.

## Integrated physical-PTT review

The daemon now accepts `physical_mic` only in explicitly enabled TX mode. It
acts only on microphone PTT edges, arms only after successful startup/recovery
cleanup and TX preparation, and snapshots frequency, USB/LSB mode, drive, and
microphone gain before the first TX command. Frequency, mode, drive, and
microphone-gain API changes are rejected while keyed. SoapySDR may remain
connected to a TX-enabled daemon as its RX/control client, but continues to
expose zero TX channels and cannot key the transmitter.

Physical microphone key-down is rejected before ownership is requested if the
frequency is unknown, the mode is not USB/LSB, or the frequency is outside the
configured amateur voice-allocation policy. TX-disabled mode never calls the
physical PTT controller.

## Remaining validation blockers

- The connected callback/profile layer still needs a dedicated injectable
  offline harness. Its component behavior is covered by ownership, protocol,
  API, DSP, audio-stream, and cleanup tests, but the composed daemon callback
  is not yet exercised without USB hardware.
- USB voice has completed repeated live dummy-load validation with natural
  pitch, adequate subjective level, reliable PTT, and RX restoration. A
  calibrated modulation/ALC measurement and live LSB validation remain open.

The transmit-readiness checklist's independent-review item remains open until
these integration blockers are implemented and the resulting complete path is
reviewed again.
