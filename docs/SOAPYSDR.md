# Receive-only SoapySDR adapter

The `flex1500Support` module presents the receive side of a running
`flex1500d` daemon as a SoapySDR device. It is an API client: it does not link
to libusb, enumerate USB devices, initialize the radio, or bypass the daemon's
safety boundary.

```text
FLEX-1500 -> flex1500d -> HTTP/F15I API -> flex1500Support -> SDR application
```

## Current contract

- driver name: `flex1500`
- one RX channel and zero TX channels
- fixed 48,000 complex samples per second
- native `CF32` samples, with `CS16` also available
- center frequency from 100 kHz through 54 MHz
- one stream, matching the daemon's current one-client limit
- daemon host defaults to `127.0.0.1`, port defaults to `15000`

The adapter verifies that `/v1/radio` reports `receive_only: true` and
`transmit_enabled: false` before creating a device. It exposes no TX stream,
PTT, MOX, or other transmit control. SoapySDR supports transmit in its general
API, but this module will not advertise it unless a future, separately reviewed
daemon TX API is deliberately implemented.

## Build

On Ubuntu, install the standard adapter dependencies:

```sh
sudo apt install libsoapysdr-dev soapysdr-tools
```

The normal project configuration builds both `flex1500d` and the SoapySDR
adapter. Configuration reports a clear error when the SoapySDR development
files are missing rather than silently producing a daemon without application
compatibility. To explicitly request a constrained daemon-only build:

```sh
cmake -S . -B build -DFLEX1500_BUILD_SOAPYSDR=OFF
```

Build and test both the daemon/API and SoapySDR adapter without opening the
radio:

```sh
cmake -S . -B build -DFLEX1500_BUILD_SOAPYSDR=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The adapter test starts a loopback mock daemon and supplies synthetic `F15I`
samples. It checks discovery, the receive-only channel count, sample rate,
frequency tuning, stream activation, and decoded samples without USB access.

## Development-tree testing

Point SoapySDR at the build directory containing `libflex1500Support.so`:

```sh
SOAPY_SDR_PLUGIN_PATH="$PWD/build" \
  SoapySDRUtil --find="driver=flex1500,host=127.0.0.1,port=15000"

SOAPY_SDR_PLUGIN_PATH="$PWD/build" \
  SoapySDRUtil --probe="driver=flex1500,host=127.0.0.1,port=15000"
```

Discovery succeeds only while a compatible daemon is listening. An offline
daemon is sufficient for discovery and probing, but its IQ endpoint returns
503, so applications cannot activate a sample stream. Live receive still
requires KB1JDX's separately authorized radio initialization and tuning flow.

## Installation

After a normal build, install the module into SoapySDR's ABI-specific module
directory with the standard CMake install target:

```sh
sudo cmake --install build
SoapySDRUtil --check=flex1500
```

Installation changes host files only. Starting `flex1500d` remains a separate
operation, and the SoapySDR module never starts or opens the radio itself.

## First live validation

Date: 2026-08-29

With KB1JDX's explicit approval, the receive-only daemon initialized the
FLEX-1500 and enabled its RX-tuning API. Development-tree SoapySDR discovery
and probing reported one RX channel, zero TX channels, `CF32`/`CS16` formats,
the 100 kHz–54 MHz range, and the fixed 48 kHz sample rate.

A bounded SoapySDR client selected exactly 10.000 MHz, activated a `CF32`
stream, and consumed exactly 48,000 complex samples with nonzero mean power.
The daemon reported RX filter 5, one successful tune, zero radio-command
errors, and zero USB transfer, packet-status, or packet-length errors. It
stopped cleanly afterward. No TX/PTT operation or transmit sample occurred.

This validates the adapter-to-daemon control and IQ paths with live hardware.

After installing the module with `cmake --install`, KB1JDX also successfully
discovered and used the FLEX-1500 from SDR Oxide. This is the first validation
with an established SoapySDR-compatible application. Broader application
compatibility and behavior over longer sessions remain future work.
