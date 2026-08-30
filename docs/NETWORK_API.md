# flex1500d network API version 1

## Security and binding

The first server mode binds only to `127.0.0.1`. It is not reachable from other
machines. Remote-LAN binding, authentication, and access policy will be designed
before exposing the daemon beyond localhost.

The offline server cannot open the radio:

```sh
./build/flex1500d --serve-offline 15000
```

The permission-gated live RX server is also implemented and has completed its
first validation. Both modes remain loopback-only.

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

Responses include `Content-Length`, close the connection after one request, and
set `Cache-Control: no-store`.

## Radio endpoint

```http
GET /v1/radio HTTP/1.1
```

This read-only JSON route reports the verified model, firmware, USB VID/PID,
and the daemon capability boundary. It explicitly reports
`receive_only: true` and `transmit_enabled: false`. It also reports whether RX
tuning is armed and the current frequency/filter when known. It also reports
the host receive mode and nominal DSP bandwidth.

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
receive-gain writes from the loopback API. It does not enable PA-filter, PTT,
TX samples, EEPROM, firmware, antenna/path routing, or other control operations. Running it requires a
separate exact permission from KB1JDX. The older `--initialize-radio` token does
not enable receive-frequency or receive-gain control.

## IQ stream endpoint

```http
GET /v1/stream/iq HTTP/1.1
```

The offline server returns `503 Service Unavailable`. The live RX server returns
`200 OK` and publishes the tested binary framing format.

The permission-gated live mode will return `200 OK` with
`application/octet-stream` and stream frames until the client disconnects. It
supports one IQ client at a time and remains bound to loopback for the first
validation.

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

Unknown paths return `404` JSON. Outside the separately armed RX-frequency
route, no PTT, TX, firmware, EEPROM, antenna, gain, PA-filter, or other control
endpoint exists in API version 1. The successful standalone TX probes have no
daemon call path; representative PTT/TX requests are tested to remain 404.

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
  boundaries, offline tune rejection, absent TX routes, and—only in the
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
