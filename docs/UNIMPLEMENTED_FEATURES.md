<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Identified features not yet implemented

This checklist records known or suspected FLEX-1500 capabilities that are not
yet fully implemented by `flex1500d`. It is a research backlog, not a promise
that every item will become a supported feature. Some items involve specialized
connectors, undocumented commands, or transmit behavior and require separate
review and explicit permission before any live radio write.

An item should be checked only after its protocol is documented, its intended
scope is implemented, and the relevant offline and live validation is recorded.
Host-side PowerSDR features are listed separately so they are not mistaken for
functions performed inside the radio.

## Physical audio and front-panel interfaces

- [ ] Send host-demodulated receive audio through USB endpoint `0x01` to the
  physical front-panel headphone jack.
- [ ] Identify and implement any codec, headphone-level, mute, and audio-routing
  controls required for dependable physical headphone output.
- [ ] Physically validate the front-panel CW key jack and document straight-key,
  paddle, dot, and dash behavior.
- [ ] Implement an optional CW keyer and sidetone path with reviewed timing,
  station ownership, TX interlocks, and guaranteed unkeying.
- [ ] Determine whether any useful physical microphone controls remain beyond
  the implemented PTT, microphone gain, USB/LSB modulation, metering, limiter,
  and optional speech compression.

## Accessory and specialized RF connections

- [ ] Document and validate FlexWire PTT behavior beyond the currently decoded
  observational status bit.
- [ ] Research other useful FlexWire accessory signaling, if supported by the
  FLEX-1500 hardware and justified by an operator use case.
- [ ] Document the XVRX and XVTX/COM transverter signal paths completely.
- [ ] Implement optional transverter receive/transmit routing only after the
  physical connections, frequency mapping, offsets, and TX safety behavior are
  independently reviewed.
- [ ] Document `SET_TX_ANT` parameters and determine whether any user-facing TX
  signal-path control is appropriate for this model.
- [ ] Document `SET_XREF` and the external 10 MHz reference-input behavior,
  including selection, tuning-word calculations, loss-of-reference behavior,
  and safe fallback to the internal reference.

## Identity, configuration, and status

- [ ] Finish decoding and expose useful read-only identity fields: radio serial,
  TRX serial/revision, PA serial/revision, and regulatory region.
- [ ] Document the full meaning of the general status response rather than only
  the currently used physical-input bits.
- [ ] Catalog the EEPROM locations read by PowerSDR and identify which values
  are calibration data, identity data, or saved configuration.
- [ ] Expose only well-understood, useful EEPROM-derived values as read-only API
  metadata; do not add EEPROM writes without a separate safety design.
- [ ] Document request-index matching, response errors, command timeouts, and
  behavior across USB disconnect/recovery in enough detail for an independent
  implementation.

## Codec, filters, and internal routing

- [ ] Identify startup opcodes `1375` through `1380`, currently suspected to
  configure codec or audio-path settings.
- [ ] Determine the exact codec transition-mute operations and timing used
  around transmit/receive switching.
- [ ] Document the named manual and bypass RX-filter operations, their
  parameters, and whether they have a safe practical use beyond automatic RF
  preselector selection.
- [ ] Document the internal BITE receive path while keeping it unavailable as a
  normal antenna choice unless a legitimate diagnostic use is established.
- [ ] Complete a reproducible command and endpoint reference containing all
  confirmed opcodes, parameters, encodings, responses, side effects, and
  evidence sources.

## Receive and transmit behavior

- [ ] Calibrate or characterize all five implemented receive gain/attenuation
  steps across representative bands if that additional accuracy proves useful.
- [ ] Implement daemon-generated AM, FM, and CW transmit modulation if desired,
  with mode-specific limits and live dummy-load validation.
- [ ] Document how external raw-I/Q applications should generate and constrain
  other analog or digital emissions; the daemon must not imply that arbitrary
  I/Q is automatically legal or spectrally clean.
- [ ] Validate physical microphone LSB transmission with the same rigor already
  applied to USB.
- [ ] Test SoapySDR transmit and station ownership with additional established
  SDR applications beyond SDR Oxide.

## Capabilities known not to be available internally

These are documented limitations rather than implementation tasks:

- [x] Confirm that the FLEX-1500 does not provide measured forward-power,
  reflected-power, SWR, or PA-temperature telemetry to this daemon.
- [x] Confirm that narrow AM/FM/SSB/CW receive filtering and demodulation are
  host DSP functions, not adjustable narrow filters inside the radio.
- [x] Confirm that the radio has one normal antenna connector; the other RF
  connectors serve the 10 MHz reference and transverter functions.

## Related documentation

- [Protocol findings](PROTOCOL_FINDINGS.md)
- [Receiver controls research](RECEIVER_CONTROLS.md)
- [PowerSDR startup/exit capture](PCAP_PROGRAM_START_STOP.md)
- [Transmit telemetry research](FLEX1500_TX_TELEMETRY.md)
- [Transmit enablement checklist](TRANSMIT_ENABLEMENT_CHECKLIST.md)
- [Receive DSP](RECEIVE_DSP.md)
