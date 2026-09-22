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
the USB demodulator pending a dedicated client-side CW receive path.

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

Stop it with `Ctrl+C`; a held station-control lease is explicitly released.
The PipeWire source exists only while the client is running and appears in
`wpctl status`, PipeWire-aware applications, and PulseAudio-compatible
applications. The current program does not yet provide receiver controls,
rig-control compatibility, a transmit audio sink, or PTT.

Future development is described in the
[companion bridge design](../docs/COMPANION_CLIENT_DESIGN.md). Planned stages
include receive IQ and local demodulation, PipeWire audio, receiver controls,
a Hamlib-compatible rig-control bridge, and—after separate safety review—the
existing ownership-aware TX API.

The client is enabled explicitly with `FLEX1500_BUILD_CLIENT=ON`; the daemon's
normal build and Debian package do not include it. A separate client-only
package can be introduced once the bridge is useful outside development.
