# flex1500d

> **Experimental software — use with care.** This project is an early,
> reverse-engineered Linux implementation for the FlexRadio FLEX-1500. It is
> not affiliated with, endorsed by, supported by, or maintained by FlexRadio
> Systems. It may contain protocol errors and is not a replacement for
> PowerSDR. Back up expectations accordingly and review the hardware-safety
> documentation before allowing any program to open the radio.

`flex1500d` turns a FLEX-1500 USB software-defined radio into a receive-only
IQ source exposed through a local network API:

```text
FLEX-1500 -> libusb -> flex1500d -> loopback HTTP/IQ API -> SDR client
```

The current daemon receives 48 kHz complex I/Q, tunes from 100 kHz through
54 MHz, selects the mapped hardware RX filter, reports detailed USB/stream
status, and serves one IQ client. An opt-in browser test page provides AM, FM,
USB, LSB, and CW audio.

## Project goals

The project began with a simple idea: keep the FLEX-1500 useful without needing
Windows, Wine, or a virtual machine. This checklist is the working roadmap:

- [x] Communicate with the FLEX-1500 directly from Linux over USB.
- [x] Initialize the radio, select a receive frequency, and receive its signals
  without PowerSDR.
- [x] Stream the radio's raw receive signal through a documented local API.
- [x] Provide a simple browser page for tuning and listening in AM, FM, USB,
  LSB, and CW.
- [ ] Make receive operation dependable enough for regular station use,
  including clearer diagnostics and recovery after errors or disconnects.
- [ ] Package `flex1500d` as a proper Linux background service that can start
  automatically and shut the radio down cleanly.
- [ ] Securely expose the API to other computers on the local network, turning
  a USB-connected FLEX-1500 into a practical network-accessible SDR.
- [x] Build an initial SoapySDR compatibility adapter so established SDR
  applications can connect to the `flex1500d` API and use the FLEX-1500 for
  receive; first validated with SDR Oxide.
- [ ] Test and refine compatibility with additional established SDR
  applications and add other adapters where they provide useful coverage.
- [ ] Add normal receiver controls such as adjustable filter bandwidth, gain,
  squelch, and improved audio/DSP behavior.
- [ ] Support more than one useful client or consumer without interrupting the
  receive stream.
- [ ] Continue documenting the reverse-engineered FLEX-1500 protocol so other
  amateur-radio operators and developers can reproduce and improve the work.
- [x] Keep transmit disabled in the current daemon and API while preserving
  the isolated experimental findings for future research.
- [ ] Before considering any future daemon transmit support, thoroughly
  understand and independently review its RF behavior, interlocks, failure
  handling, and safe unkeying.

In other words, the long-term goal is not merely a browser page. An operator
should eventually be able to start `flex1500d`, open an established SDR
application, select the FLEX-1500 through a compatibility adapter, and use the
radio without that application needing to understand the FLEX-1500's USB
protocol.

## Important safety boundary

The daemon and API are intentionally receive-only:

- no PTT or MOX route;
- no TX sample producer or endpoint-`0x01` output scheduler;
- no network transmit endpoint;
- `GET /v1/radio` reports `transmit_enabled: false`;
- representative TX/PTT requests are tested to return 404.

The repository retains minimally tested standalone transmit-research sources
for protocol documentation and reproducibility. They are excluded from the
default build and are never called by the daemon. Their arming strings are
safety interlocks, not authentication or access control. Do not execute a TX
probe on an antenna. Any deliberate experiment requires a suitable 50-ohm
dummy load, an independently reviewed fixed test plan, and explicit
authorization. A dummy load alone does not make an unreviewed test safe.

See [hardware safety](docs/HARDWARE_SAFETY.md) and
[transmit-research safety](docs/TRANSMIT_RESEARCH_SAFETY.md).

## Current capabilities

- Native Linux userspace USB transport using `libusb-1.0`
- Tested FLEX-1500 identity: USB `2192:1502`, firmware `0.5.3.24`
- Receive-only 48 kHz complex-I/Q streaming
- RX frequency and hardware-filter control from 100 kHz to 54 MHz
- Loopback HTTP API bound to `127.0.0.1`
- Versioned binary `F15I` IQ framing
- Detailed USB, sample, ring-buffer, network, and tuning counters
- Offline IQ inspection, framing, AM/FM/USB/LSB demodulation, and WAV output
- Opt-in browser RX page with AM, FM, USB, LSB, and CW audio
- Receive-only SoapySDR adapter for established SDR applications
- Guarded hardware probes that remain offline unless given an exact execution
  argument

## Current limitations

- Only the FLEX-1500 hardware and firmware listed above have been tested.
- The API is local-only; there is no authentication or remote-LAN binding.
- Only one IQ stream client is supported at a time.
- The browser page is a development harness, not the long-term user interface.
- DSP filters and gain controls are not yet generally adjustable.
- The initial SoapySDR adapter has been validated with live radio data and SDR
  Oxide, but broader application compatibility still needs testing; there is
  no Hamlib or other adapter yet.
- There is no installer, systemd unit, or background-service configuration;
  the daemon currently runs in the foreground.
- Transmit is unsupported and intentionally disabled in the daemon/API.

## Requirements

The initial development and testing platform is Ubuntu Linux. Install the
compiler, CMake, pkg-config, libusb, and SoapySDR development files:

```sh
sudo apt update
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev \
  libsoapysdr-dev soapysdr-tools
```

The browser test page additionally requires a browser with `AudioWorklet` and
a 48 kHz `AudioContext`.

## Build and test

The normal build excludes all TX-owned research executables:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The clean default configuration currently runs 18 offline tests. Building and
testing does not enumerate, open, initialize, tune, or otherwise access the
radio. An explicit daemon-only build runs the original 17-test set.

The standard build produces both the API daemon and the receive-only
`flex1500Support` module. It runs an eighteenth test against a synthetic
loopback daemon. See [the SoapySDR adapter guide](docs/SOAPYSDR.md).

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

Initialization and receive streaming without API tuning:

```sh
./build/flex1500d --serve-live-rx 15000 --initialize-radio
```

Initialization plus receive-frequency/filter control:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-rx-tuning
```

Both commands bind only to `127.0.0.1`. Stop the foreground daemon with
Ctrl+C. They do not install or start a system service.

## Browser receive test

Start the tuning-enabled daemon with the separately opt-in test page:

```sh
./build/flex1500d --serve-live-rx 15000 \
  --initialize-radio-and-enable-rx-tuning \
  --enable-test-page
```

Then open:

```text
http://127.0.0.1:15000/test
```

The page consumes only the public API. It does not access USB directly and
contains no TX/PTT control. Changing frequency closes the current IQ stream;
press **Start audio** again after tuning. See
[the browser test-page documentation](docs/WEB_TEST_UI.md).

The page can be served without opening a radio for UI testing:

```sh
./build/flex1500d --serve-offline 15001 --enable-test-page
```

## API summary

The current API is HTTP version 1 on loopback:

| Method and path | Purpose |
| --- | --- |
| `GET /v1/status` | Daemon, USB, buffer, and network counters |
| `GET /v1/radio` | Radio identity, RX state, mode, and TX-disabled status |
| `GET /v1/stream/iq` | Versioned 48 kHz complex-float IQ stream |
| `PUT /v1/radio/frequency/HZ` | Tune RX and select its hardware filter |
| `PUT /v1/radio/mode/MODE` | Record host DSP mode: AM/FM/USB/LSB/CW |
| `GET /test` | Opt-in development page when explicitly enabled |

Frequency control is available only with the exact tuning-enabled daemon
command. Mode selection is host-side state and does not send a mode command to
the radio. The complete response schema and `F15I` framing are documented in
[the network API reference](docs/NETWORK_API.md).

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

## Transmit research sources

Most users should not build this code. It is retained so the experimental
protocol work remains reviewable. The explicit research configuration is:

```sh
cmake -S . -B build-tx-research \
  -DFLEX1500_BUILD_TX_RESEARCH=ON
cmake --build build-tx-research
ctest --test-dir build-tx-research --output-on-failure
```

CMake emits a safety warning when this option is enabled. It creates separate
PA-filter, zero-IQ, and two-tone research programs; it does not add transmit
capability to `flex1500d` or the API. Do not infer permission to execute a
hardware probe merely because its source or binary is present.

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
