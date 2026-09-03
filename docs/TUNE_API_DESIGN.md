<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Tune API safety design

PowerSDR Tune has been captured and reproduced at 5 W into KB1JDX's dummy
load. This establishes the waveform but does not justify adding an unguarded
transmit route to the current HTTP server. The server presently listens on the
LAN without authentication or encryption, and arming strings are safety
interlocks rather than access control.

## Proposed lifecycle

- `PUT /v1/radio/tune/start` starts the fixed Tune carrier at the current dial
  frequency and returns a random, process-local lease identifier.
- `PUT /v1/radio/tune/keepalive/LEASE` renews ownership.
- `PUT /v1/radio/tune/stop/LEASE` stops Tune immediately.
- A lease lasts 15 seconds without keepalive, allowing at least ten seconds for
  manual adjustment. The test page renews it while Tune remains selected.
- An independent 60-second hard limit stops Tune even if keepalives continue.
  A later configuration may raise this only after thermal testing.
- USB failure, stream underrun, lease loss, process signal, normal
  shutdown, or any inconsistent state immediately runs mute, `SET_TR(0)`, RX
  center restoration, and PA-filter idle cleanup.
- Only one controlling lease exists. RX tuning and gain changes are rejected
  while Tune owns the radio.

The hardware-independent ownership and watchdog state machine is implemented
in `tune_control.c` with offline tests for disabled operation, zero and stale
leases, duplicate start, keepalive, lease expiry, hard timeout, startup
failure, explicit stop, and shutdown cleanup. It is connected to the USB and
HTTP layers in the tuning-enabled live daemon mode. The basic live daemon mode
remains incapable of transmission.

## Network boundary

Tune is enabled only by the transmit-enabled live daemon mode and is
available through the LAN API; authentication and
encrypted control are tracked as a separate project item. The normal receive-
only command remains incapable of transmit and `/v1/radio` reports the armed
Tune capability and current activity explicitly.

The live mode is:

```text
--initialize-radio-and-enable-transmit
```

Lease identifiers begin from a process-local random seed, but leases only
express ownership; neither they nor the startup string are security
credentials. Firewall policy should limit the unauthenticated API to trusted
station-control hosts.

## SoapySDR

No Tune control is proposed for SoapySDR. SoapySDR has normal TX streaming and
gain concepts but no standard antenna-adjustment carrier operation. Mapping
Tune onto an unrelated setting would make ownership and safety behavior
ambiguous. Future general Soapy TX should use the same exclusive transmitter
state machine but remain distinct from this HTTP Tune operation.
