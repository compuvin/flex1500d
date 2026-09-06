# Release notes

## v0.2.0 — experimental network SDR and transmit release

This experimental release expands `flex1500d` from its initial receive-only
foundation into a practical trusted-LAN SDR with guarded transmit support. It
is intended for technically experienced amateur-radio operators who can
independently monitor frequency, modulation, power, and load conditions.

The project remains unaffiliated with, unendorsed by, and unsupported by
FlexRadio Systems. The network API is unauthenticated and unencrypted and must
not be exposed to an untrusted network or the public internet.

### Highlights

- Native Linux control and continuous 48 ksample/s receive I/Q for the
  FLEX-1500, tested with firmware `0.5.3.24`
- LAN-accessible HTTP API and browser test receiver
- SoapySDR RX/TX adapter, live validated with SDR Oxide
- AM, FM, USB, LSB, and CW receive DSP with adjustable bandwidth and squelch
- Five hardware receive gain/attenuation settings
- Up to four concurrent IQ consumers
- Persistent first-client station ownership across HTTP and SoapySDR
- Receive-only secondary clients with independent tuning inside the primary
  station's plus/minus 24 kHz IQ window
- Hardware frequency, band, TX setting, and transmit rejection for secondary
  clients
- Physical microphone PTT with local priority, USB/LSB audio conversion,
  metering, clipping protection, optional compression, and maximum-key timer
- HTTP PCM-audio and raw-I/Q TX plus SoapySDR raw-I/Q TX
- Capture-matched 5 W Tune carrier
- Frequency-allocation checks, PA-filter tracking, exclusive TX operations,
  prebuffer/data watchdogs, disconnect cleanup, and USB recovery diagnostics

HTTP-primary/Soapy-secondary and Soapy-primary/HTTP-secondary ownership were
both live validated. Soapy TX required both the persistent station-control
lease and subordinate TX-operation lease throughout stream connection, PTT,
keepalive, and release; the complete lifecycle is now covered by the offline
mock-daemon test.

### Experimental Debian package

The release includes an experimental `amd64` Debian package built in Release
mode. It installs:

- the `flex1500d` daemon under `/usr/bin`;
- `libflex1500Support.so` in SoapySDR's module directory;
- the FLEX-1500 udev access rule; and
- the README, release notes, and GPLv3 license under `/usr/share/doc`.

The package declares its shared-library dependencies automatically. It does not
install or start a systemd service. It is a convenience package, not a
distribution-neutral installer; users on other architectures or incompatible
Debian-family releases should build from source. The project tests source builds
on ARM64 through GitHub Actions, but this release does not include an ARM64
package.

### Important limitations

- TX behavior remains experimental. Use a suitable dummy load for initial
  validation and independently monitor every transmission.
- API version 1 has no authentication or transport encryption.
- SoapySDR TX has been validated with SDR Oxide, not multiple established SDR
  transmit applications.
- Browser microphone capture normally requires localhost or HTTPS.
- There is no packaged systemd service or distribution package yet.
- The FLEX-1500 provides no host-readable forward/reflected power, SWR, or PA
  temperature telemetry; external station instruments remain necessary.
- Daemon-generated AM, FM, CW, and digital-mode TX are not implemented. Raw-IQ
  clients are responsible for the emissions they generate.

### Validation

- Clean Release build with warnings treated as errors and standalone TX
  research probes excluded
- 31/31 offline tests passing in the packaged Release configuration; 48/48 in
  the full development configuration
- No automated test opens, commands, or transmits with a radio
- Live receive, browser audio/microphone TX, physical microphone TX, Tune,
  SoapySDR TX, ownership in both client directions, and secondary-client local
  tuning validated by KB1JDX

## v0.1.0 — initial experimental receive release

This is the first public experimental release of `flex1500d`, a native Linux
userspace implementation for receiving with the FlexRadio FLEX-1500 over USB.
It is intended for developers and technically experienced amateur-radio
operators who understand that the USB protocol was reverse-engineered and that
the software remains under active development.

The project is unaffiliated with, unendorsed by, and unsupported by FlexRadio
Systems.

### Highlights

- Native `libusb-1.0` communication with USB device `2192:1502`
- Tested with FLEX-1500 firmware `0.5.3.24`
- Receive-only 48 kHz complex-I/Q streaming
- RX tuning from 100 kHz through 54 MHz
- Frequency-dependent hardware RX-filter selection
- Loopback HTTP API with radio and detailed service status
- Versioned `F15I` complex-float network framing
- One receive IQ stream client
- Host receive-mode state for AM, FM, USB, LSB, and CW
- Opt-in browser page for tuning and listening
- Offline IQ analysis, framing, demodulation, and WAV output
- Granular USB transfer, packet, sample, buffer, and network diagnostics
- GPL-3.0-only source license and reproducible CMake build

### Receive-only safety boundary

The daemon and API intentionally contain no PTT/MOX route, TX sample producer,
sample-OUT scheduler, or network transmit endpoint. The radio endpoint reports
`transmit_enabled: false`, and representative TX/PTT routes are tested to
remain unavailable.

Minimally tested standalone TX-owned research sources are retained for protocol
review but excluded from the default build. They are not supported transmitter
controls. See `docs/TRANSMIT_RESEARCH_SAFETY.md` before inspecting or building
that research configuration.

### Build and validation

The documented Ubuntu dependencies are:

```sh
sudo apt install build-essential cmake pkg-config libusb-1.0-0-dev
```

The default build is:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Validation for this release includes:

- clean Debug and Release configurations;
- warnings treated as errors;
- 17/17 default receive-only tests passing;
- 22/22 offline/interlock tests passing in the explicit TX-research build;
- a clean source-snapshot build with ignored captures and artifacts absent;
- documentation-link, SPDX, workflow-YAML, whitespace, and artifact audits.

No automated build or test opens or commands the radio.

### Known limitations

- Only FLEX-1500 firmware `0.5.3.24` has been tested.
- The server binds only to `127.0.0.1`; there is no LAN authentication or
  remote access yet.
- Only one IQ client is supported.
- The browser page is a development harness, not a finished radio interface.
- Filter bandwidth, gain, squelch, and other common receiver controls are not
  generally adjustable yet.
- There is no SoapySDR or other established-application adapter yet.
- There is no installer or systemd service; the daemon runs in the foreground.
- Transmit is unsupported and intentionally disabled in the daemon and API.

### Roadmap

The next major goals are dependable everyday RX operation, service packaging,
secure local-network access, established SDR-application compatibility through
an adapter, improved receiver controls and DSP, and support for additional
clients. The live roadmap is maintained in `README.md`.
