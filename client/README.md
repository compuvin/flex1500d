# flex1500-client

`flex1500-client` is the initial implementation of the project's thin
companion bridge. It runs on a computer other than the one attached to the
FLEX-1500 and communicates only with the documented `flex1500d` network API.
It never opens the radio's USB device.

The current first stage connects to a daemon, logs essential daemon/radio
identity, acquires station control when available, renews that lease, and
releases it during a normal shutdown. When another station owns control, it
remains connected in receive-only mode and periodically retries ownership.
It also consumes the daemon's framed 48 kHz IQ stream, demodulates AM, FM,
USB, or LSB with the project's existing receive DSP, and publishes mono
48 kHz audio through a PipeWire source named `FLEX-1500 RX`. CW currently uses
the USB demodulator pending a dedicated client-side CW receive path. At the
client boundary, Q is negated to convert the FLEX/native API orientation to
the conventional spectrum orientation also used by the SoapySDR and rtl_tcp
adapters.

The client also listens on `127.0.0.1:4532` with an initial receive-only
subset of Hamlib's `rigctld` text protocol. It supports frequency and mode
queries and ownership-authorized changes for AM, FM, USB, LSB, and CW. Mode
requests carry Hamlib's passband value into the daemon's receive-bandwidth
state; the companion's audio DSP currently continues to use its validated
mode-default filter. The capability handshake required by Hamlib NET rigctl
clients such as WSJT-X is supported. PTT status reports receive, and PTT-on requests return
Hamlib's not-implemented error until the separately reviewed transmit-audio
stage exists. The initial server accepts one local rig-control connection at a
time and is deliberately bound only to loopback.

Building the client requires the PipeWire development package in addition to
the daemon's normal build dependencies:

```sh
sudo apt install libpipewire-0.3-dev
```

```sh
cmake -S . -B build-client -DFLEX1500_BUILD_CLIENT=ON
cmake --build build-client --parallel
./build-client/client/flex1500-client --host 192.168.1.116
```

The rig-control port defaults to 4532 and can be changed when necessary:

```sh
./build-client/client/flex1500-client --host 192.168.1.116 \
  --rigctl-port 4533
```

Hamlib applications should select **NET rigctl** (model 2) and use
`127.0.0.1:4532` as the rig device. Command-line interoperability checks are:

```sh
rigctl -m 2 -r 127.0.0.1:4532 f
rigctl -m 2 -r 127.0.0.1:4532 m
```

Stop it with `Ctrl+C`; a held station-control lease is explicitly released.
The PipeWire source exists only while the client is running and appears in
`wpctl status`, PipeWire-aware applications, and PulseAudio-compatible
applications. The current program does not yet provide gain or squelch through
rig control, an adjustable local DSP filter, a transmit audio sink, or
transmit PTT.

Future development is described in the
[companion bridge design](../docs/COMPANION_CLIENT_DESIGN.md). Planned stages
include additional receiver controls and—after separate safety review—the
existing ownership-aware TX API.

The client is enabled explicitly with `FLEX1500_BUILD_CLIENT=ON`; the daemon's
normal build and Debian package do not include it. A separate client-only
package can be introduced once the bridge is useful outside development.
