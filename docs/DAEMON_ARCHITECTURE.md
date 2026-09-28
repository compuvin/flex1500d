# flex1500d daemon architecture

`flex1500d` is the single process that owns the FLEX-1500 USB device, applies
radio-control policy, and exposes receive and explicitly enabled transmit
services to network clients. It is live-tested beta software. The normal
configuration is receive-only with tuning enabled; transmit facilities exist
only when the operator deliberately selects `mode=transmit`.

This document describes component boundaries and runtime data flow. Wire-level
routes and formats are documented in the [network API](NETWORK_API.md), while
operating and safety procedures are in the
[transmit operator guide](TX_OPERATOR_GUIDE.md).

## Process overview

```text
                              +---------------- browser test page
                              |
FLEX-1500                     |  HTTP API / F15I IQ / leased TX
 USB  <->  USB backend  <->  daemon event loop  <-> network clients
                              |                         |
                              |                         +-- SoapySDR adapter
                              |                         +-- companion client
                              |                             -> PipeWire/Hamlib
                              |
                              +---------------- rtl_tcp compatibility client
```

The daemon is deliberately the only component that understands the radio's
reverse-engineered USB protocol. The browser page, SoapySDR adapter,
`rtl_tcp` clients, and companion client use network interfaces and never open
the FLEX-1500 directly.

## Configuration and operating modes

Startup policy is assembled from conservative built-in defaults, an optional
configuration file, and command-line overrides. The current modes are:

- `offline`: serve offline API/UI responses without opening USB;
- `receive`: initialize the radio and publish receive I/Q without radio tuning;
- `rx-tuning`: add receive-frequency, filter, and gain control; and
- `transmit`: retain receive operation while preparing the PA path and enabling
  guarded Tune, physical-microphone, audio, and raw-I/Q transmit facilities.

The packaged and built-in default is `rx-tuning`. Configuration parsing and
validation live in `src/config.c`; the full precedence and option contract is
documented in [configuration](CONFIGURATION.md). Legacy exact-mode commands
remain supported, but they enter the same runtime implementation.

## Component boundaries

### Main daemon and event loop

`src/flex1500d.c` owns process lifetime and concrete resources:

- HTTP and optional `rtl_tcp` listening sockets;
- accepted HTTP, IQ-stream, TX-stream, and `rtl_tcp` client sockets;
- the libusb receiver instance and its recovery lifecycle;
- runtime status snapshots and console diagnostics;
- station ownership, Tune leases, network-TX sessions, and TX state; and
- `SIGINT`/`SIGTERM` shutdown and final safe cleanup.

The event loop pumps libusb completions, advances ownership and TX watchdogs,
accepts and dispatches network requests, feeds transmit samples, and publishes
receive samples. Network sockets are nonblocking, so a slow or disconnected
consumer does not stop USB reception or other clients.

### API policy controller

`src/api.c` parses API requests, validates values and capabilities, applies
station-ownership policy, and produces HTTP/JSON responses. It is independent
of libusb and socket creation. Hardware operations cross narrow callbacks
provided by the live daemon, including receive tuning and receive gain.

The same controller is used by offline tests with no hardware callbacks. This
keeps route and safety-policy tests deterministic and prevents an offline
server from reaching the radio accidentally.

### USB and protocol layers

`src/usb_rx.c` owns live device I/O for USB `2192:1502`. It claims interface 3,
sends the understood endpoint-`0x04` control commands, schedules receive and
transmit transfers, and maintains radio and USB diagnostics. `src/protocol.c`
contains command encoding, filter mapping, band-policy helpers, and physical
input decoding.

All command and stream operations cross the mockable interface in
`src/usb_io.c`. Production delegates to libusb; offline failure-injection tests
substitute deterministic command, stream-start, stream-service, and cleanup
results. See [USB I/O mocking](USB_IO_MOCKING.md).

### Receive processing and publication

`src/iq.c` decodes the radio's signed 16-bit little-endian complex samples,
tracks raw statistics and sentinel frames, applies adaptive DC removal, and
places complex-float samples in the bounded receive ring. `src/publisher.c`
encodes versioned `F15I` frames for network delivery.

`src/dsp.c` provides the host-side AM, FM, USB, and LSB demodulation used by
offline tools and the browser path. Demodulation mode and bandwidth are host
state; selecting them does not send a demodulation-mode command to the radio.

### Transmit control and signal generation

Transmit responsibilities are divided so that authorization, lifecycle, DSP,
and USB scheduling can be tested separately:

- `src/station_owner.c` implements the persistent first-station control lease;
- `src/tx_control.c` implements exclusive TX ownership and the startup,
  receive, preparing, transmitting, unkeying, recovery, and fault states;
- `src/tune_control.c` adds the dedicated Tune lease and hard-limit watchdog;
- `src/network_tx.c` manages leased audio/raw-IQ sessions, stream attachment,
  prebuffer readiness, data activity, and disconnect/expiry cleanup;
- `src/tx_dsp.c` converts AM/USB/LSB audio to complex I/Q; and
- `src/tx_audio_stream.c` applies gain, optional speech compression, limiting,
  metering, bounded buffering, fade-out, and graceful draining.

All normal TX sources converge on the shared controller before the USB backend
can key the radio. Standalone research probes are separate executables; the
daemon never invokes them.

### Compatibility front ends

The SoapySDR module in `soapy/Flex1500Device.cpp` is a client of the daemon's
HTTP/IQ API, not an in-process radio driver. It maps Soapy discovery, tuning,
gain, RX streaming, and optional TX streaming to daemon operations.

The optional `rtl_tcp` listener is implemented by `src/rtl_tcp.c` and the main
event loop. It is receive-only, converts the fixed 48 ksample/s radio stream to
unsigned 8-bit I/Q, and uses filtered compatibility resampling for requested
display rates. Its displayed bandwidth does not increase the FLEX-1500's
48 kHz RF window; see [`rtl_tcp` compatibility](RTL_TCP.md).

The companion client under `client/` is a separate executable. It consumes
the daemon API remotely and presents PipeWire RX/TX audio plus a loopback
Hamlib NET rigctl service to local applications. It is intentionally outside
the daemon process and never accesses USB. See the
[companion client guide](../client/README.md).

## Receive data path

```text
FLEX-1500 endpoint 0x82
        |
        v
asynchronous libusb IN scheduler
        |
        v
IQ16LE decode + statistics + sentinel detection
        |
        v
adaptive DC subtraction
        |
        v
bounded 48 ksample/s complex-float ring
        |
        v
F15I framing and fan-out
        |
        +-- up to four HTTP IQ consumers
        +-- optional rtl_tcp consumer and rate conversion
```

The ring has a one-second capacity of 48,000 complex samples. If production
outruns delivery, new samples are dropped and counted rather than silently
overwriting unread samples. Each HTTP IQ consumer has an independent socket;
a stalled or disconnected consumer is removed without stopping the others.
The first receive consumer after an idle period discards stale buffered I/Q so
it begins near live radio time.

Receive tuning sends the mapped frequency and RX-filter commands as one policy
operation. After a successful retune, queued pre-tune I/Q is cleared while
connected streams remain open. The physical RF center is shared; secondary
clients may tune and demodulate only within the controlling station's 48 kHz
window without moving the hardware.

The native API preserves the radio's established complex-sample orientation.
Compatibility boundaries that require conventional orientation—currently
SoapySDR, `rtl_tcp`, and the companion client—apply the documented Q correction
at their boundary. A coordinated native-orientation migration remains a
separate future task.

## Physical inputs

A continuous interrupt-IN listener on endpoint `0x83` decodes active-low
physical microphone PTT, FlexWire PTT, dot, and dash state. The daemon reports
the latest state and transition counters through the API.

In receive-only modes all four inputs are observational. In transmit mode,
physical microphone PTT enters the shared TX state machine after startup,
frequency, mode, band-policy, and prepared-hardware checks pass. It is the
reviewed local-priority exception to network TX ownership: it uses the current
station settings without revoking the persistent station-control lease.
FlexWire PTT, dot, and dash remain observational.

## Ownership and tuning model

The first TX-capable client to acquire station control receives a renewable
15-second lease. While held, that station controls physical frequency, mode,
gain, TX settings, Tune, and general network TX. A foreign client cannot retune
the hardware or transmit; it may continue receiving and select a virtual
frequency inside the owner's current 48 kHz I/Q window.

The ownership lease is persistent across individual transmissions. It ends
only through explicit release, lease expiry/disconnect handling, daemon USB
recovery, or process shutdown. A mode-unaware owner such as the bundled Soapy
adapter is recorded as such so physical-microphone PTT can derive the normal
USB/LSB choice from the hardware frequency. Leases are coordination and safety
mechanisms, not authentication credentials.

Tune, physical microphone, and network audio/raw-IQ sessions additionally
compete for the one shared transmitter state machine. Two network sources
cannot key simultaneously.

## Transmit data and lifecycle

```text
PCM16 audio ----> mode DSP ---+
                              |
complex IQ16 ---------------->+-> gain/limit/buffer -> endpoint 0x01 scheduler
                              |                          |
physical microphone audio --->+                          +-> endpoint 0x04
                                                            key/filter control
```

Network TX requires a valid station owner, a leased session, an attached data
stream, and a 4,096-frame prebuffer before PTT can start. Audio profiles pass
through AM/USB/LSB DSP; raw-I/Q profiles remain bounded by the same output
limiter and drive setting. The SoapySDR TX channel and companion-client audio
path use this leased network scheduler.

Normal PTT release stops accepting new samples, drains queued meaningful audio
and submitted USB data, applies the end fade, and unkeys when empty. The drain
is bounded to one second; any remainder is discarded and reported. Watchdog,
disconnect, USB error, maximum-key, shutdown, and emergency paths unkey
immediately instead of waiting for buffered audio.

The shared maximum-key timer defaults to 180 seconds and cannot be disabled.
Tune has its own renewable lease and 60-second hard limit. Shutdown and partial
startup failure attempt transition mute, unkey, receive-frequency restoration,
PA-filter reset, amplifier disable, transfer cancellation, interface release,
and device close. Counters expose starts, stops, watchdog activity, rejected
ownership, queue depth, graceful drains, underruns, limiting, dropped samples,
and cleanup failures.

## USB failure recovery

If USB processing stops or the device disappears, the daemon:

1. closes receive and TX stream clients;
2. invalidates network TX and station-control state;
3. performs immediate TX cleanup and stops the USB backend;
4. retries reopening the radio once per second for up to 60 attempts; and
5. restores the previous receive gain and known physical frequency.

In transmit mode, recovery must also prepare the TX hardware again before the
state machine is re-armed. The configured maximum-key timeout and accumulated
TX diagnostics survive recovery. IQ rings are cleared so clients cannot receive
stale pre-disconnect samples. Some USB disconnects do not re-enumerate the
FLEX-1500 without a physical power cycle; this is reported explicitly.

## Security boundary

The HTTP API and optional `rtl_tcp` listener can bind to the LAN, but API
version 1 has no authentication or transport encryption. Station and TX leases
do not identify a person and must not be treated as security controls. Bind or
firewall the listeners for trusted hosts only and never expose them directly to
the Internet. The proposed security separation is documented in
[API authentication design](API_AUTHENTICATION_DESIGN.md).

## Offline operation and verification

Status inspection, capture analysis, framing, demodulation, configuration
validation, the offline HTTP server, and the automated test suite do not open
the radio. Hardware-independent policy, DSP, ownership, lifecycle, network,
and injected USB-failure behavior are exercised offline. Live commands and
configuration modes are the explicit boundary that opens and changes radio
state.

The current default suite contains 54 offline tests; enabling the optional thin
client adds three more for a total of 57. Live validation records and
operator-observed results remain in the focused documents linked from the main
README rather than being embedded as historical milestones here.
