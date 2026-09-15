<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Transmit operator guide

This guide describes the experimental transmit paths currently implemented by
`flex1500d`, their operating limits, and how to stop a transmission. It is not
a substitute for the FLEX-1500 manual, station safety procedures, or the
control operator's legal responsibilities.

## Operator responsibilities

The control operator is responsible for:

- using a suitable 50-ohm dummy load for unvalidated tests and a properly
  matched antenna system only for a path that has completed appropriate
  on-air readiness testing;
- complying with license privileges, band plans, emission restrictions, RF
  exposure limits, station-identification rules, and local regulations;
- independently monitoring frequency, modulation quality, output power, and
  SWR with suitable station equipment;
- preventing untrusted access to TCP port 15000; and
- remaining able to remove power from the radio immediately.

The FLEX-1500 does not provide the daemon with measured forward power,
reflected power, SWR, or PA temperature. Software cannot substitute estimated
values for external protection or metering.

## Enabling transmit

The normal `--initialize-radio` and
`--initialize-radio-and-enable-rx-tuning` modes remain receive-only. Transmit
is available only when deliberately enabled:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-transmit
```

This prepares the amplifier and PA-filter path, enables Tune, observes physical
microphone PTT, and exposes the guarded HTTP TX session routes. It also enables
receive and receive-frequency control. Starting this mode does not itself key
the transmitter.

The API currently listens on all host interfaces without authentication or
transport encryption. Use it only on a firewall-protected trusted LAN. TX
leases prevent competing transmitter owners but are not authentication
credentials or security boundaries.

## Currently supported paths

| Path | Current status |
| --- | --- |
| Capture-matched Tune carrier | Live validated at 5 W into a dummy load; exclusive lease, keepalive watchdog, maximum-key timer, and cleanup apply. |
| Physical microphone USB | Repeated live voice validation into a dummy load; physical PTT uses the shared exclusive owner. |
| Physical microphone LSB | Implemented with offline spectral tests; still needs a dedicated live dummy-load validation. |
| HTTP mono PCM (`s16le`, 48 kHz) | USB completed a live three-second tone test with zero underruns or errors. LSB uses the same tested DSP orientation but still needs dedicated live validation. |
| HTTP raw I/Q (`cs16le`, 48 kHz) | Live validated with constant-envelope calibration signals at 25%, 50%, 75%, and 100% drive at 28.475 MHz. The complete ±24 kHz span must fit inside an allowed amateur allocation. |
| SoapySDR raw-I/Q TX | Repeatedly live validated with SDR Oxide at 28.475 MHz into a dummy load, including clean PTT stop and lease release. Other applications remain unvalidated. |
| AM, FM, CW, and digital-mode modulation in the daemon | Not implemented. A client may not select these as daemon-generated TX modes. Raw-I/Q clients remain responsible for their generated emission. |

An SDR application's Tune button is not the daemon's dedicated Tune API.
During SDR Oxide validation it opened a normal raw-I/Q TX stream and keyed the
radio but produced no measurable RF output. Use the browser or HTTP Tune route
for the validated capture-matched 5 W carrier. The initial Soapy key may also
be delayed while the daemon fills its 4,096-frame (approximately 85 ms)
startup reserve.

The first TX-capable browser or SoapySDR station to connect retains persistent
station control across individual transmissions. All other network clients are
receive-only, cannot retune hardware, and can listen only within the owner's
48 kHz IQ window. Physical microphone PTT is the local-priority exception and
uses the current station configuration without revoking that network owner.

Only one TX operation may be active. Physical microphone PTT, Tune, HTTP, and
SoapySDR TX share the same transmitter state machine. Unsafe setting
changes are rejected while keyed and are never silently staged; unkey, change
the setting, and key again.

Tune and microphone-audio TX are blocked outside the daemon's configured
amateur-band allocations. This coarse guard is enforced again at the hardware
start boundary, but it cannot determine an operator's license privileges or
whether a particular subband and emission are legal.

## Drive and timing safeguards

General voice/data TX defaults to 100% drive by operator policy for this 5 W
QRP radio. Accepted drive values are 1–100%. Drive is a complex-I/Q amplitude
percentage, not an RF-watt percentage. A non-disableable final limiter caps all
daemon audio and raw-I/Q output at the selected drive. The measured calibration
at 28.475 MHz is documented in [TX drive calibration](TX_DRIVE_CALIBRATION.md).

Every owner shares a non-disableable maximum-key timer. It defaults to 180
seconds and may be configured from 30 through 1800 seconds while unkeyed. HTTP
TX additionally requires a prebuffer, a live sample stream, lease renewals,
and continuing sample delivery. Lease expiry, data timeout, stream disconnect,
or maximum-key timeout requests automatic unkey and RX restoration.

These mechanisms reduce risk but do not guarantee that commands reach hardware
after a USB failure. Watch the external radio and station instruments.

## Normal unkey procedures

- Physical microphone: release PTT.
- Tune: send `PUT /v1/radio/tune/stop/LEASE` with the lease returned by Tune
  start.
- HTTP audio or I/Q: send `PUT /v1/tx/ptt/stop` with the
  `X-Flex1500-TX-Lease` header, then release the session with
  `DELETE /v1/tx/sessions/current` using the same header.
- Daemon console: press Ctrl+C once and wait for the shutdown diagnostics.

A clean daemon shutdown reports TX cleanup confirmation, PA filter 0, and the
amplifier disabled. Review any `cleanup failed`, USB, command, or recovery
message rather than assuming the radio is safe solely because the process
ended.

Normal PTT release uses a bounded graceful stop: no new audio is accepted, and
up to one second is allowed for already queued DSP/USB frames to finish before
unkey. This bound prevents a stale backlog from holding the transmitter keyed.
All watchdog, disconnect, error, shutdown, preemption, and emergency paths
continue to unkey immediately. The daemon logs pending, drained, discarded,
and elapsed values for each graceful stop.

### Live graceful-stop validation — 2026-09-14

KB1JDX live-tested normal PTT release through the browser HTTP microphone,
SDR Oxide through SoapySDR, and the physical microphone. The browser and Soapy
tests transmitted speech that began before the relay click and preserved the
queued ending after Stop was pressed. The physical microphone also keyed,
carried intelligible audio, drained, and returned to receive normally.

After all three paths were exercised, daemon `0.2.0` at development revision
`80edebbc-dirty` reported 13 starts and 13 stops. A total of 375,763 frames
present at normal Stop requests were all drained in 8,419 ms cumulatively, with
zero graceful-discarded frames, zero dropped microphone frames, zero watchdog
stops, and zero cleanup failures. The largest observed pending queue was 46,048
frames, approximately 959 ms at 48 ksample/s. This validates the one-second
normal-stop bound for the tested workload; it does not alter the immediate
behavior of safety-driven stops.

## Emergency unkey

If RF continues after the expected unkey:

1. Release physical PTT and send the appropriate API stop if its controlling
   client is still responsive.
2. Press Ctrl+C once in the daemon terminal. Watch for confirmation that
   `SET_TR(0)`, RX restoration, PA filter 0, and amplifier disable completed.
3. If transmission continues or cleanup cannot reach the USB device, switch
   off or remove DC power from the FLEX-1500 using the station's established
   emergency procedure.
4. Do not rely on unplugging USB as an emergency unkey; USB loss can prevent
   the host from sending the unkey command while the radio remains powered.
5. Do not resume operation until the cause and cleanup diagnostics have been
   reviewed. Power-cycle and reconnect the radio only after RF has ceased.

Keep the radio's power control physically accessible. Do not handle RF
connectors, dummy loads, or antenna wiring as an emergency response while the
transmitter may still be energized.

## Known limitations

- This work has primarily been tested with KB1JDX's FLEX-1500, firmware
  `0.5.3.24`; behavior is not established for every unit or calibration.
- Most live TX work used 28.475 MHz and a dummy load. It does not establish
  clean, legal operation on every supported band.
- The daemon has no ALC, forward/reflected-power, SWR, or temperature telemetry
  from the radio.
- The 25/50/75/100% wattmeter results are frequency-, waveform-, radio-, and
  instrument-specific and must not be treated as universal watt settings.
- General HTTP TX remains unauthenticated and unencrypted.
- SoapySDR transmit and daemon-generated AM, FM, CW, and digital modulation are
  not available.
- Independent safety review, exhaustive fault injection, broader dummy-load
  testing, and antenna readiness remain incomplete checklist work.

Standalone TX research probes are separate fixed experiments. Their arming
strings are only safeguards against accidental execution. Follow
[transmit-research safety](TRANSMIT_RESEARCH_SAFETY.md) before using them.

## Pre-transmission check

Before each experimental transmission, confirm:

- the intended load is connected and rated for the test;
- a watt/SWR meter and monitor receiver are available as appropriate;
- the requested frequency, mode, drive, and source are correct;
- no other program controls the radio;
- the LAN is trusted and firewalled;
- the maximum-key timer is appropriate; and
- the radio power control is within reach.
