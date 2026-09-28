# Client integration guide

This guide describes what an external program needs to use the documented
`flex1500d` network API. It is intended for developers building SDR front ends,
station-software bridges, audio adapters, or other clients without requiring
knowledge of the FLEX-1500 USB protocol.

The project includes `flex1500-client` as a working **thin client** reference.
It is a compatibility bridge, not a full SDR application: it translates the
daemon API into PipeWire audio devices and a local Hamlib-compatible rig-control
interface so established applications can supply their own waterfall, digital
mode, logging, and operator interface.

The complete route and field contract remains authoritative in the
[network API reference](NETWORK_API.md). This guide explains how those pieces
fit together in a useful client.

## Choose the client boundary

A client can operate at one of several levels:

- consume `F15I` receive I/Q and perform its own demodulation and display;
- translate the API into a compatibility interface such as SoapySDR, PipeWire,
  Hamlib, or another established protocol;
- provide receive-only monitoring inside the controlling station's RF window;
  or
- acquire station ownership and provide tuning and explicitly enabled transmit
  control.

A client does not need to reproduce the daemon's USB commands, filter mapping,
band policy, transmitter state machine, or cleanup logic. Those remain
authoritative in `flex1500d`.

## Connect and identify the daemon

The normal HTTP endpoint is TCP port 15000. Begin with:

```http
GET /v1/status HTTP/1.1
GET /v1/radio HTTP/1.1
```

Check at least:

- `api_version`, `software_version`, and `git_revision`;
- whether the radio is open and the receive stream is active;
- the known physical frequency, receive gain, mode, and bandwidth;
- `transmit_enabled` and the current TX state; and
- whether another station owns control.

Do not infer capabilities solely from a daemon version. Use the reported state
and normal HTTP results so the client behaves safely with different builds and
radio modes.

API version 1 closes ordinary HTTP connections after one request. A client may
therefore use short control connections while keeping the IQ or TX sample
tunnel open separately.

## Station ownership

In a transmit-enabled daemon, the first TX-capable client to request station
control becomes the owner. Acquire it with `POST /v1/control/owner`, retain the
returned lease, and renew it before its 15-second expiry. Include the lease on
hardware-changing and transmit requests as documented by the API.

Ownership persists across individual transmissions. Do not release it after
every PTT cycle. Release it deliberately when the application is finished, or
allow the daemon to expire it after a lost client. A client must stop presenting
writable hardware controls and PTT when ownership is rejected or lost.

The local rig-control or user interface may remain available while another
station owns the radio. In that state it must behave as receive-only: it may
report state and select a virtual receive frequency within the owner's current
48 kHz I/Q window, but it must not imply that it can retune the physical radio
or transmit.

If a protocol cannot communicate operating mode, it may identify itself as
mode-unaware when acquiring ownership. This lets the daemon select the normal
band-derived USB or LSB mode for physical-microphone PTT. It does not grant
additional authority.

Station and TX leases coordinate clients and enforce safety cleanup. They are
not authentication credentials.

## Receive I/Q

Open the long-lived receive stream with:

```http
GET /v1/stream/iq HTTP/1.1
```

The response contains versioned `F15I` frames. Each frame has a 20-byte header
followed by little-endian complex float32 samples at 48,000 complex samples per
second. Parse the header and sample count instead of assuming that a socket read
contains exactly one complete frame. Use sequence numbers to detect lost
application frames.

The native API preserves the FLEX-1500 sample orientation established during
reverse engineering. Clients that use conventional SDR spectrum orientation
must apply the documented Q correction at their boundary. The bundled SoapySDR
adapter, `rtl_tcp` listener, and thin client do this already.

The radio provides a real 48 kHz RF window. Resampling to a larger nominal rate
does not create additional RF bandwidth. A receive-only secondary may tune its
DSP approximately 24 kHz to either side of the physical center, subject to
filter transition regions and the selected signal bandwidth.

After opening a new stream or reconnecting, discard stale local buffers before
presenting audio. When the physical radio retunes, reset demodulator, resampler,
and audio state so samples from the previous frequency do not leak into the new
one.

## Tuning and receiver controls

An authorized owner can use the API to set physical frequency, host mode,
receive gain, DSP bandwidth, and squelch. Frequency changes also select the
mapped hardware RX filter and clear queued pre-tune IQ in the daemon.

Mode and bandwidth are host-side DSP state. The FLEX-1500 sends raw I/Q and
does not demodulate AM, FM, USB, LSB, or CW for a network client. A client that
publishes audio must implement the selected demodulator and filter locally.

Treat API rejection as authoritative. Do not update the displayed physical
frequency or writable state until the daemon accepts the request. A non-owner
can keep a local virtual tuning value, but it should clearly distinguish that
value from the shared hardware center.

## Producing and recording receive audio

A client obtains ordinary audio by:

1. consuming the 48 ksample/s complex `F15I` stream;
2. applying the required sample-orientation correction;
3. shifting within the physical 48 kHz window when necessary;
4. demodulating and filtering the selected mode; and
5. publishing mono PCM through a local audio interface.

On Linux, a PipeWire source is a practical output because PipeWire-aware and
PulseAudio-compatible programs can select it like any other recording device.
The bundled thin client demonstrates this with its `FLEX-1500 RX` source.

Recording does not require a daemon recording endpoint. An application can
record the published PCM audio using its normal facilities or an operating-
system audio recorder. Programs that need reproducible RF data instead should
record framed raw I/Q and the tuning metadata described in the
[raw-IQ interoperability guide](RAW_IQ_INTEROPERABILITY.md).

## Transmit integration

Transmit is available only when the daemon was explicitly started in transmit
mode and the client owns station control. A general network transmitter uses
the ownership-aware sequence documented in the API:

1. select frequency, mode, drive, and other settings while unkeyed;
2. create an audio or raw-I/Q TX session and retain its lease;
3. attach the persistent sample stream;
4. send enough data to satisfy the 4,096-frame prebuffer;
5. request PTT start;
6. continue sending correctly paced samples and renewing required leases;
7. request PTT stop; and
8. release the TX session when finished.

Mono PCM16 audio profiles let the daemon generate mode-correct AM, USB, or LSB
I/Q. Raw complex-IQ profiles are appropriate only for software that already
generates the complete emission. Both paths remain subject to daemon limiting,
drive, ownership, frequency policy, maximum-key timing, and cleanup.

Audio presence is not PTT. Digital silence or a quiet microphone is valid
sample data and must not cause a client to unkey automatically. Conversely,
feeding audio must not key the transmitter until an explicit PTT request has
been accepted. A VOX implementation would require a separate deliberate design
and safety review.

Normal PTT stop allows the daemon to drain its bounded sample tail before
unkeying. Client crash, stream disconnect, lease expiry, watchdog, USB error,
and shutdown use immediate safety cleanup. A client must report rejected PTT
or a failed sample stream visibly; it must not allow the controlling application
to believe transmission continues after the daemon has rejected or stopped it.

For arbitrary analog or digital emissions, follow the constraints in the
[raw-IQ interoperability guide](RAW_IQ_INTEROPERABILITY.md). The availability
of a raw-I/Q route does not imply that arbitrary samples are legal, correctly
tuned, or spectrally clean.

## Failure and reconnection behavior

A robust client should distinguish at least:

- daemon or network disconnection;
- radio/USB unavailability reported by the daemon;
- ownership rejection or expiry;
- receive sequence loss, network jitter, and local audio underrun;
- TX stream failure or PTT rejection; and
- a daemon watchdog or safety shutdown.

On connection loss, stop accepting new local transmit requests and close the
local TX path. The daemon remains the final unkey authority because a client
that has lost its network cannot send a cleanup request. After reconnecting,
query fresh status, reacquire ownership if appropriate, reopen streams, and
discard stale local audio/IQ. Never assume that a previous lease remains valid.

A thin compatibility client may communicate these states through concise
console diagnostics and standard error results. A full GUI is not required.

## Security boundary

API version 1 has no authentication or transport encryption. Use it only on a
trusted, firewalled LAN; do not expose the API directly to the Internet or an
untrusted wireless network. Ownership leases do not solve this limitation.
Future authentication and encryption are a separate project described in the
[API authentication design](API_AUTHENTICATION_DESIGN.md).

## Working reference implementations

Project components illustrate different client boundaries:

- [`flex1500-client`](../client/README.md) is the project's thin client. It
  demonstrates remote IQ reception, local demodulation, PipeWire RX/TX audio,
  station ownership, saved operating state, and Hamlib-compatible control.
- The SoapySDR adapter demonstrates mapping a conventional SDR device API to
  daemon discovery, controls, RX I/Q, ownership, and raw-I/Q TX.
- The daemon's optional `rtl_tcp` listener demonstrates receive-only protocol
  compatibility and rate conversion, although it is server-side rather than a
  separate client executable.
- The browser test page demonstrates direct use of the public HTTP API for
  receive controls, demodulated audio, Tune, and microphone TX.

Recorded thin-client tests, including WSJT-X operation and a completed 5 W FT8
contact, are in [companion client testing](COMPANION_CLIENT_TESTING.md).
