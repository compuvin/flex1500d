# flex1500d

> **Experimental software — use with care.** This project is an early,
> reverse-engineered Linux implementation for the FlexRadio FLEX-1500. It is
> not affiliated with, endorsed by, supported by, or maintained by FlexRadio
> Systems. It may contain protocol errors and is not a replacement for
> PowerSDR. Back up expectations accordingly and review the hardware-safety
> documentation before allowing any program to open the radio.

`flex1500d` turns a FLEX-1500 USB software-defined radio into a network SDR
exposed through a local API. It defaults to receive-only operation; separately
enabled transmit support remains experimental:

```text
FLEX-1500 -> libusb -> flex1500d -> HTTP/IQ API -> SDR client
```

The current daemon receives 48 kHz complex I/Q, tunes from 100 kHz through
54 MHz, selects the mapped hardware RX filter, reports detailed USB/stream
status, and serves as many as four simultaneous IQ clients. An opt-in browser
test page provides AM, FM, USB, LSB, and CW audio.

## Project goals

The project began with a simple idea: keep the FLEX-1500 useful without needing
Windows, Wine, or a virtual machine. This checklist is the working roadmap:

- [x] Communicate with the FLEX-1500 directly from Linux over USB.
- [x] Initialize the radio, select a receive frequency, and receive its signals
  without PowerSDR.
- [x] Stream the radio's raw receive signal through a documented local API.
- [x] Provide a simple browser page for tuning and listening in AM, FM, USB,
  LSB, and CW.
- [x] Make receive operation dependable enough for regular station use,
  including clearer diagnostics and recovery after errors or disconnects.
- [x] Package `flex1500d` as a proper Linux background service that can start
  automatically and shut the radio down cleanly.
- [x] Expose the API to other computers on a trusted local network, turning a
  USB-connected FLEX-1500 into a practical network-accessible SDR.
- [ ] Secure remote API access with authentication and transport encryption.
  (See: [API and SoapySDR authentication design](docs/API_AUTHENTICATION_DESIGN.md).)
- [x] Build an initial SoapySDR compatibility adapter so established SDR
  applications can connect to the `flex1500d` API and use the FLEX-1500 for
  receive; first validated with SDR Oxide.
- [ ] Test and refine compatibility with additional established SDR
  applications and add other adapters where they provide useful coverage.
- [x] Add normal receiver controls such as adjustable filter bandwidth, gain,
  squelch, and improved audio/DSP behavior.
- [x] Support more than one useful client or consumer without interrupting the
  receive stream.
- [ ] Continue documenting the reverse-engineered FLEX-1500 protocol so other
  amateur-radio operators and developers can reproduce and improve the work.
  (See: [identified radio features not yet implemented](docs/UNIMPLEMENTED_FEATURES.md)
  and [PowerSDR software feature gaps](docs/POWERSDR_FEATURE_GAPS.md).)
- [x] Keep transmit disabled in the current daemon and API while preserving
  the isolated experimental findings for future research.
- [x] Before considering any future daemon transmit support, thoroughly
  understand and independently review its RF behavior, interlocks, failure
  handling, and safe unkeying.

In other words, the long-term goal is not merely a browser page. An operator
should eventually be able to start `flex1500d`, open an established SDR
application, select the FLEX-1500 through a compatibility adapter, and use the
radio without that application needing to understand the FLEX-1500's USB
protocol.

## Stretch goals

These ideas would make fuller use of the FLEX-1500 hardware, but they are not
requirements for the daemon's initial release or normal network-SDR use:

- [ ] Send host-demodulated receive audio back through USB endpoint `0x01` so
  the radio's physical front-panel headphone jack can be used while the daemon
  is running.
- [ ] Physically validate the front-panel CW key jack's decoded dot and dash
  events, then document straight-key and paddle behavior.
- [ ] Add an optional CW keyer and sidetone path, with reviewed timing,
  transmitter ownership, carrier generation, and safe-unkey behavior.

## Important safety boundary

The basic `--initialize-radio` mode and the configured `receive` and
`rx-tuning` daemon modes remain receive-only:

- no PTT or MOX route;
- no general TX sample-stream endpoint;
- no general remotely controlled PTT or arbitrary-sample transmit endpoint;
- `GET /v1/radio` normally reports `transmit_enabled: false`; and
- representative TX/PTT requests are tested to return 404.

The SoapySDR adapter follows the daemon's capability boundary: it exposes
receive normally and exposes one TX channel only to the station owner when it
connects to an explicitly transmit-enabled daemon.

Transmit operations are exposed only by configured `mode=transmit` or the
legacy `--initialize-radio-and-enable-transmit` live mode. The validated fixed
Tune carrier and physical microphone path share an exclusive owner with the
HTTP PCM-audio and complex-I/Q TX sessions. Network TX adds prebuffer, lease,
sample-data, maximum-key, disconnect, and cleanup watchdogs. HTTP PCM audio and
raw I/Q have completed bounded live dummy-load tests; individual modes, bands,
and applications still require the validation tracked in the engineering
checklist. The interlocks and leases are safety mechanisms, not security
credentials. Use a suitable matched antenna system or 50-ohm dummy load and
follow normal RF exposure and station-control practices.

The standard build includes standalone transmit-research executables for
protocol documentation and reproducibility. They are never called by the
daemon. Their arming strings are
safety interlocks, not authentication or access control. Do not execute a TX
probe on an antenna. Any deliberate experiment requires a suitable 50-ohm
dummy load, an independently reviewed fixed test plan, and explicit
authorization. A dummy load alone does not make an unreviewed test safe.

See [hardware safety](docs/HARDWARE_SAFETY.md) and
[transmit-research safety](docs/TRANSMIT_RESEARCH_SAFETY.md). Before enabling
daemon transmit, read the consolidated
[transmit operator guide](docs/TX_OPERATOR_GUIDE.md), including its emergency
unkey procedure.
The remaining engineering work for general transmit is tracked separately in
[the transmit enablement checklist](docs/TRANSMIT_ENABLEMENT_CHECKLIST.md); it
does not modify the project goals above.

## Current capabilities

- Native Linux userspace USB transport using `libusb-1.0`
- Tested FLEX-1500 identity: USB `2192:1502`, firmware `0.5.3.24`
- Receive-only 48 kHz complex-I/Q streaming
- RX frequency and hardware-filter control from 100 kHz to 54 MHz
- HTTP/IQ API on TCP port 15000, available to local and trusted-LAN clients
- LAN-accessible 5 W Tune API in the transmit-enabled daemon, with
  lease/watchdog cleanup
- Versioned binary `F15I` IQ framing
- Detailed USB, sample, ring-buffer, network, and tuning counters
- Offline IQ inspection, framing, AM/FM/USB/LSB demodulation, and WAV output
- Opt-in browser operator page with AM, FM, USB, LSB, and CW receive audio;
  transmit-enabled mode adds Tune, TX settings, and local-computer microphone
  controls
- Conditional RX/TX SoapySDR adapter for established SDR applications; TX is
  exposed only to the station owner when the daemon is transmit-enabled
- Guarded hardware probes that remain offline unless given an exact execution
  argument

## Current limitations

- Only the FLEX-1500 hardware and firmware listed above have been tested.
- The API accepts trusted-LAN connections but has no authentication or
  transport encryption; it must not be exposed to an untrusted network.
- As many as four IQ stream clients are supported at a time; they share one
  physical center frequency and 48 kHz RF window.
- The browser page is a development harness, not the long-term user interface.
  Computer-microphone TX requires a secure browser context, so plain HTTP works
  on `localhost` but not normally from another LAN computer.
- Receiver filter bandwidth, RF/preamp gain, audio gain, and squelch are
  adjustable through the API and test page; hardware bandwidth and additional
  DSP refinement remain future work.
- The initial SoapySDR adapter has been validated with live radio data and SDR
  Oxide, but broader application compatibility still needs testing; there is
  no Hamlib or other adapter yet.
- The Debian package installs and enables a systemd service for the next boot,
  but deliberately does not start it during package installation. Source-tree
  commands continue to run in the foreground.
- Transmit remains experimental and is unavailable in normal receive-only
  daemon modes. The explicitly transmit-enabled mode supports the paths and
  limitations listed in the [transmit operator guide](docs/TX_OPERATOR_GUIDE.md).

## Requirements

The initial development and testing platform is Ubuntu Linux. Install the
compiler, CMake, pkg-config, libusb, and SoapySDR development files:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev \
  libsoapysdr-dev soapysdr-tools
```

The browser test page requires a browser with a 48 kHz `AudioContext`.
`AudioWorklet` is preferred, with a compatible script-processor fallback.

### Experimental Debian package

GitHub releases may include an experimental `amd64` package. Install a
downloaded package and its declared dependencies with:

```sh
sudo apt install ./flex1500d_0.2.1_amd64.deb
```

The package installs `flex1500d`, the SoapySDR module, the udev access rule, a
default receive-only configuration, a systemd service, and project
documentation. It enables the service for the next boot but deliberately does
not start it during installation. Unplug and reconnect the FLEX-1500 after
installation so the new udev rule is applied. See the
[systemd service guide](docs/SYSTEMD_SERVICE.md) before starting it. Remove the
package with `sudo apt remove flex1500d`.

The package is built for the Ubuntu version used to create the release and may
not run on older Debian-family systems whose glibc or SoapySDR ABI differs.
Build from source when the packaged dependencies are incompatible.

## Build and test

The normal build includes the standalone TX research executables, whose
execution still requires their exact safety arming strings:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The clean default configuration currently runs 54 offline tests. Building and
testing does not enumerate, open, initialize, tune, or otherwise access the
radio.

The standard build produces both the API daemon and the conditional RX/TX
`flex1500Support` module. The adapter exposes TX only when connected to an
explicitly transmit-enabled daemon as the station owner. Its test uses a
synthetic loopback daemon.
See [the SoapySDR adapter guide](docs/SOAPYSDR.md).

> **Development installation:** Rebuilding updates the artifacts under
> `build/` but does not update a Soapy module already installed under
> `/usr/local`. After a Soapy adapter change, fully close SDR applications,
> run `sudo cmake --install build`, verify the loaded module as described in
> the adapter guide, and then reopen the application. Restart a running daemon
> whenever its executable was rebuilt. A live Soapy test is not considered
> valid until the build and installed module have been verified to match. The
> default source install prefix is `/usr/local`; it does not enable or start
> the Debian package's `/usr/bin/flex1500d` systemd service. Use the `.deb` for
> the documented background-service workflow.

SoapySDR can be explicitly omitted for a constrained or daemon-only build with
`-DFLEX1500_BUILD_SOAPYSDR=OFF`; it is included and required by default.

Useful offline checks:

```sh
./build/flex1500-info
./build/flex1500d --status
./build/flex1500d --help
```

`flex1500-info` prints compiled-in protocol information. `--status` prints
offline daemon state. Neither command opens USB.

## USB permissions

The supplied udev rule grants the active local desktop user access only to USB
device `2192:1502`:

```sh
sudo install -m 0644 ./70-flex1500.rules \
  /etc/udev/rules.d/70-flex1500.rules
sudo udevadm control --reload-rules
```

Unplug and reconnect the FLEX-1500 after reloading the rules. Installing the
rule changes host-side device permissions only; it sends nothing to the radio.

To remove it:

```sh
sudo rm /etc/udev/rules.d/70-flex1500.rules
sudo udevadm control --reload-rules
```

Unplug and reconnect the radio again after removal. Additional details are in
[RULE_CHANGES.txt](RULE_CHANGES.txt).

## Run the receive daemon

Live commands open and initialize the radio and therefore change radio state.
Review the command and ensure no other program owns the FLEX-1500 first.

Installed packages provide `/etc/flex1500d/flex1500d.conf`. Its default starts
RX with frequency tuning enabled while leaving transmit and the browser test
page disabled. Start the installed, already-enabled systemd service with:

```sh
sudo systemctl start flex1500d
sudo systemctl status flex1500d
```

If it is not started manually, it starts at the next normal boot. See the
[systemd service guide](docs/SYSTEMD_SERVICE.md) for logs, shutdown, restart,
and configuration procedures.

For a foreground source-tree run using the version-controlled receive-only
configuration:

```sh
./build/flex1500d --config ./flex1500d.conf
```

Stop a foreground daemon with Ctrl+C. The default configuration path can be
replaced with `--config PATH`, and individual settings can be overridden on the
command line. Use `--check-config` or `--print-effective-config` to inspect the
effective configuration without opening the radio. See the
[configuration reference](docs/CONFIGURATION.md).

An experimental receive-only [`rtl_tcp` compatibility listener](docs/RTL_TCP.md)
is available for clients without the project Soapy adapter. It is disabled by
default and uses the FLEX-1500's fixed 48 ksample/s bandwidth.

The earlier exact-mode commands remain supported for compatibility with old
scripts and research notes, but new usage should prefer configuration files or
the equivalent `--radio-mode` override:

Initialization and receive streaming without API tuning:

```sh
./build/flex1500d --serve-live-rx 15000 --initialize-radio
```

Initialization plus receive-frequency/filter control:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-rx-tuning
```

Initialization, receive controls, PA-filter tracking, and the validated Tune
carrier:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-transmit
```

All three compatibility commands listen on TCP port 15000 on all IPv4
interfaces. They run in the foreground and do not install or start a system
service.

The validated capture-matched 5 W Tune API is included only in the transmit-
Configured operation exposes the same Tune API only when `mode=transmit`; the
command and Tune lease are not authentication mechanisms.

> **LAN security:** API version 1 currently has no authentication or transport
> encryption. Permit port 15000 only from trusted local hosts using the daemon
> machine's firewall. Never forward this port from an internet router or expose
> it directly to an untrusted network. Authentication and encryption remain a
> separate project goal.

## Browser receive test

For a foreground source-tree test, enable the page while retaining the
configuration's receive-only `rx-tuning` mode:

```sh
./build/flex1500d --config ./flex1500d.conf --enable-test-page
```

Then open:

```text
http://DAEMON_HOST:15000/test
```

The page consumes only the public API and never accesses USB directly. In
receive-only modes its Tune and TX operations are unavailable. When the daemon
is deliberately configured with `mode=transmit`, the page also exposes Tune,
TX settings, and local-computer microphone controls. Tuning keeps browser audio
connected. See [the browser test-page documentation](docs/WEB_TEST_UI.md).

The page can be served without opening a radio for UI testing:

```sh
./build/flex1500d --config ./flex1500d.conf \
  --radio-mode offline --http-port 15001 --enable-test-page
```

## API summary

The current API is HTTP version 1 on TCP port 15000:

| Method and path | Purpose |
| --- | --- |
| `GET /v1/status` | Daemon, USB, buffer, and network counters |
| `GET /v1/radio` | Radio identity, RX state, mode, and Tune capability/state |
| `GET /v1/stream/iq` | Versioned 48 kHz complex-float IQ stream |
| `POST /v1/control/owner` | Acquire persistent station control; first TX-capable client wins |
| `PUT /v1/control/owner/keepalive` | Renew station control using its control-lease header |
| `DELETE /v1/control/owner` | Relinquish station control while no transmitter is active |
| `PUT /v1/radio/frequency/HZ` | Tune RX and select its hardware filter |
| `PUT /v1/radio/mode/MODE` | Record host DSP mode: AM/FM/USB/LSB/CW |
| `PUT /v1/radio/gain/DB` | Set receive gain: -10/0/10/20/30 dB |
| `PUT /v1/radio/bandwidth/HZ` | Set host DSP bandwidth |
| `PUT /v1/radio/squelch/DBFS` | Set host squelch threshold |
| `PUT /v1/radio/tune/start` | Start Tune in transmit-enabled mode and return its lease |
| `PUT /v1/radio/tune/keepalive/LEASE` | Renew the active Tune lease |
| `PUT /v1/radio/tune/stop/LEASE` | Stop Tune and restore receive operation |
| `PUT /v1/radio/tx-timeout/SECONDS` | Configure the shared 30–1800 second TX timer |
| `PUT /v1/radio/tx-compressor/on|off` | Enable or disable shared TX speech compression while unkeyed |
| `POST /v1/tx/sessions` | Reserve general network TX with an explicit audio or I/Q profile |
| `CONNECT /v1/tx/stream` | Open the leased raw PCM16 or complex-IQ sample tunnel |
| `PUT /v1/tx/ptt/start|stop` | Key or unkey the leased, prebuffered network TX session |
| `DELETE /v1/tx/sessions/current` | Unkey and release the network TX lease |
| `GET /test` | Opt-in development page when explicitly enabled |

In transmit-enabled mode, the first TX-capable browser or SoapySDR station to
connect owns hardware tuning, radio settings, Tune, and network TX until it
disconnects, explicitly releases control, or stops renewing its 15-second
lease. Other clients remain receive-only and can select frequencies within the
owner's 48 kHz IQ window without retuning the radio. Physical microphone PTT
remains the reviewed local-priority exception and acts through the current
station configuration without revoking the network station owner.

Physical frequency control is available when the effective radio mode is
`rx-tuning` or `transmit`; a non-owner remains limited to virtual tuning within
the station owner's 48 kHz I/Q window. Mode selection is host-side state and
does not send a demodulation-mode command to the radio. The complete response
schema and `F15I` framing are documented in the
[network API reference](docs/NETWORK_API.md).

## Offline processing

Analyze a signed-16-bit little-endian IQ file:

```sh
./build/flex1500d --analyze captures/example.iq16le
```

Demodulate it to a mono 48 kHz PCM16 WAV:

```sh
./build/flex1500d --demod captures/example.iq16le usb output.wav
```

Modes are `am`, `fm`, `usb`, and `lsb`; an optional final argument sets
the tuning offset in hertz. See [receive DSP](docs/RECEIVE_DSP.md).

## Transmit status and research tools

The standard build includes the standalone TX research probes. They can be
omitted from a constrained build with:

```sh
cmake -S . -B build -DFLEX1500_BUILD_TX_RESEARCH=OFF
```

The probes retain their exact execution strings to reduce accidental use, but
those strings are not security controls. Building a probe does not execute it.

The daemon's live-tested transmit features require configured
`mode=transmit`, or its legacy `--initialize-radio-and-enable-transmit`
equivalent. This includes the capture-matched nominal 5 W Tune carrier. The
mode also provides receive streaming and controls, prepares the TX amplifier
path, and keeps the PA filter mapped to the known frequency. Physical
microphone PTT and continuous USB/LSB modulation are now connected to the
reviewed ownership and cleanup path. Repeated USB voice tests into a dummy load
validated natural
audio, adequate subjective level, reliable PTT, unkey, and RX restoration;
calibrated modulation/ALC measurement and live LSB validation remain open.
Physical and HTTP PTT are rejected unless TX mode is enabled, startup/recovery
has established an unkeyed prepared state, the frequency is known and
permitted by the daemon's band policy, and the selected profile is valid. HTTP
TX accepts USB/LSB PCM audio or guarded raw complex I/Q. The SoapySDR adapter
maps its TX stream to the same leased raw-I/Q API and exposes TX only to the
station owner when the connected daemon reports transmit enabled.

For a deliberate foreground transmit-enabled source-tree run with the browser
test page:

```sh
./build/flex1500d --config ./flex1500d.conf \
  --radio-mode transmit --enable-test-page
```

This command changes radio state and enables transmit facilities. Review the
transmit operator guide and ensure the station is safe before running it.

Transmit mode and all standalone TX probes are experimental, intended for
testing, and used entirely at the operator's own risk. The operator is
responsible for legal operation, a suitable matched antenna system or dummy
load, RF exposure, interference prevention, and immediately stopping an
unexpected transmission.

## Documentation map

- [Daemon architecture](docs/DAEMON_ARCHITECTURE.md)
- [Network API and IQ framing](docs/NETWORK_API.md)
- [Receive DSP](docs/RECEIVE_DSP.md)
- [SDR compatibility roadmap](docs/SDR_COMPATIBILITY.md)
- [Protocol findings](docs/PROTOCOL_FINDINGS.md)
- [Hardware safety](docs/HARDWARE_SAFETY.md)
- [Transmit-research boundary](docs/TRANSMIT_RESEARCH_SAFETY.md)
- [Windows capture procedure](docs/WINDOWS_CAPTURE.md)
- [Publishing checklist](PUBLISHING_CHECKLIST.md)
- [Release notes](RELEASE_NOTES.md)
- [Project-origin chat](research/Chat.txt)

The detailed probe specifications and historical validation records remain in
the [docs directory](docs/).

## License

Copyrightable project source is licensed under the
[GNU General Public License version 3 only](LICENSE). Source files carry the
SPDX identifier `GPL-3.0-only`. See [NOTICE.md](NOTICE.md) for copyright,
attribution, dependency, and reverse-engineering provenance.

[![CodeFactor](https://www.codefactor.io/repository/github/compuvin/flex1500d/badge)](https://www.codefactor.io/repository/github/compuvin/flex1500d)
