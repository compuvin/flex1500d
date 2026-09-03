# Fixed three-second zero-I/Q TX switching probe

> **Minimally tested TX research:** This executable is included in the default
> build. Its arming string is a safety interlock, not access control. Do not
> execute it on an antenna; a suitable 50-ohm dummy load and a separately
> reviewed and authorized test plan are mandatory.

This probe is the first proposed Linux transmit-switching experiment. It is
fixed to 28.475 MHz, a three-second keyed interval, and test metadata recording
the requested 50% drive. KB1JDX confirmed that the transmitter will be
connected to a suitable 50-ohm dummy load.

The waveform is deliberately zero: every signed-16-bit endpoint-`0x01` frame is
`I=0, Q=0`. Consequently, applying a 50% numerical drive factor still produces
zero. This run is intended to validate USB streaming, relay/configuration
sequencing, and automatic unkeying without intentional modulation or carrier
generation. It does not validate RF output power.

## Offline-by-default behavior

Research-only build (does not enable TX in the daemon or API):

```sh
cmake -S . -B build-tx-research -DFLEX1500_BUILD_TX_RESEARCH=ON
cmake --build build-tx-research
```

Running the executable without arguments only prints its exact plan and packet
bytes:

```sh
./build-tx-research/flex1500-zero-iq-tx-probe
```

It does not enumerate, open, or modify the radio. Building and automated tests
also use only this offline form. Any live run requires a separate review of the
printed plan and explicit permission from KB1JDX for this exact command:

```sh
./build-tx-research/flex1500-zero-iq-tx-probe \
  --execute-approved-zero-iq-tx-28475000-50pct-3s
```

## Fixed live sequence

If separately approved, the probe will:

1. Open only USB device `2192:1502`, refuse an active kernel driver, and claim
   interface 3.
2. Send `INITIALIZE`, choose PA filter 2, enable `AMP_TX1`, and set the known
   receive center word `0x25f60000`.
3. Queue eight zero-filled endpoint-`0x01` transfers, each containing 64
   192-byte isochronous packets, and allow 250 ms of pre-roll.
4. Apply transition mute, set exact TX word `0x25f77777`, and send `SET_TR(1)`.
5. Remove transition mute after 200 ms while continuing zero I/Q.
6. At three seconds after successful `SET_TR(1)`, mute and send `SET_TR(0)`.
7. Restore receive word `0x25f60000`, unmute after 200 ms, return PA filter to
   0, cancel/drain sample transfers, release the interface, and close USB.

After any error, SIGINT, or SIGTERM while keyed, cleanup attempts transition
mute, `SET_TR(0)`, and receive-frequency restoration before stopping the sample
stream. It also attempts `SET_PA_FILTER(0)` whenever the interface was claimed.

## Explicit exclusions and limitations

The probe has no parameterized frequency, duration, drive, or waveform; no
network entry point; and no nonzero sample generator. It does not change
antennas, PA bias, EEPROM, firmware, USB configuration, or alternate settings.

Zero I/Q minimizes intentional output but does not prove zero RF energy:
hardware DC offsets, leakage, or switching transients may remain. The dummy
load requirement therefore still applies. This probe also cannot establish the
safe amplitude for a later tone test.

## First approved execution

KB1JDX confirmed the suitable dummy load and explicitly approved the exact
fixed command. The probe was executed once on 2026-08-28. All 13 planned
commands completed with 20/20 bytes transferred, including the scheduled
`SET_TR(0)`, receive-center restoration, transition unmute, and
`SET_PA_FILTER(0)` cleanup. Endpoint `0x01` completed 1,408 zero-I/Q
isochronous packets, and the program reported no stream failure.

The software result establishes successful host-side sequencing and orderly
unkey completion. It does not establish emitted RF power; KB1JDX's external
observations and any measurement-instrument results should be recorded
separately.

KB1JDX heard the expected relay clicks but observed no apparent RF
transmission. That is consistent with the intentionally zero-valued I/Q stream,
not evidence that the one-second interval was too short. The next revision
extends the keyed interval to three seconds to make switching-state observation
easier while retaining zero I/Q and the same lack of intentional RF output.

## Three-second approved execution

KB1JDX separately approved the revised three-second command, which was executed
once on 2026-08-28. All 13 commands again completed with 20/20 bytes
transferred, including scheduled `SET_TR(0)`, receive-center restoration,
transition unmute, and `SET_PA_FILTER(0)`. Endpoint `0x01` completed 3,456
zero-I/Q isochronous packets with no reported stream failure. External radio
indication and measurement observations remain to be supplied by KB1JDX.
