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

- [x] Design an exclusive controlling-client model, maximum-key timer,
  disconnect unkey, startup inhibit, and explicit TX-enable interlocks. This
  covers physical microphone PTT as well as HTTP and Soapy owners; see
  [TX_OWNERSHIP_STATE_MACHINE.md](TX_OWNERSHIP_STATE_MACHINE.md).
- [x] Generalize the Tune lease into one exclusive transmitter owner shared by
  physical PTT, HTTP TX, and SoapySDR TX.
- [x] Define RX, preparing, transmitting, unkeying, recovering, and faulted
  states with permitted transitions. The offline-tested ownership policy is
  documented in
  [TX_OWNERSHIP_STATE_MACHINE.md](TX_OWNERSHIP_STATE_MACHINE.md).
- [x] Reject frequency, mode, drive, and filter changes that are unsafe while
  keyed; define which changes may be staged for the next transmission. The API
  rejects frequency, mode, drive, microphone-gain, compressor, timeout, and
  filter-bandwidth changes for every active TX owner. Nothing is implicitly
  staged: unkey, apply the setting, and key again. Host-only receive squelch is
  explicitly safe to change.
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
- [x] Add source-specific physical-microphone gain, input/post-gain/output
  peak and RMS metering, a non-disableable final magnitude limiter, limiter
  diagnostics, and an optional compressor that defaults off. Future HTTP and
  Soapy audio sources will use their own source gains before this shared stage.
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
- [x] Map requested drive percentage to calibrated I/Q magnitude and verify it
  at several power levels. The linear amplitude mapping was verified at 25%,
  50%, 75%, and 100% using a constant-envelope waveform at 28.475 MHz;
  KB1JDX measured approximately 0.75 W, 3.5 W, 4.0–4.25 W, and exactly 5 W.
  The implemented mapping, controlled measurement procedure, and results are in
  [TX drive calibration](TX_DRIVE_CALIBRATION.md).
- [x] Add conservative safety defaults and explicit maximum-drive enforcement.
  TX remains disabled unless explicitly enabled at daemon startup. At KB1JDX's
  request, general voice/data TX defaults to 100% for this 5 W QRP radio. All
  daemon audio and raw-I/Q paths reject values outside 1–100% and pass through
  a non-disableable limiter at the selected drive, with 24,890 counts as the
  full-drive ceiling. Drive is amplitude, not calibrated RF wattage; Tune
  remains its separate validated fixed full-drive carrier.
- [x] Research forward-power, reflected-power, temperature, and SWR telemetry
  available from the radio. The FLEX-1500 exposes none of these as measured
  radio telemetry; see [FLEX-1500 transmit telemetry research](FLEX1500_TX_TELEMETRY.md).
- [x] Add protection responses for high SWR, excessive temperature, missing
  samples, and invalid telemetry where supported. The FLEX-1500 provides none
  of this measured telemetry, so no radio-derived protection response is
  supported; external metering would be required. See
  [FLEX-1500 transmit telemetry research](FLEX1500_TX_TELEMETRY.md).
- [ ] Verify operation into a dummy load before any antenna testing of each new
  modulation or control path.

## API and applications

- [x] Expose Tune start, keepalive, and stop through the HTTP API.
- [x] Report Tune capability and active state through `/v1/radio`.
- [x] Implement the general TX/PTT API routes with ownership tokens and
  explicit mode, drive, and audio/IQ source selection. The implementation
  contract is documented in
  [GENERAL_TX_API_DESIGN.md](GENERAL_TX_API_DESIGN.md).
- [ ] Add authenticated and encrypted LAN control as the separate security
  project identified in the README goals. This remains open but does not block
  controlled transmit development or operation on a firewall-protected,
  trusted LAN; see
  [API and SoapySDR authentication notes](API_AUTHENTICATION_DESIGN.md).
- [x] Add browser controls only after the corresponding API operation is safe
  and fully recoverable. The test page now controls Tune and TX settings and
  uploads computer-microphone PCM through the leased owner, prebuffer, data
  watchdog, maximum-key timer, limiter, disconnect cleanup, and explicit stop.
  Microphone TX is disabled outside `localhost` or another secure context.
- [x] Add SoapySDR TX streaming after the shared ownership and continuous TX
  scheduler are complete. The adapter exposes TX only when the daemon is in
  transmit-enabled mode and maps continuous `CF32`/`CS16` I/Q into a leased
  raw-I/Q session with prebuffering, PTT, keepalive, and disconnect cleanup.
  KB1JDX repeatedly live-validated application audio, keying, unkeying, and
  lease release with SDR Oxide at 28.475 MHz into a dummy load. SDR Oxide's
  Tune control uses ordinary raw I/Q rather than the dedicated 5 W Tune API;
  it keyed without measurable RF in the observed test.
- [ ] Test interoperability with multiple SDR applications without allowing
  competing transmit owners.

## Testing and release readiness

- [x] Add a mockable USB command/stream layer for exhaustive failure-injection
  tests without a radio. The production TX command and stream paths now use the
  interface documented in [USB I/O mocking](USB_IO_MOCKING.md), with an
  in-memory fail-on-operation test backend.
- [x] Test command failures at every preparation, key, stream, unkey, and
  restoration step. The offline matrix injects a failure at each of nine start
  and seven cleanup operations and verifies that every required cleanup action
  is still attempted; see [USB I/O mocking](USB_IO_MOCKING.md).
- [ ] Test process signals, client loss, USB unplug/reconnect, stream underrun,
  lease expiry, and hard timeout while transmitting.
- [ ] Conduct reviewed dummy-load tests for every supported band, mode, and
  drive range.
- [ ] Perform an independent safety/code review before enabling general TX in a
  release build.
- [x] Document operator responsibilities, supported configurations, known
  limitations, and emergency unkey procedures. See the
  [transmit operator guide](TX_OPERATOR_GUIDE.md).

The validated fixed 5 W Tune carrier and physical-microphone USB/LSB path are
compiled into `--initialize-radio-and-enable-transmit`. Repeated live USB
voice tests into a dummy load have validated physical PTT, modulation, unkey,
and RX restoration. LSB still requires a dedicated live validation. HTTP PCM,
guarded raw-I/Q PTT, and the SDR Oxide SoapySDR path have completed bounded
dummy-load tests. Compatibility with additional SoapySDR applications remains
open.
