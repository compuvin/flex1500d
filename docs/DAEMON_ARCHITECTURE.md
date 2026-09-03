# flex1500d receive architecture

## Internal boundaries

The HTTP-facing policy is implemented by the radio-independent API controller
in `src/api.c`. It owns route dispatch, receive-mode state, capability gates,
and HTTP/JSON responses. It has no libusb or socket dependency.

The daemon in `src/flex1500d.c` owns LAN sockets, IQ-client lifetime, and
live status snapshots. Live RX supplies the controller one narrow callback for
an approved receive-frequency operation; the callback is the only connection
from API policy to the concrete USB RX backend. The offline server supplies no
radio callback, so its frequency route cannot invoke hardware behavior.

```text
HTTP socket -> API controller -> RX-tune callback -> USB RX backend
                  |
                  +-> response/status policy

USB IQ -> ring/publisher -> IQ client socket
```

Transmit research sources are not linked through the controller, callback, or
daemon. Representative TX/PTT paths remain ordinary 404 responses.

## Current implementation

The daemon executable is intentionally offline by default. It now provides:

- A JSON status representation.
- An offline `iq_s16le` capture-analysis path.
- Incremental raw I/Q statistics.
- Sentinel-frame detection.
- Adaptive DC estimation and subtraction.
- A bounded complex-float ring buffer with drop accounting.

The default, analysis, capture-framing, and offline-server modes do not open
USB. A live receive mode is implemented and has been successfully validated
behind an explicit arming token:

```sh
./build/flex1500d --serve-live-rx PORT --initialize-radio
```

This exact form is permission-gated. Omitting or changing the final token exits
with usage information before libusb is initialized.

## Receive data path

```text
FLEX-1500 endpoint 0x82
        |
        v
USB packet scheduler
        |
        v
iq_s16le decoder, 48 kHz
        |
        +---- raw statistics / sentinel detection
        |
        v
adaptive DC estimator and subtraction
        |
        v
bounded complex-float ring buffer
        |
        +---- status counters
        |
        v
network stream publisher
```

The ring currently uses a one-second capacity of 48,000 complex samples. A
producer that outruns its consumer drops new frames and increments an explicit
counter instead of overwriting unread data silently.

## DC removal

The processor seeds its I and Q estimates from the first sample and updates each
estimate with an exponential coefficient of 0.001. The corrected output is the
raw sample minus the current estimate.

Applied offline to `captures/rx-settled.iq16le`, it produced:

| Measurement | I | Q |
|-------------|---:|---:|
| Raw mean | 98.666394 | 18.124706 |
| Final DC estimate | 98.559044 | 17.991203 |
| Processed mean | -0.009257 | -0.021161 |

All 47,616 frames were processed with zero sentinel frames and zero ring drops.

## Current commands

```sh
./build/flex1500d --status
./build/flex1500d --analyze captures/rx-settled.iq16le
```

Both are offline. `--status` emits the status model intended for the future HTTP
endpoint. `--analyze` runs a raw capture through the receive processor.

## Planned network API

The first network version will keep control and sample transport separate:

- `GET /v1/status`: JSON service, radio, USB, sample, and buffer state.
- `GET /v1/radio`: JSON identity and firmware information.
- `GET /v1/stream/iq`: binary complex-sample stream with an explicit format
  header and sequence numbers.

State-changing control endpoints will not be added until their radio commands
are individually understood and permission-tested. TX/PTT remains absent from
the daemon and API. The successful standalone TX probes are deliberately
separate executables with no daemon command-line or network call path.

## Filter ownership and lifecycle plan

Receive tuning will own the receive preselector (`SET_RX_FILTER`, opcode 1257).
The daemon will select the documented receive-filter band when it applies a
frequency change. Receive startup and shutdown will not write the PA-filter
relay merely to produce a click or force a presumed default.

The PA filter (`SET_PA_FILTER`, opcode 1260) belongs to a future transmit
subsystem. Before that subsystem can be implemented, it must select the proper
PA band before keying, remember whether it changed the relay, and return it to
filter 0 during an orderly full shutdown if it owns that change. The transmit
capture also shows that a safe TX lifecycle requires substantially more than a
PA-filter write; see `PCAP_TRANSMIT.md`. No TX endpoint or live TX path is
currently planned for the receive milestone.

## Live integration boundary

The reusable continuous libusb RX producer is now implemented as
`flex1500_usb_rx`, behind an explicit API boundary:

- Creating, inspecting, and destroying an unstarted receiver is offline.
- `flex1500_usb_rx_start()` is the permission-gated live boundary. It opens only
  `2192:1502`, claims interface 3, sends the fixed opcode-1219 `INITIALIZE`
  packet, queues eight endpoint-`0x82` IN transfers, and queues one continuous
  interrupt-IN listener on endpoint `0x83`.
- Each transfer contains 64 packets. Completed packets are decoded, DC-corrected,
  and pushed into the destination ring before the transfer is resubmitted.
- Packet errors, bytes, resubmissions, submission failures, IQ statistics, ring
  drops, and the last error are available to the future status endpoint.
- Stop cancels pending host-side IN transfers, drains callbacks, releases the
  interface, and closes libusb.

Endpoint `0x83` is decoded as active-low physical mic PTT, FlexWire PTT, dash,
and dot inputs. The backend stores the latest state and counts packets, changes,
and errors. The daemon logs transitions and exposes them through
`GET /v1/radio`. In RX-only mode they remain observational. In explicitly
enabled TX mode, changed microphone-PTT edges enter the exclusive physical-mic
state machine after startup, mode, and frequency interlocks pass; FlexWire,
dash, and dot remain observational.

The live daemon test on September 1, 2026 observed mic PTT press as raw `0x38`
and release as raw `0x39`. The API subsequently reported known, released state
for all four inputs, `transmit_enabled` remained false, and shutdown completed
normally without keying the radio.

The live start API is wired only to `--serve-live-rx PORT --initialize-radio`.
It listens on all IPv4 interfaces, opens the radio, sends the fixed initialization
packet, and services continuous IN transfers. `SIGINT` and `SIGTERM` cancel the
host transfers and release the USB interface.

The event loop accepts status requests and at most one IQ streaming client.
The IQ socket is nonblocking. It drains as many frames as possible after each
USB completion, preserves partially written frames across backpressure, and
closes a disconnected client without stopping radio reception. The bounded ring
continues to account for samples dropped when no client can keep up.

The first live daemon run and IQ-client validation completed successfully. The
HTTP loop now preserves a partially received request across nonblocking event
iterations. The read-only API exposes `/v1/status`, `/v1/radio`, and one IQ
stream; status distinguishes USB/ring counters from publisher delivery,
backpressure, disconnect, and write-error counters.

Daemon-managed RX tuning/filter selection is now implemented behind a new exact
arming token and remains unexecuted. The old initialization-only command retains
its original behavior. The next receive milestones are live validation of RX
tuning, LAN binding with an explicit access policy, a compatibility adapter,
and live demodulated-audio streaming. Each radio-state-changing integration or
live validation remains subject to KB1JDX's explicit permission.

## Transmit boundary

Native Linux TX switching, fixed test waveforms, and the PowerSDR-compatible
Tune carrier have been demonstrated successfully. The basic and RX-tuning
daemon modes remain receive-only. The distinct transmit-enabled mode prepares
the TX amplifier path, tracks the PA filter after a frequency becomes known,
exposes the fixed Tune carrier, and connects physical microphone PTT to live
USB/LSB modulation through the shared TX controller. Arbitrary network TX
samples, HTTP PTT, and SoapySDR TX remain unavailable.

The guarded TX probes remain research/validation tools and are included in the
standard build. `FLEX1500_BUILD_TX_RESEARCH=OFF` omits those standalone
executables without changing daemon capabilities. Probe execution remains
separately armed and entirely at the operator's own risk.
