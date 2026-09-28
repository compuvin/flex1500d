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

The thin client also listens on `127.0.0.1:4532` with a focused subset of
Hamlib's `rigctld` text protocol. It supports frequency and mode queries,
ownership-authorized changes for AM, FM, USB, LSB, and CW, and Hamlib PTT. Mode
requests carry Hamlib's passband value into the daemon's receive-bandwidth
state; the companion's audio DSP currently continues to use its validated
mode-default filter. The capability handshake required by Hamlib NET rigctl
clients such as WSJT-X is supported. The server accepts one local rig-control
connection at a time and is deliberately bound only to loopback. The first
connected application retains that connection; later connection attempts are
rejected rather than displacing the active program.

Hamlib applications commonly query the rig before sending their selected
working frequency. When a newly started daemon has no frequency yet, the
bridge presents the frequency and mode saved at its last orderly shutdown.
The first-run defaults are 28.475 MHz and USB. This local handshake state is
stored under `$XDG_STATE_HOME/flex1500-client/state.conf`, or under
`~/.local/state/flex1500-client/state.conf` when `XDG_STATE_HOME` is unset.
When this client acquires station control, it applies that state to a daemon
whose frequency is unset, so the physical radio returns to the same operating
point. A receive-only secondary client never performs this restore. Subsequent
application frequency and mode commands update the radio and the state that
will be saved at clean shutdown.

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
applications. The client publishes a `FLEX-1500 TX` PipeWire sink and captures
its 48 kHz mono audio into a bounded 4,800-frame (100 ms) local buffer. Hamlib
PTT uses the daemon's existing leased audio session, 4,096-frame prebuffer,
persistent PCM stream with bounded 48 kHz pacing, watchdog, maximum-key timer,
and stop/release cleanup. This path is available only when the daemon was
explicitly started in transmit mode. It has completed clean WSJT-X dummy-load
tests and a 5 W FT8 QSO; see the testing record below. The current program does
not yet provide gain or squelch through rig control or an adjustable local DSP
filter.

Guidance for developers building other thin clients, SDR front ends, or
compatibility bridges is in the
[client integration guide](../docs/CLIENT_INTEGRATION_GUIDE.md).

Recorded offline, dummy-load, and live station results are in
[companion client testing](../docs/COMPANION_CLIENT_TESTING.md).

The client is enabled explicitly with `FLEX1500_BUILD_CLIENT=ON`; the daemon's
normal build and `flex1500d` Debian package do not include it. Releases may
provide a separate `flex1500-client` package for the operator workstation:

```sh
sudo apt install ./flex1500-client_0.2.3_amd64.deb
# or, on ARM64:
sudo apt install ./flex1500-client_0.2.3_arm64.deb
```
