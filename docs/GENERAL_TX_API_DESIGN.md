<!-- SPDX-License-Identifier: GPL-3.0-only -->

# General TX/PTT API design

This document specifies the implemented general-transmit API. It is callable
only in the explicitly transmit-enabled daemon mode. The existing physical
microphone and fixed Tune paths remain available through the same exclusive
owner and cleanup state machine. Live validation of these network routes is
still required before routine use.

## Design principles

- Reserving the transmitter does not key it.
- Exactly one shared TX owner exists across Tune, physical microphone PTT,
  HTTP and SoapySDR streams.
- Mode, drive, and sample source are explicit and immutable while keyed.
- A network source must be connected, validated, and sufficiently prebuffered
  before PTT start can key the radio.
- Every stop, timeout, disconnect, parse error, underrun limit, USB failure,
  process signal, and shutdown uses the shared guaranteed-unkey path.
- Ownership leases identify an operation but do not authenticate a client.

## Session profile

An HTTP client requests an unkeyed TX session with a JSON profile:

```http
POST /v1/tx/sessions HTTP/1.1
Content-Type: application/json

{
  "mode": "usb",
  "drive_percent": 25,
  "source": "audio",
  "sample_format": "s16le",
  "sample_rate": 48000,
  "channels": 1
}
```

`mode` names the RF modulation performed by the daemon. Only modes whose
complete transmit path has been implemented and has appropriate offline safety
coverage may be accepted for experimental validation. Intended values are
`usb`, `lsb`, `am`, `fm`, `cw`, and `iq`:

- `audio` supplies mono baseband PCM to a mode-specific daemon modulator. It is
  valid only for an implemented audio modulation: AM, USB, or LSB.
- `iq` supplies already-modulated complex baseband samples. It requires mode
  `iq`; the daemon applies no voice-mode modulator. It enforces sample rate,
  final complex magnitude, drive, ownership, and coarse center-frequency
  containment, but it does not inspect occupied bandwidth or spectral purity.
  Client responsibilities are documented in the
  [raw I/Q interoperability guide](RAW_IQ_INTEROPERABILITY.md).

Raw I/Q is accepted only when the complete 48 kHz Nyquist span (24 kHz on each
side of the center frequency) remains inside one configured amateur allocation.
It is therefore rejected near band edges and on 60 meters. This guard cannot
determine whether client-generated samples comply with emission, license-class,
or geographic rules; the control operator remains responsible.

The AM audio profile produces a centered full carrier with symmetric filtered
sidebands and an 80% maximum nominal modulation index. Its 300–3000 Hz audio
passband requires at least 3 kHz of permitted allocation on each side of the
carrier. AM is therefore rejected at allocation edges and on 60 meters.

Tune is not an audio mode or sample source and continues to use its dedicated
API. Physical microphone input is selected by the radio's PTT edge rather than
by a network session, so `physical_mic` is not a valid general-API source.

The daemon accepts only explicitly enumerated formats and its supported sample
rate. Missing required members, incompatible mode/source pairs, and
out-of-range drive are rejected without reserving or keying TX. Unknown JSON
members do not alter the accepted profile. Source-specific gain and processing
settings may be included later, but they must feed the shared final limiter.
General TX uses 100% when a daemon-controlled default is needed, an intentional
operator policy choice for this 5 W QRP radio. Network profiles must still state
their drive explicitly. Accepted values are 1–100%, interpreted as I/Q
amplitude rather than calibrated RF power. A non-disableable shared limiter
caps audio and raw-I/Q sources at the selected drive, with 100% corresponding
to the validated 24,890-count complex-magnitude ceiling.

On success the daemon returns `201 Created` with an opaque random lease, the
accepted immutable profile, and timer values:

```json
{
  "lease": "opaque-random-value",
  "state": "reserved",
  "mode": "usb",
  "drive_percent": 25,
  "source": "audio",
  "lease_timeout_seconds": 15,
  "maximum_key_seconds": 180
}
```

The lease must have enough entropy to resist guessing, but remains an ownership
correlation value rather than an authentication credential. It should be sent
in the `X-Flex1500-TX-Lease` header so routine request logs do not place it in a
URL. Status responses report owner type and state but never this lease.

## Routes

| Method and route | Purpose |
| --- | --- |
| `POST /v1/tx/sessions` | Reserve the unkeyed HTTP owner with an explicit immutable profile. |
| `PUT /v1/tx/sessions/keepalive` | Renew the short ownership/liveness deadline. |
| `CONNECT /v1/tx/stream` | Open the one raw sample tunnel matching the reserved source and declared sample format. |
| `PUT /v1/tx/ptt/start` | Validate interlocks and prebuffer, then key this session. |
| `PUT /v1/tx/ptt/stop` | Stop accepting samples, perform the bounded normal drain, and retain the unkeyed session for reuse. |
| `DELETE /v1/tx/sessions/current` | Unkey if necessary, close the stream, and relinquish ownership. |

Every route except session creation requires `X-Flex1500-TX-Lease`. Future API
authentication additionally requires `Authorization`; the two headers have
independent meanings.

The CONNECT response remains open while samples are accepted. After receiving
`200 Connection Established`, the client sends a continuous raw stream in the format
fixed by its profile: mono signed PCM16 little-endian for `audio`, or interleaved
signed I/Q PCM16 little-endian for `iq`. TCP fragments are reassembled at sample
boundaries. A single stream cannot change its declared format. A second stream,
an incorrect lease, malformed profile, or sustained underrun is rejected and
must not silently substitute zeros indefinitely while keyed.

## Lifecycle

The required network sequence is:

1. Acquire a session while the shared controller is unowned and unkeyed.
2. Open the matching sample upload and fill the bounded pre-key buffer.
3. Send PTT start with the lease.
4. Continue samples and lease keepalives while transmitting.
5. Send PTT stop, which runs complete unkey/RX restoration.
6. Reuse the unchanged session or delete it to release ownership.

The 180-second default maximum-key timer starts at successful PTT start. Neither
samples nor keepalives extend it. A short lease watchdog and a shorter stream-
data watchdog stop TX earlier when the client disappears. Closing the upload
connection while keyed is a disconnect-unkey event. If the control request
vanishes after keying but before its response arrives, ownership and watchdogs
remain deterministic and will unkey unless the same lease resumes keepalives.

PTT stop is idempotent for the correct lease: retrying it after a lost response
returns the already-unkeyed state. Session deletion is likewise idempotent for
the correct current lease. A stale or foreign lease can never renew, feed,
start, stop, or release the active owner.

## Interlocks and conflicts

Session creation is available only in the explicitly transmit-enabled daemon.
PTT start rechecks all interlocks immediately before hardware preparation:
known permitted frequency, matching PA filter, implemented mode/source pair,
valid drive and levels, healthy USB stream, safe RX state, no fault/recovery,
adequate prebuffer, and ownership by the supplied lease.

Frequency, mode, drive, filter, source, and sample-format changes are rejected
with `409 Conflict` while keyed. This first design also rejects profile changes
while reserved; the client releases and reacquires with a new profile, avoiding
partly updated configurations. Receive clients remain connected during TX.
No rejected change is staged for the next transmission. To change frequency,
mode, drive, microphone gain, compressor state, maximum-key timeout, or filter
bandwidth, the client must explicitly unkey, apply the setting, and key again.
Host-only receive squelch may change while keyed because it cannot affect the
radio or transmitted samples. There is no direct PA-filter route; PA-filter
selection follows the accepted RF frequency and is reasserted during keying.

Physical microphone PTT has local priority under the shared ownership design.
It first safely unkeys and releases an active HTTP operation, then acquires the
physical-microphone owner without disconnecting the operator's API or Soapy
control session. The preempted lease becomes stale and cannot resume
automatically. HTTP and Soapy owners never preempt physical PTT.

## Responses and diagnostics

- `400 Bad Request`: malformed or internally incompatible profile/framing.
- `401 Unauthorized` or `403 Forbidden`: reserved for the future authentication
  layer, not ownership conflicts.
- `404 Not Found`: general TX API not compiled/enabled for this daemon mode.
- `409 Conflict`: TX busy, keyed configuration change, insufficient prebuffer,
  or controller in an incompatible state.
- `410 Gone`: stale, expired, preempted, or otherwise invalidated lease.
- `422 Unprocessable Content`: syntactically valid but unsupported mode,
  format, sample rate, frequency, or drive.
- `500 Internal Server Error`: hardware transition or cleanup failure, with the
  controller faulted until recovery.
- `503 Service Unavailable`: radio disconnected, recovering, or not prepared.

`GET /v1/radio` should expose TX capability, owner category, controller state,
accepted profile without secrets, keyed duration, and maximum-key duration.
`GET /v1/status` should expose starts/stops, ownership rejections, watchdog and
disconnect stops, underruns, malformed/dropped frames, limiter/clipping counts,
and cleanup failures.

## SoapySDR mapping

Future Soapy support uses the same internal operations even if the adapter
hides the HTTP details. `setupStream` validates the proposed format;
`activateStream` acquires a `soapy` owner, establishes/prebuffers the upload,
and keys only when the caller requests TX activation. `writeStream` supplies
samples and refreshes the stream-data watchdog. `deactivateStream`,
`closeStream`, device destruction, connection loss, and write timeout all
unkey and release the owner.

Soapy mode and gain calls populate the immutable session profile before stream
activation. They return an error while keyed rather than silently retuning or
changing drive. Authentication, when implemented, is supplied by the adapter
as described in [API and SoapySDR authentication notes](API_AUTHENTICATION_DESIGN.md).

## Required implementation gates

The routes, bounded upload handling, owner-specific leases, prebuffer policy,
watchdogs, disconnect cleanup, PCM modulation, raw-I/Q limiting, and offline
state-machine tests are implemented. Live testing remains separately
permission-gated. An independent key/unkey review and dummy-load validation of
each source remain required before treating general network TX as release-ready.

## Bounded validation client

`tools/network_tx_test_client.py` generates a 700 Hz test signal and defaults
to offline validation without opening a socket. Its live mode is hard-coded to
28.475 MHz, 50% drive, and three seconds, requires an exact arming string, and
always attempts PTT stop and session release during cleanup. `audio` exercises
mono PCM through the USB modulator; `iq` exercises guarded native complex I/Q.
Each live invocation remains subject to a separately approved dummy-load plan.
