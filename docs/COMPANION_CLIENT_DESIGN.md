# Companion bridge design

## Purpose

A future `flex1500` companion bridge should run on another computer and turn
the network API exposed by `flex1500d` into familiar local audio and rig-control
interfaces. Existing logging, digital-mode, and station software should not
need to understand the FLEX-1500 USB protocol or the native HTTP/IQ API.

This is an architectural design, not an implemented program. It deliberately
describes a focused compatibility bridge rather than a complete SDR client.
The public API remains available to anyone who wants to build a richer client.

```text
Remote computer
┌─────────────────────────────────────────┐
│ flex1500 companion bridge               │
│                                         │
│ flex1500d API connection                │
│   ├─ RX IQ                              │
│   ├─ tuning and receiver controls       │
│   ├─ station ownership                  │
│   └─ TX audio/IQ and PTT                │
│                                         │
│ Local operating-system interfaces       │
│   ├─ virtual audio input/output         │
│   └─ standard rig-control interface     │
└─────────────────────────────────────────┘
                 │ LAN
                 ▼
          flex1500d → FLEX-1500
```

## Local interfaces

The initial Linux bridge should expose well-known interfaces instead of
requiring every station application to add native `flex1500d` support:

- a PipeWire source carrying demodulated receive audio;
- a PipeWire sink accepting microphone or application audio for transmit;
- a Hamlib-compatible `rigctld` interface or bridge for frequency, mode, PTT,
  and supported receiver controls;
- minimal connection, ownership, level, and fault status; and
- possibly a virtual serial CAT interface later for applications that cannot
  use Hamlib.

PulseAudio compatibility may be provided through PipeWire's PulseAudio layer
where available. Other operating systems can expose their corresponding
standard virtual-audio and rig-control facilities without changing the daemon
protocol.

## Intended station workflow

Digital-mode, logging, and contest applications would use normal local
interfaces while the companion bridge translates them to the network API:

```text
WSJT-X / fldigi / logging software
       ├─ audio through PipeWire
       └─ rig control through Hamlib
                    ↓
       flex1500 companion bridge
                    ↓
             flex1500d API
```

The bridge supplies transport and compatibility interfaces. Existing
applications remain responsible for their own operator interface, waterfall,
logging, digital-mode processing, memories, scanning, automation, and
recording. Other developers remain free to build full clients directly on the
documented API.

## Responsibility boundary

`flex1500d` remains responsible for:

- USB communication and radio initialization;
- authoritative physical frequency and hardware-filter selection;
- exclusive station and transmit ownership;
- transmit band and mode policy;
- TX interlocks, maximum-key timers, watchdogs, and guaranteed unkey;
- physical microphone PTT priority;
- raw RX and TX transport; and
- recovery after USB, stream, or client failures.

The companion bridge is responsible for:

- receive demodulation and mode-specific host DSP;
- local audio-device integration;
- microphone or application-audio capture;
- Hamlib and optional CAT presentation;
- minimal connection, ownership, level, and fault reporting; and
- translating local application requests into ownership-aware API operations.

Safety decisions remain authoritative in the daemon. A client request must
never bypass frequency limits, ownership, drive limits, maximum-key timing, or
unkey cleanup.

## Audio bridge

Receive IQ should be demodulated locally, then published as a normal PipeWire
audio source. Applications can listen to or record that source without adding
another daemon endpoint. Recording belongs to those applications; the bridge
does not need its own recording subsystem.

For transmit, a PipeWire sink accepts audio from a microphone, digital-mode
program, or other local source. The client applies only the processing assigned
to it by the selected API profile and sends audio or already-generated IQ using
the existing ownership-aware TX routes. PTT remains a separate explicit
operation; the presence of audio must not key the transmitter unless a future
reviewed VOX mode deliberately provides that behavior.

Applications that record or replay raw IQ should follow the separate
[raw-IQ interoperability guide](RAW_IQ_INTEROPERABILITY.md).

## Rig control and ownership

The companion bridge should acquire the station-control lease before exposing
writable rig controls. While it owns the station, Hamlib requests can change
the physical frequency and supported controls, and an authorized TX path can
request PTT. The client must renew ownership, display loss of ownership, and
immediately stop presenting writable control after expiration or rejection.

A non-owner instance may eventually operate as a receive-only listener inside
the owner's real 48 kHz IQ window. It must not imply that it can retune the
physical radio or transmit. Physical microphone PTT remains the documented
priority exception and acts as an extension of the current station operator.

## Failure behavior

The client should fail safely and visibly:

- API loss or TX-stream failure requests immediate unkey through the daemon's
  existing disconnect handling;
- local audio loss does not silently substitute uncontrolled samples;
- ownership loss disables writable rig control and PTT;
- stale buffered receive audio is discarded after reconnect or physical
  retuning; and
- status should distinguish network delay, audio underrun, ownership rejection,
  radio disconnection, and daemon safety shutdown.

The daemon remains the final safety boundary because a remote client can crash,
lose power, or disappear from the network at any time.

## Suggested implementation stages

1. Connect to the native API, display status, and acquire/release station
   ownership.
2. Receive IQ, demodulate the implemented modes, and publish a PipeWire receive
   source.
3. Add frequency, mode, filter, gain, and squelch control.
4. Expose a Hamlib-compatible control bridge for logging and digital-mode
   applications.
5. Add the PipeWire transmit sink and integrate it with the reviewed API TX
   ownership and PTT state machine.
6. Add optional CAT and additional-platform integrations only where they solve
   demonstrated compatibility needs.

Each stage should remain useful independently and should be testable without
weakening the daemon's ownership or transmit-safety rules.
