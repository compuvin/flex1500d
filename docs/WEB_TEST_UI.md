# Opt-in receive test page

The small browser page is a development/test client for the `flex1500d` API. It
does not access USB, execute local programs, or communicate with the FLEX-1500
directly. Its only operations are:

- `GET /v1/radio`
- `GET /v1/status`
- `PUT /v1/radio/frequency/FREQUENCY_HZ`
- `PUT /v1/radio/mode/am|fm|usb|lsb|cw`
- `GET /v1/stream/iq`

When the daemon is deliberately started in transmit-enabled mode and the page
owns station control, it also exercises the documented Tune and general TX
session APIs. Computer-microphone TX supports AM, USB, and LSB. The page still
never accesses USB or radio hardware directly.

It parses the versioned `F15I` stream in JavaScript and provides receive-only
AM, FM, USB, LSB, and CW demodulation with DC removal, smoothing, and AGC. CW
uses a 700 Hz beat note. Mono 48 kHz audio is queued through an `AudioWorklet`.
Tuning or changing mode stops audio; press Start audio again afterward.

The page displays initial host-DSP bandwidths without allowing them to
be edited: AM 6 kHz, USB/LSB 2.7 kHz, FM 12 kHz, and CW 500 Hz. The SSB
passbands run approximately 100–2800 Hz from the carrier (center 1450 Hz,
half-width 1350 Hz), below the carrier for LSB and above it for USB. These are
demodulator bandwidths, not the numbered hardware RX preselector selected as a
function of center frequency. USB and LSB use opposite complex 65-tap FIR
bandpass filters so that the dial remains at the carrier frequency; CW uses a
centered narrow FIR followed by its 700 Hz beat oscillator. Adjustable filters
remain future work.

The AudioWorklet uses a 4096-sample (approximately 85 ms) startup/rebuffer
threshold to absorb browser scheduling jitter. Its underrun count is displayed
while audio runs. An increasing count indicates browser audio starvation;
signal fading or RF noise does not increment it.

The first live LSB observation exposed an error in the initial browser
implementation: moving a sideband centered near 1.65 kHz to zero before taking
the real component shifted recovered speech and made a station near 7,296,000
Hz sound best with the radio tuned near 7,294,500 Hz. The FIR sideband filters
replace that shortcut and preserve the expected carrier-frequency dial model.

Transmit buttons are hidden behind the daemon's transmit-enabled policy and
shared ownership/state machine. The page contains no standalone probe token or
direct radio interface.

## Explicit opt-in

Normal daemon modes do not serve HTML and return 404 for `/test`. The page is
available only when the exact `--enable-test-page` flag is present:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-rx-tuning \
  --enable-test-page
```

After that separately permissioned command starts, open:

```text
http://DAEMON_HOST:15000/test
```

The browser may run on the daemon host or another trusted LAN computer. The
current API is unauthenticated and unencrypted, so the daemon port must be
restricted to trusted hosts. Browser audio must support a 48 kHz `AudioContext`
and `AudioWorklet`.

The offline form can validate page delivery without USB or radio access:

```sh
./build/flex1500d --serve-offline 15001 --enable-test-page
```

## Long-term boundary

The embedded page is not intended to define the production daemon architecture.
It is an opt-in integration harness that exercises the same API a future
SoapySDR adapter or standalone web client will use. It can later move to
separately hosted static files without changing the radio/stream API.

## Offline validation

On 2026-08-29, strict C compilation and all 22 tests present at that stage
passed. JavaScript syntax checking passed. A loopback integration test
confirmed that normal daemon mode
returned 404 for `/test`, while the opt-in offline mode returned HTTP 200,
`text/html; charset=utf-8` and the embedded page body.

## First live browser observation

KB1JDX started the opt-in live daemon and observed that Tune RX changed the
radio frequency but the page subsequently displayed `TypeError: Failed to
fetch`. The tune request itself succeeded. The page then issued `/v1/radio` and
`/v1/status` concurrently, while the current daemon accepts only one pending
ordinary HTTP request; it closed the second connection. The test page was
changed to fetch those status resources sequentially. The daemon API and tuning
operation were not changed.

KB1JDX also noted no audible click at receive enable or RX band changes. This is
expected: two physical tests found no audible relay operation from
`SET_RX1_FILTER`. The previously confirmed click came from `SET_PA_FILTER`,
which is TX-owned and intentionally absent from the receive daemon.
