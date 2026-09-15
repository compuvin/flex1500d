# SoapySDR adapter

The `flex1500Support` module presents the receive side of a running
`flex1500d` daemon as a SoapySDR device. It is an API client: it does not link
to libusb, enumerate USB devices, initialize the radio, or bypass the daemon's
safety boundary.

```text
FLEX-1500 -> flex1500d -> HTTP/F15I API -> flex1500Support -> SDR application
```

## Current contract

- driver name: `flex1500`
- one RX channel; a TX channel only when this client owns station control
- fixed 48,000 complex samples per second
- native `CF32` samples, with `CS16` also available
- center frequency from 100 kHz through 54 MHz
- up to four independent daemon IQ consumers
- daemon host defaults to `127.0.0.1`, port defaults to `15000`

For a remote trusted-LAN daemon, supply its hostname or address without changing
the daemon service:

```sh
SoapySDRUtil --find="driver=flex1500,host=radio-pc.local,port=15000"
```

API version 1 is unauthenticated and unencrypted. Limit port 15000 to trusted
LAN hosts with a firewall; do not expose it directly to the internet. SoapySDR
does not define authentication for this daemon protocol, but a future version
of this module can supply API credentials on the application's behalf; see
[API and SoapySDR authentication notes](API_AUTHENTICATION_DESIGN.md).

The FLEX-1500/native `F15I` stream uses the orientation expected by the
project's browser and offline DSP. The adapter negates Q at the SoapySDR
boundary so positive/negative frequencies follow SoapySDR application
conventions; without that conversion, USB and LSB appear reversed. The native
API bytes are intentionally unchanged.

The adapter can attach to either RX-only or explicitly TX-enabled daemon mode.
In TX-enabled mode, the first TX-capable station to connect acquires a
persistent station-control lease and renews it in an independent background
thread. It retains that lease across TX stream activation and deactivation;
device destruction releases it. Other Soapy processes report zero TX channels
and cannot change hardware settings. They may select a receive frequency only
within the owner's current plus/minus 24 kHz IQ window; that shift is applied
locally without retuning the radio.

The owning client reports one `CF32`/`CS16`, 48 ksample/s TX channel and maps it
to the daemon's leased raw-I/Q API. Stream activation reserves the subordinate
TX operation and connects its sample tunnel. After the required 4,096-frame
prebuffer, continuous writes key the transmitter. Deactivation stops PTT,
releases that TX operation, and closes the stream without surrendering station
control. Periodic writes renew the TX-operation lease;
the daemon applies TCP backpressure when its USB/DSP TX ring is full rather
than accepting and discarding a client's queued samples. The adapter paces
post-key writes at the radio's fixed 48 ksample/s rate with a small lead so a
fast client cannot accumulate seconds of stale audio in operating-system socket
buffers. Stalled or disconnected clients remain subject to the daemon's data
watchdog, maximum-key timer, guaranteed-unkey cleanup, drive limiter, and
ownership rules.

Soapy I/Q is conjugated at the adapter boundary, matching the receive-side
orientation correction. TX drive is exposed through Soapy's TX gain control as
an integer percentage from 1 through 100. Raw-I/Q center frequencies are
advertised only where the complete plus/minus 24 kHz span fits within a
configured amateur allocation; the daemon independently rechecks that rule at
PTT start. Soapy timed bursts and `END_BURST` flags are not yet supported;
applications must use continuous streaming and explicitly deactivate TX.
Normal deactivation stops sample acceptance and permits a bounded one-second drain
of meaningful frames already in the daemon's DSP/USB pipeline before unkey.
Safety-driven stops remain immediate.

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
samples. It checks discovery, first-client ownership, second-process
receive-only behavior and local tuning bounds, conditional TX channel counts,
frequency, gain, stream activation, decoded RX samples, leased TX lifecycle, I/Q
orientation, PTT stop, and session release without USB access.

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

### Required development update procedure

`cmake --build build` updates `build/libflex1500Support.so`; it does **not**
replace a module previously installed under `/usr/local`. An SDR application
using automatic SoapySDR discovery normally loads the installed copy and keeps
that code in memory until the application exits.

After every change that rebuilds the Soapy adapter, use this procedure before
a live application test:

1. Completely close SDR Oxide and every other process using the FLEX-1500
   Soapy module.
2. Build and test the project.
3. Install the new artifacts:

   ```sh
   sudo cmake --install build
   ```

4. Confirm which installed module SoapySDR discovers and its embedded version:

   ```sh
   SoapySDRUtil --info | grep flex1500
   ```

   A connected client's Soapy hardware information also reports
   `adapter_version` and `adapter_git_revision`.

5. Verify that the build and installed module are byte-for-byte identical. The
   two hashes printed by this command must match:

   ```sh
   sha256sum build/libflex1500Support.so \
     /usr/local/lib/SoapySDR/modules0.8/libflex1500Support.so
   ```

6. Restart any running `flex1500d` process if the daemon was also rebuilt.
   Replacing its file does not replace code already loaded by a running
   process.
7. Reopen the SDR application only after these checks pass.

The developer or assistant coordinating a live test must explicitly tell the
operator when installation, daemon restart, or SDR-application restart is
required. “Built successfully” must not be used to imply that the installed
module or a running process has been updated.

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

## First live TX validation

Date: 2026-09-03

KB1JDX tested the SoapySDR TX channel repeatedly with SDR Oxide at 28.475 MHz
into a suitable dummy load. SDR Oxide supplied continuous raw I/Q through the
leased network TX path. The radio transmitted application audio, then unkeyed
and released the session cleanly on every observed test. Daemon logs showed
successful session acquisition, stream connection, PTT start, PTT stop, and
session release without an abandoned owner.

TX begins only after the daemon has received its required 4,096-frame
prebuffer. During this validation the adapter's initial PTT attempts returned
transient `409 Conflict` responses until the daemon had accounted for enough
samples; a later retry then keyed successfully. This produces a noticeable but
safe startup delay and remains an interoperability/latency improvement area.

SDR Oxide's Tune button does not call the FLEX-1500 daemon's dedicated
capture-matched 5 W Tune API. It opens an ordinary SoapySDR raw-I/Q TX stream.
In the observed test the radio keyed but the wattmeter showed no RF output,
consistent with the application supplying zero-magnitude or otherwise
carrier-free I/Q. The adapter must not reinterpret zero I/Q as a Tune request,
because zero samples are valid raw-stream data. Operators who need the
validated 5 W carrier should use the daemon's browser or HTTP Tune control.

This validates the adapter-to-daemon Soapy TX lifecycle with SDR Oxide. It does
not yet establish compatibility with other SoapySDR transmit applications,
timed bursts, or `END_BURST` operation.

## Installed-module correction and latency validation

Date: 2026-09-14

An audit found that SDR Oxide had continued loading an installed September 4
module (`0.1.0-0625c40`) while newer adapter code was only being rebuilt in the
development tree. Earlier Soapy-specific latency and audio tests performed
with that stale module must therefore not be treated as validation of the
newer adapter. This incident led to the mandatory build/install/version/hash
procedure documented above.

After installing the current module and completely reopening SDR Oxide,
KB1JDX reported substantially faster PTT response and clean, uninterrupted
audio. The installed module was subsequently verified by `SoapySDRUtil` as
`0.2.0-80edebb`. Browser HTTP microphone transmission using the same rebuilt
daemon was also clean and responsive, separating the corrected Soapy adapter
result from the shared daemon behavior.

Normal-stop testing then exercised browser HTTP, SDR Oxide/SoapySDR, and the
physical microphone. The complete measured graceful-drain result is recorded
in the [transmit operator guide](TX_OPERATOR_GUIDE.md). These tests validate
SDR Oxide only; compatibility with additional Soapy TX applications remains
open.
