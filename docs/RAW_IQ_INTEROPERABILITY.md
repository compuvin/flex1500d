# Raw I/Q interoperability guide

This guide covers two different operations:

1. recording and replaying received complex samples without losing tuning
   context; and
2. supplying already-modulated complex samples for transmission.

They use related sample representations, but an RX recording is not inherently
safe or appropriate to transmit. The control operator remains responsible for
frequency authorization, occupied bandwidth, spectral cleanliness, power, and
the content of every emission.

## Receive recording

The most reproducible native recording is the byte-for-byte response body from
`GET /v1/stream/iq`. For example:

```sh
curl --no-buffer http://127.0.0.1:15000/v1/stream/iq \
  --output flex1500-recording.f15i
```

Stop `curl` with Ctrl+C. The resulting file is a sequence of `F15I` frames, not
a headerless raw-float file. Each frame contains:

- four-byte ASCII magic `F15I`;
- protocol version 1 and format 1;
- a 20-byte header;
- a monotonically increasing frame sequence number;
- a 48,000-complex-sample/s rate and sample count; and
- interleaved IEEE-754 float32 I then Q, each little-endian.

Header integers are big-endian. The complete byte layout is documented in the
[network API](NETWORK_API.md#binary-iq-frame). Preserve frame boundaries and
sequence numbers: a replay or converter can then detect a truncated file or a
lost network frame instead of silently changing time alignment.

`F15I` does not put RF center frequency or wall-clock time in every frame.
Record a UTF-8 JSON sidecar at the start of each capture. At minimum it should
contain:

```json
{
  "format": "flex1500-f15i-v1",
  "sample_rate": 48000,
  "center_frequency_hz": 14225000,
  "started_utc": "2026-09-15T00:00:00Z",
  "iq_orientation": "flex1500-native",
  "daemon_version": "0.2.1",
  "daemon_git_revision": "2dee1686",
  "rx_mode": "usb",
  "rx_bandwidth_hz": 2700,
  "rx_gain_db": 20,
  "operator_notes": "example only"
}
```

Obtain the radio settings from `GET /v1/radio` and the daemon version from
`GET /v1/status`. Use the actual UTC start time and values returned for that
capture; the example values above are not defaults. If the hardware center
frequency changes, close the current recording and begin a new file/sidecar.
Otherwise a single file would contain samples with more than one RF frequency
but no in-band marker identifying the change.

The native `F15I` orientation is the convention used by the daemon's browser
DSP. The SoapySDR adapter negates Q at its boundary to follow the convention
expected by Soapy applications. A converter must record which convention it
wrote and negate Q exactly once when crossing between them. It must not infer
orientation from the selected USB or LSB receive mode.

For replay into a receiver DSP, parse and validate every `F15I` header, preserve
the recorded 48 ksample/s timing, report sequence gaps, and restore the center
frequency from the sidecar when labeling absolute frequencies. The daemon does
not currently provide a route that injects a recording as simulated receive
data. Replay is therefore a client-side DSP or file-conversion operation.

Soapy applications may instead use their own recording containers. Such a
recording still needs the same center frequency, rate, complex format,
orientation, start time, and gap information. A headerless `.cf32` or `.cs16`
file without a sidecar is not considered a reproducible station recording.

## Raw-I/Q transmit input

Raw-I/Q TX means the client has already generated the modulation. The daemon
does not select an analog or digital mode, shape symbols, suppress a carrier,
remove DC, or decide whether the resulting emission is legal.

The direct network TX profile is fixed to:

- mode `iq` and source `iq`;
- one complex channel;
- 48,000 complex samples/s; and
- interleaved signed 16-bit little-endian I then Q (`cs16le`).

The SoapySDR adapter accepts `CF32` or `CS16` at 48 ksample/s, converts it to the
daemon's `cs16le` tunnel, and negates Q at the adapter boundary. `CF32` inputs
use the conventional normalized range of -1.0 through +1.0. Direct `cs16le`
uses the full signed 16-bit range. The daemon applies the selected 1–100%
drive scaling and a non-disableable final complex-magnitude limiter; 100% caps
output at the validated 24,890-count magnitude. Drive percentage is amplitude
control, not calibrated RF wattage.

Before keying, the daemon verifies that the entire theoretical 48 kHz Nyquist
span, plus or minus 24 kHz around the hardware center, fits inside one of its
configured amateur allocations. It rejects raw I/Q on 60 meters and near an
allocation edge. This is only a coarse containment interlock. It does not:

- inspect actual occupied bandwidth or out-of-band energy;
- know the operator's license privileges or geographic band plan;
- identify a modulation or validate symbol rate, deviation, or sidebands;
- guarantee suppression of DC, images, aliases, or unwanted carriers;
- measure transmitted power, SWR, temperature, or spectral purity; or
- make arbitrary client-generated I/Q legal or clean.

## Client emission requirements

A raw-I/Q transmitter should, before supplying samples:

1. Choose an RF center and baseband offset that keep the complete occupied
   emission, filter transition bands, frequency error, and appropriate guard
   margin inside the operator's authorized allocation.
2. Generate at exactly 48 ksample/s or perform controlled resampling with an
   anti-alias filter. Do not merely relabel samples made at another rate.
3. Band-limit the waveform for its intended mode. Keep meaningful energy away
   from the plus/minus 24 kHz Nyquist edges.
4. Remove unintended DC and account for the fact that baseband DC produces RF
   energy at the hardware center frequency.
5. Keep complex magnitude within the normalized range before the daemon's
   limiter. A continuously active limiter changes the waveform and can create
   additional spectral products.
6. Ramp burst and PTT envelopes or otherwise maintain phase/amplitude
   continuity. Abrupt sample discontinuities produce wideband splatter.
7. Monitor the result independently with suitable RF instruments or another
   receiver, beginning into a suitable dummy load.
8. Stop PTT and release the stream through the normal ownership API. Do not
   treat a lease, watchdog, limiter, or frequency check as authorization or as
   a substitute for station supervision.

Silence is zero I and zero Q. The daemon and adapter intentionally do not turn
zero I/Q into a Tune carrier: zero samples are valid content in an ordinary
raw stream. Use the dedicated, separately documented Tune API when a validated
fixed carrier is required.

Do not feed an `F15I` receive recording directly into TX. It has the native RX
orientation, float payload framing, receiver noise, and potentially signals
across the complete passband. A deliberate replay transmission would require
an explicitly reviewed converter, authorization for the content and spectrum,
level control, filtering, and dummy-load validation.

## Related documents

- [Network API](NETWORK_API.md)
- [SoapySDR adapter](SOAPYSDR.md)
- [General TX/PTT API design](GENERAL_TX_API_DESIGN.md)
- [Transmit operator guide](TX_OPERATOR_GUIDE.md)
- [Hardware safety](HARDWARE_SAFETY.md)
