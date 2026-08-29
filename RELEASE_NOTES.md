# Release notes

## v0.1.0 — initial experimental receive release

Planned tag: `v0.1.0`

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

### Tagging status

The version is selected, but the `v0.1.0` Git tag will be created only after
the initial commit has been reviewed and approved by KB1JDX.
