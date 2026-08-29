# FLEX-1500 protocol findings

## Source provenance

These findings come from the public `ke9ns/PowerSDR-KE9NS-v2.8.0` repository at
commit `12cdc2bb3b2a777cf4dfb5b5a0a6a78242eef275` (2026-08-17). In particular,
the repository contains `Console/hid/Flex1500USB.cs`, identified in its header
as decompiled from `Flex1500USB.dll` version 2.3.2.1.

The upstream source was inspected from a temporary checkout and is not vendored
into this project.

## Endpoint assignment

The decompiled wrapper discovers pipes by direction and transfer type. Its
assignments agree with the descriptors observed directly on this radio:

| Endpoint | Transfer | PowerSDR role |
|----------|----------|---------------|
| `0x01` | Isochronous OUT | Host-to-radio I/Q |
| `0x82` | Isochronous IN | Radio-to-host I/Q |
| `0x83` | Interrupt IN | Status and command responses |
| `0x04` | Interrupt OUT | Commands |

## Command request format

Normal commands sent to interrupt OUT endpoint `0x04` are 20 bytes:

| Offset | Size | Meaning | Encoding |
|--------|------|---------|----------|
| 0 | 1 | Request index | Incrementing byte |
| 1 | 3 | Reserved | Zero |
| 4 | 4 | Opcode | Big-endian unsigned integer |
| 8 | 4 | Parameter 1 | Big-endian unsigned integer |
| 12 | 4 | Parameter 2 | Big-endian unsigned integer |
| 16 | 4 | Result placeholder | Zero in requests |

`WriteOp` sends this packet and does not wait for a response. `ReadOp` sends the
same packet and waits for an interrupt-IN response carrying the matching request
index.

Examples of useful read opcodes discovered in the source:

| Decimal | Hex | Name |
|---------|-----|------|
| 1200 | `0x04b0` | Get firmware revision |
| 1220 | `0x04c4` | Get serial number |
| 1266 | `0x04f2` | Read PTT/key state |
| 1350 | `0x0546` | Get regulatory region |
| 1355 | `0x054b` | Get status |

Although these opcodes request information, using them requires an interrupt-OUT
transfer. They must not be tested on the radio without explicit permission from KB1JDX.

The receive-frequency path uses write opcode 1347 (`SET_RX1_FREQ_TW`). Its
parameter is the 32-bit DDS tuning word described in
`docs/RX_TUNING_PROBE.md`; parameter 2 is zero.

## Interrupt-IN response format

The interrupt-IN handler interprets:

- Byte 0 as live state bits: PTT `0x01`, FlexWire PTT `0x08`, dash `0x10`, and
  dot `0x20`.
- Byte 1 as message type: `1` for a normal response and `2` for EEPROM data.
- Byte 2 as the request index used to match a response to its request.
- Bytes 4 through 7 as the normal 32-bit big-endian result.

The source explicitly describes interrupt-IN timeouts as normal when no PTT or
CW-key events have occurred. This explains the zero-packet status probe without
implying a USB fault.

## I/Q stream format

The wrapper defines these constants:

- 48,000 complex samples per second.
- 1,000 USB packets per second.
- 48 complex samples per USB packet.
- 192 bytes per USB packet.
- Signed 16-bit samples.

Receive bytes are converted as little-endian signed 16-bit values and split by
alternating values into I and Q. Each sample frame is therefore:

```text
I low, I high, Q low, Q high
```

Transmit samples use the same interleaved little-endian signed-16-bit layout.
This confirms the earlier descriptor-based 48 kHz hypothesis.

## Stream startup behavior

When PowerSDR's Start button is pressed, the wrapper opens both isochronous
streams. The 2026-08-28 Start/Stop capture shows that endpoint `0x01` carries
duplicated mono receive audio after an initial 48 ms of zeros; it is not simply
zero-filled for the duration of receive. No explicit start-stream opcode is
visible in this path.

Submitting endpoint-`0x82` transfers is sufficient to start the sample path, as
subsequently confirmed on Linux. The OUT schedule is needed for the radio's
codec/headphone audio path, not for basic I/Q reception.

## Observed PowerSDR lifecycle

The source shows this high-level order:

1. Device attach assigns all four pipes and starts the interrupt-IN listener.
2. Radio discovery sends opcode 1220 (`GET_SERIAL_NUM`) through endpoint `0x04`
   and receives its indexed response through `0x83`.
3. Selecting the radio installs application callbacks and initializes host-side
   buffers; this step does not itself send a radio command in the decompiled
   wrapper.
4. PowerSDR reads firmware revision with opcode 1200 (`GET_FIRMWARE_REV`).
5. The explicit opcode 1219 (`INITIALIZE`) appears in the normal wrapper API,
   but the inspected application calls it only following a firmware-update path.
6. Pressing Start applies current frequency/mixer settings and then opens both
   isochronous streams.

Therefore, PowerSDR does communicate with the radio before streaming, but the
source does not show a mandatory generic initialize opcode on every normal
startup. A capture will establish what the installed version actually does and
which setting writes occur before its first I/Q frame.

The later application launch/exit capture confirms that normal PowerSDR startup
does not send opcode 1219. See `docs/PCAP_PROGRAM_START_STOP.md` for the actual
discovery, EEPROM, routing, filter, and shutdown sequence.

## What a Windows capture would add

The source provides most structural details, but a capture remains useful to:

- Confirm the actual command ordering used by the installed PowerSDR version.
- Confirm response bytes and resolve any decompiler artifacts.
- Observe USBPcap representation of isochronous frames and errors.
- Determine whether `0x82` begins before the first `0x01` packet.
- Establish timing around Start and Stop.
