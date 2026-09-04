# flex1500d network API version 1

## Security and binding

Server modes listen on all IPv4 interfaces (`0.0.0.0`) using the selected TCP
port. Port 15000 is the normal default for clients and future service packaging,
so installations do not need a machine-specific bind address.

API version 1 does not yet provide authentication or transport encryption.
Treat it as a trusted-LAN interface: restrict the port with the daemon host's
firewall, do not create an internet port-forward, and do not expose it on an
untrusted Wi-Fi network. Secure authenticated/encrypted access remains a
separate roadmap item. The proposed separation between API credentials and TX
ownership, including SoapySDR credential handling, is recorded in
[API and SoapySDR authentication notes](API_AUTHENTICATION_DESIGN.md).

The offline server cannot open the radio:

```sh
./build/flex1500d --serve-offline 15000
```

The permission-gated live RX server is also implemented and has completed its
first validation. Both modes use the same LAN listener.

An embedded receive test client is available only with the separate
`--enable-test-page` flag at `GET /test`. Normal daemon modes return 404 for
that path. The page is an API consumer, not a direct USB/radio interface; see
`WEB_TEST_UI.md`.

## Status endpoint

```http
GET /v1/status HTTP/1.1
```

Response content type: `application/json`.

Current fields include:

- `service` and `api_version`
- daemon `state`
- `radio_open` and `network_listening`
- `sample_rate` and `sample_format`
- processed and sentinel frame counts
- USB packet and packet-error counts
- ring-buffer dropped-frame count
- network frame and sample delivery counts
- network backpressure, disconnect, and write-error counts

Transmit diagnostics are also reported:

- `tx_starts` and `tx_stops` count successful Tune starts and all active-Tune
  stop attempts;
- `tx_underruns`, `tx_clipped_frames`, `tx_limited_frames`, and
  `tx_dropped_microphone_frames` report live microphone-stream quality;
- `tx_audio_meter_valid` and the input, post-gain, and output peak/RMS dBFS
  fields describe the current or most recently started microphone stream;
- `tx_rejected_ownership_requests` counts busy starts and invalid or mismatched
  Tune lease operations;
- `tx_watchdog_stops` counts lease-expiry and hard-limit stops; and
- `tx_cleanup_failures` counts Tune or final PA/amplifier cleanup failures.

The daemon also prints the six-counter summary during shutdown. Counters survive
an automatic USB recovery within the same daemon process.

Responses include `Content-Length`, close the connection after one request, and
set `Cache-Control: no-store`.

## Radio endpoint

```http
GET /v1/radio HTTP/1.1
```

This read-only JSON route reports the verified model, firmware, USB VID/PID,
and the daemon capability boundary. Normal startup reports `receive_only: true`,
`transmit_enabled: false`, `tune_enabled: false`, and `tune_active: false`.
The transmit-enabled startup reports Tune capability and current activity
through those fields. The route also reports whether RX tuning is enabled and
the current frequency/filter when known. It also reports
the host receive mode and nominal DSP bandwidth. The fields
`physical_inputs_known`, `mic_ptt`, `flexwire_ptt`, `dash`, and `dot` expose the
latest endpoint-`0x83` observation. Physical microphone PTT acts on the
transmitter only in the explicitly transmit-enabled daemon mode.

## Receive demodulator mode

The host-side mode state accepts AM, FM, USB, LSB, and CW:

```http
PUT /v1/radio/mode/usb HTTP/1.1
```

This endpoint does not send a USB command or change radio hardware. It records
the receive DSP selection used by API clients such as the test page. Initial
nominal bandwidths are AM 6000 Hz, USB/LSB 2700 Hz, FM 12000 Hz, and CW 500 Hz.
The SSB passbands are approximately 100–2800 Hz from the carrier. The current
bandwidth is reported as `rx_bandwidth_hz`. CW uses a 700 Hz beat note in the
browser test demodulator.
Bandwidth is adjustable from 100 through 20,000 Hz with:

```http
PUT /v1/radio/bandwidth/2400 HTTP/1.1
```

Host-side squelch uses a dBFS threshold from -120 through 0. A value of -120
is effectively open:

```http
PUT /v1/radio/squelch/-60 HTTP/1.1
```

Both settings are available in the web test page. SoapySDR exposes bandwidth
through its standard bandwidth interface and squelch as the `squelch_db`
device setting.

## Receive gain

The armed live receive daemon sets +20 dB at startup and accepts exactly five
hardware gain settings:

```http
PUT /v1/radio/gain/20 HTTP/1.1
```

Valid values are -10, 0, 10, 20, and 30 dB. Successful requests return the
applied `rx_gain_db`; other values are rejected without a USB write. The same
control is available through the web test page and SoapySDR's standard RX gain
interface.

## Receive-frequency control

RX tuning is unavailable in the offline server and in the existing
initialization-only live command. Those modes return 404 for the control path.
A separately armed receive-only daemon mode enables:

```http
PUT /v1/radio/frequency/10000000 HTTP/1.1
```

The accepted range is 100,000 through 54,000,000 Hz. A successful request sends
exactly `SET_RX1_FREQ_TW`, followed by the PowerSDR-mapped `SET_RX1_FILTER`, and
returns the applied frequency and filter as JSON. Out-of-range values return
400; a USB command failure returns 500 and increments
`radio_command_errors`. Successful paired operations increment
`rx_tune_operations`.

After both commands succeed, the daemon clears queued pre-tune IQ. An attached
IQ stream remains connected, allowing the browser test receiver to tune without
stopping audio. A small amount of already-buffered pre-tune audio may still be
heard during the transition. SoapySDR clients may continue to reconnect after
a frequency change to reset their own stream state.

The reconnect path atomically replaces the previous single IQ socket. This
avoids a race where a fast SoapySDR reconnect arrived before the daemon had
observed the old socket closing and received a truncated HTTP response. A live
test retuned an active Soapy stream through 7.1, 14.2, 21.25, and 28.475 MHz,
then restored 10 MHz, with valid samples after every change and no USB packet
or radio-command errors.

The new live command is deliberately distinct:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-rx-tuning
```

It initializes the radio and permits receive frequency/filter and validated
receive-gain writes from the API. It does not enable Tune or any other
transmit operation. The older `--initialize-radio` token does not enable
receive-frequency or receive-gain control.

## Tune control

The fixed, capture-matched nominal 5 W Tune carrier is available only with the
transmit-enabled live daemon command:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-transmit
```

These routes are available through the LAN API:

- `PUT /v1/radio/tune/start` returns a lease;
- `PUT /v1/radio/tune/keepalive/LEASE` renews its 15-second watchdog; and
- `PUT /v1/radio/tune/stop/LEASE` stops immediately.

Tune start is rejected with `422 tx_frequency_not_allowed` unless the current
frequency is inside the daemon's configured amateur-band allocations. The API
checks this before acquiring a lease, and the hardware-start callback checks it
again immediately before starting the carrier. This is a coarse safety guard,
not a substitute for the operator's license-class, subband, mode, or geographic
requirements.

The transmit-enabled daemon also accepts `PUT /v1/radio/tx-drive/PERCENT`
(1–100) and `PUT /v1/radio/mic-gain/DB` (0–70 dB) while unkeyed. General
voice/data TX defaults to 100% drive and 10 dB microphone gain. On this 5 W
QRP radio, the full-drive default is an intentional operator policy choice.
These configure the immutable profile
captured by the next physical PTT press; neither route keys the transmitter.
Frequency, mode, drive, and microphone-gain changes return `409 Conflict` while
any TX owner is active. Filter-bandwidth, compressor, and maximum-key-timeout
changes are rejected as well. No rejected setting is staged: unkey, apply the
change, and key again. Receive squelch remains adjustable because it is
host-only and cannot affect RF or transmitted samples.

An optional shared speech compressor is disabled by default and may be changed
only while unkeyed:

```http
PUT /v1/radio/tx-compressor/on HTTP/1.1
```

Use `off` to disable it. The initial implementation uses a conservative static
3:1 curve above -12 dBFS. The final complex-I/Q limiter is always enabled and
caps output at the magnitude selected by TX drive; it cannot be disabled.
Drive is an I/Q-amplitude percentage, not calibrated RF wattage. Every general
voice/data source is capped at 100% (24,890-count complex magnitude), including
raw network I/Q. The dedicated Tune operation remains the separately validated,
fixed full-drive carrier.
`GET /v1/radio` reports `tx_compressor_enabled`, while `GET /v1/status` reports
meter values and limiter activity. Physical-microphone gain remains a source-
specific control. Future HTTP and Soapy audio sources should have separate
source gains and then enter this same compressor, limiter, and meter path.

Physical microphone TX additionally requires USB or LSB and a known frequency
inside the daemon's conservative U.S. amateur voice-allocation policy: the
160, 80, 40, 20, 17, 15, 12, 10, or 6 meter allocation, or the permitted
60-meter USB spectrum/carriers. This guard is not a substitute for the control
operator's license-class, subband, emission, power, and geographic obligations.
The operator remains responsible for legal operation.

The shared maximum-transmit timer is configured with:

```http
PUT /v1/radio/tx-timeout/180 HTTP/1.1
```

It defaults to 180 seconds and accepts 30 through 1800 seconds. Zero is
rejected, so the safety timer cannot be disabled. Changes while any TX owner is
active return `409 Conflict`; the API/Soapy control connection remains
connected. `GET /v1/radio` reports the current value as
`tx_timeout_seconds`, and automatic USB recovery preserves it.

Tune has an independent 60-second hard limit. Expiry, shutdown, USB failure,
or a partial startup failure invokes transition mute, `SET_TR(0)`, receive-
frequency restoration, PA-filter reset, and endpoint-stream cancellation.
The lease is a safety/ownership mechanism, not access control. API
authentication remains a separate project item. Until then,
firewall access to the API must be limited to trusted station-control hosts.
SoapySDR does not expose the dedicated Tune operation. Its optional TX channel
uses the general leased raw-I/Q routes described below.

The transmit-enabled daemon implements leased general network-transmit session,
PTT, and sample-upload routes for mono `s16le` audio and `cs16le` I/Q. They use
a 500 ms prebuffer, 15-second lease watchdog, one-second keyed-data watchdog,
the shared maximum-key timer, mandatory output limiting, and disconnect unkey.
The route contract and SoapySDR mapping are specified in
[General TX/PTT API design](GENERAL_TX_API_DESIGN.md). These routes have offline
test coverage; USB PCM audio and raw I/Q have also completed bounded live
dummy-load tests. Individual modes, bands, and applications still require the
validation tracked in the engineering checklist. Operational responsibilities,
supported paths, limitations, and normal and emergency unkey procedures are in
the [transmit operator guide](TX_OPERATOR_GUIDE.md).

The browser test page uses `POST /v1/tx/audio` for bounded mono `s16le` PCM
blocks under the same TX lease. The first block attaches the logical stream;
each accepted block refreshes the one-second sample-data watchdog. Bodies must
contain an even number of bytes and are limited to 9,600 bytes (100 ms at
48 kHz). This route accepts only an audio-source session and does not replace
the persistent `CONNECT` tunnel intended for native clients.

Browser microphone capture is enabled only in a secure browser context.
`http://localhost:15000/test` qualifies on the daemon host; a page opened from
another LAN computer normally requires future HTTPS support. Tune and ordinary
radio controls do not require microphone permission.

## IQ stream endpoint

```http
GET /v1/stream/iq HTTP/1.1
```

The offline server returns `503 Service Unavailable`. The live RX server returns
`200 OK` and publishes the tested binary framing format.

The permission-gated live mode will return `200 OK` with
`application/octet-stream` and stream frames until the client disconnects. It
supports one IQ client at a time.

## Binary IQ frame

All multibyte header integers are big-endian. Float payload values are IEEE-754
little-endian.

| Offset | Size | Meaning |
|-------:|-----:|---------|
| 0 | 4 | ASCII magic `F15I` |
| 4 | 1 | Protocol version: 1 |
| 5 | 1 | Format: 1 = complex float32 little-endian |
| 6 | 2 | Header size: 20 |
| 8 | 4 | Monotonic frame sequence |
| 12 | 4 | Complex sample rate: 48,000 |
| 16 | 4 | Complex sample count |
| 20 | variable | Repeated float32 I, float32 Q pairs |

The sequence number lets clients detect a lost application frame independently
of USB packet counters. The sample count lets clients skip or buffer frames
without assuming a fixed network chunk size.

## Unsupported routes

Unknown paths return `404` JSON. General PTT/TX routes exist only in the
explicitly transmit-enabled daemon; receive-only modes keep them unavailable.
No firmware, EEPROM, antenna, or direct PA-filter route exists. The standalone
TX probes have no daemon call path.

## Offline verification

The saved raw capture can be passed through the same DC blocker, IQ ring, and
network-frame publisher without opening USB or a socket:

```sh
./build/flex1500d --frame-capture captures/rx-settled.iq16le /tmp/rx.f15i
```

The output is a concatenation of the binary IQ frames described above. The
command uses exclusive creation and refuses to overwrite an existing file.

- Status JSON and HTTP response headers are byte-tested.
- IQ frame headers, integer byte order, and float byte order are byte-tested.
- The inactive IQ route is tested for `503`.
- HTTP bytes are tested through an in-process Unix socket pair where permitted.
- The default receive-only build passes 19 tests. Enabling the separately
  guarded TX-research build adds five offline/interlock tests for a total of
  24. Coverage includes rejection of unarmed radio modes, partial HTTP-header
  detection, radio metadata, publisher counters, frequency parsing/filter
  boundaries, offline tune rejection, receive-mode TX rejection, and—only in the
  research build—isolated TX-probe arming strings.

## Receive API hardening validation

Date: 2026-08-28

An offline loopback integration check on port 15001 confirmed:

- `/v1/status` returned all USB, ring, and network publisher counters.
- `/v1/radio` returned model `FLEX-1500`, firmware `0.5.3.24`, USB IDs
  `2192:1502`, `receive_only: true`, and `transmit_enabled: false`.
- `POST /v1/radio/ptt` returned `404 Not Found`.
- The server stopped cleanly with `SIGINT`.

No USB or radio access occurred during this validation.

## First live RX-tuning and bounded WAV validation

Date: 2026-08-29

KB1JDX approved starting the separately armed receive-only tuning daemon and a
single tune to the previously validated 10.000 MHz WWV frequency. The daemon
sent `INITIALIZE`; the API then successfully sent `SET_RX1_FREQ_TW(10000000)`
and `SET_RX1_FILTER(5)`. The tune response was HTTP 200, and `/v1/radio`
reported 10,000,000 Hz, filter 5, `receive_only: true`, and
`transmit_enabled: false`.

`tools/capture_network_iq.py` discarded 12,000 settling samples and captured
exactly 240,000 sequence-continuous samples from 987 `F15I` frames. The raw
capture is `captures/rx-10mhz-5s.iq16le`. Offline AM demodulation produced
`captures/rx-10mhz-5s-am.wav`: mono PCM16, 48 kHz, exactly 240,000 frames and
5.000 seconds.

Offline analysis of the retained network-derived capture finds no literal
`(-1,-1)` pairs, but this is not proof that no source sentinel occurred: the
network stream is DC-corrected complex float, so converting it back to
`iq_s16le` does not preserve the raw sentinel signature. Across the daemon's
longer lifetime, status reported 51,325 completed USB packets, 67 packet-status
errors, 6 raw sentinel frames, one successful tune operation, and zero
radio-command errors. The bounded application stream had continuous sequence
numbers.

At 48 frames per completed 192-byte packet, 51,325 packets would contain
2,463,600 frames, but the processor reported 2,463,533: a separate 67-frame
shortfall. This implies that some descriptors marked completed had short
payloads. The current combined counter cannot reconstruct whether the 67 packet
errors were individual isochronous errors or included a whole-transfer failure,
which adds 64 at once. More granular transfer-status and packet-length counters
are required before another diagnostic run.

The daemon was stopped cleanly after capture. No endpoint `0x01`, PA-filter,
PTT, or TX operation occurred.

## Granular USB diagnostic comparison

Date: 2026-08-29

After the first run's combined 67-error count, the receive scheduler gained
separate whole-transfer status classes, individual packet status classes,
completed-packet length checks, missing/trailing-byte totals, and first/last
event timing. KB1JDX approved a directly comparable receive-only run at 10 MHz
with RX filter 5 and another bounded five-second IQ capture.

The comparison reported:

- 44,544 completed packets and exactly 2,138,112 processed frames
- zero whole-transfer status errors
- zero individual packet-status errors
- zero short, zero-length, or oversized completed packets
- zero missing or trailing bytes
- zero command errors and one successful RX tune
- continuous `F15I` sequence numbers for the bounded capture
- 24 candidate raw `(-1,-1)` sentinel frames, about 11.2 ppm

Thus the previous USB loss did not recur and is consistent with a transient
rather than a deterministic defect in every run. The first implementation of
daemon-relative sentinel timestamps initialized its clock during stop instead
of start, producing invalid multi-hour millisecond values. That instrumentation
bug—and the intended initial command index—were corrected offline afterward;
all 22 tests present at that stage passed. The recorded sentinel frame
positions remain valid, but a
future separately approved run is needed before using their timestamps.

The comparison IQ is saved as
`captures/rx-10mhz-5s-diagnostic.iq16le`. No TX operation occurred.

## First loopback-server validation

Date: 2026-08-27

The user started the offline daemon on port 15000. Read-only HTTP checks
confirmed:

- The process listened only on `127.0.0.1:15000`.
- `GET /v1/status` returned `200 OK`, API version 1, state
  `offline_serving`, `radio_open: false`, and `network_listening: true`.
- `GET /v1/stream/iq` returned the intended `503 Service Unavailable` JSON.
- An unknown route returned the intended `404 Not Found` JSON.
- Content types, content lengths, connection-close behavior, and no-store cache
  headers were present.

No USB or radio access occurred during this server validation.

## First live-RX daemon validation

Date: 2026-08-27

KB1JDX explicitly approved this command:

```sh
./build/flex1500d --serve-live-rx 15000 --initialize-radio
```

The daemon sent the one fixed opcode-1219 `INITIALIZE` packet, started the
continuous endpoint-`0x82` receive scheduler, and listened only on
`127.0.0.1:15000`.

Read-only localhost validation confirmed:

- `/v1/status` reported state `receiving`, `radio_open: true`, and a 48 kHz
  complex-float stream.
- After 63,936 USB packets, the daemon reported zero USB packet errors and zero
  sentinel frames.
- A bounded one-second IQ client received 735,184 bytes. Its first complete
  frame had magic `F15I`, protocol version 1, sequence 0, sample rate 48,000,
  and 256 complex samples.
- The client timeout intentionally truncated its last in-flight frame; the
  daemon continued receiving after that disconnect.
- Ring drops accumulated while no IQ client was connected. This is the intended
  bounded-buffer behavior: the daemon accounts for dropped new samples instead
  of overwriting unread data silently.

No frequency, filter, gain, routing, PTT, TX, firmware, or EEPROM command was
sent during this validation. After the checks, the daemon was stopped cleanly
with `SIGINT`; it exited with status 0 after cancelling its host-side IN
transfers, releasing interface 3, and closing the listener.
