<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Transmit enablement engineering checklist

This is an implementation checklist, not an addition to the README project
goals. It tracks the technical and safety work needed for general FLEX-1500
transmit support.

## Hardware state and frequency tracking

- [x] Decode the PA-filter command and map every supported frequency range.
- [x] Reproduce PowerSDR's capture-matched 5 W Tune carrier.
- [x] Add an exclusive Tune lease, renewal watchdog, hard time limit, and
  fail-safe unkey/RX restoration.
- [x] Add a distinct transmit-enabled daemon startup mode.
- [x] Enable the TX amplifier path in transmit-enabled mode.
- [x] Select the PA filter after the first known frequency and update it on
  every later frequency change.
- [x] Reassert the correct PA filter before Tune keys the radio.
- [x] Live-test PA-filter tracking without transmitting at 1.900, 3.800,
  5.300, 7.200, 14.200, 21.250, 28.475, and 50.100 MHz; filters 7 through 1
  matched the PowerSDR map with zero radio-command or USB errors.
- [x] Confirm shutdown and USB-recovery behavior for every partially prepared
  TX state. The state/cleanup matrix and verification evidence are recorded in
  [TX_SHUTDOWN_RECOVERY.md](TX_SHUTDOWN_RECOVERY.md).

## Ownership and state machine

- [ ] Design an exclusive controlling-client model, maximum-key timer,
  disconnect unkey, startup inhibit, and explicit TX-enable interlocks. This
  must cover physical microphone PTT as well as HTTP and future Soapy owners.
- [x] Generalize the Tune lease into one exclusive transmitter owner shared by
  physical PTT, HTTP TX, and future SoapySDR TX.
- [x] Define RX, preparing, transmitting, unkeying, recovering, and faulted
  states with permitted transitions. The offline-tested ownership policy is
  documented in
  [TX_OWNERSHIP_STATE_MACHINE.md](TX_OWNERSHIP_STATE_MACHINE.md).
- [ ] Reject frequency, mode, drive, and filter changes that are unsafe while
  keyed; define which changes may be staged for the next transmission.
- [ ] Ensure every error, disconnect, signal, timeout, and shutdown path
  attempts transition mute, `SET_TR(0)`, receive-frequency restoration, and
  safe PA state.
- [ ] Independently review the complete key, modulate, and guaranteed-unkey
  state machine before connecting physical microphone PTT or general transmit
  control to the daemon or API.
- [x] Add counters and diagnostics for TX starts, stops, underruns, rejected
  ownership requests, watchdog stops, and cleanup failures.
- [x] Add a non-disableable, configurable maximum-transmit timer shared by all
  TX owners: 180-second default, 30–1800-second API range, and reject changes
  while keyed.

## Physical microphone transmit

- [x] Decode and report physical microphone PTT without acting on it.
- [x] Capture physical microphone audio from endpoint `0x82` while keyed in a
  controlled research test.
- [x] Demonstrate prerecorded microphone speech modulation into a dummy load.
- [x] Convert arbitrarily chunked live microphone audio into continuous USB or
  LSB transmit I/Q with persistent DSP state, drive scaling, clipping
  accounting, startup fade, buffering, and zero-I/Q underrun behavior.
- [x] Connect physical PTT press/release to the exclusive TX state machine.
- [ ] Add microphone gain, level metering, clipping protection, and optional
  processing controls.
- [x] Define behavior when physical PTT conflicts with an API or Soapy owner.

## Modulation and DSP

- [x] Implement the initial offline USB/LSB SSB modulator and verify spectrum
  orientation.
- [x] Integrate live USB and LSB modulation with the daemon stream scheduler;
  USB voice has been validated live, while LSB still requires its own live
  dummy-load validation.
- [ ] Implement and validate AM, FM, CW, and digital-mode transmit paths as
  individually reviewed work.
- [ ] Apply mode-specific TX filtering, carrier placement, and sideband rules.
- [ ] Prevent DC, clipping, discontinuities, and endpoint underruns.
- [x] Add deterministic DSP vector and spectral-regression tests for the
  implemented USB/LSB microphone modulator.

## Power and RF protection

- [x] Establish the 24,890-count constant-envelope reference that produces
  approximately 5 W on KB1JDX's FLEX-1500.
- [ ] Map requested drive percentage to calibrated I/Q magnitude and verify it
  at several power levels.
- [ ] Add conservative defaults and explicit maximum-drive enforcement.
- [ ] Research forward-power, reflected-power, temperature, and SWR telemetry
  available from the radio.
- [ ] Add protection responses for high SWR, excessive temperature, missing
  samples, and invalid telemetry where supported.
- [ ] Verify operation into a dummy load before any antenna testing of each new
  modulation or control path.

## API and applications

- [x] Expose Tune start, keepalive, and stop through the HTTP API.
- [x] Report Tune capability and active state through `/v1/radio`.
- [ ] Design general TX/PTT API routes with ownership tokens and explicit mode,
  drive, and audio/IQ source selection.
- [ ] Add authenticated and encrypted LAN control as the separate security
  project identified in the README goals.
- [ ] Add browser controls only after the corresponding API operation is safe
  and fully recoverable.
- [ ] Add SoapySDR TX streaming after the shared ownership and continuous TX
  scheduler are complete.
- [ ] Test interoperability with multiple SDR applications without allowing
  competing transmit owners.

## Testing and release readiness

- [ ] Add a mockable USB command/stream layer for exhaustive failure-injection
  tests without a radio.
- [ ] Test command failures at every preparation, key, stream, unkey, and
  restoration step.
- [ ] Test process signals, client loss, USB unplug/reconnect, stream underrun,
  lease expiry, and hard timeout while transmitting.
- [ ] Conduct reviewed dummy-load tests for every supported band, mode, and
  drive range.
- [ ] Perform an independent safety/code review before enabling general TX in a
  release build.
- [ ] Document operator responsibilities, supported configurations, known
  limitations, and emergency unkey procedures.

The validated fixed 5 W Tune carrier and physical-microphone USB/LSB path are
compiled into `--initialize-radio-and-enable-transmit`. Repeated live USB
voice tests into a dummy load have validated physical PTT, modulation, unkey,
and RX restoration. LSB still requires a dedicated live validation. Arbitrary
transmit I/Q, HTTP PTT, and SoapySDR transmit remain unavailable.
