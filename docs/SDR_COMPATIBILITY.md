# SDR application compatibility plan

The end goal remains using the FLEX-1500 from supported SDR applications over
the network. The daemon's `F15I` IQ framing and HTTP API are intentionally a
small internal protocol, not an assumption that existing applications will
natively recognize `flex1500d`.

## Layering

```text
FLEX-1500 USB
    -> flex1500d receive/safety core
    -> versioned network IQ and control API
    -> compatibility adapter
    -> SDR application
```

The first general adapter candidate is a SoapySDR device module. That would map
application requests for sample rate, center frequency, and sample streaming to
the receive-only daemon API. Applications with suitable SoapySDR integration
could then use the radio without knowing the FLEX-1500 USB protocol.

Additional bridges can be evaluated for applications that speak only another
network protocol. An `rtl_tcp`-style bridge could broaden basic receive
compatibility, but that protocol may not represent every radio capability or
the daemon's richer status information. Program-specific compatibility will be
verified rather than claimed generically.

## Receive-only contract

Adapters will initially expose:

- one 48 kHz complex receive stream
- bounded 100 kHz–54 MHz center-frequency control
- daemon/radio status and disconnect handling
- no TX/PTT capability

The adapter must not invent or forward TX controls. Native Linux TX research is
shelved, and `/v1/radio` advertises `receive_only: true` and
`transmit_enabled: false`.

## Before LAN exposure

The current daemon binds only to loopback. A remote adapter can initially run on
the same host. Direct LAN binding will be added only with an explicit listen
address and access policy; state-changing RX tuning must not become available
to arbitrary network clients accidentally.
