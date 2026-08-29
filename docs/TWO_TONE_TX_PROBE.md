# Fixed 700/1900 Hz two-tone TX probe

> **Minimally tested TX research:** This source is excluded from the default
> build. Its arming string is a safety interlock, not access control. Do not
> execute it on an antenna; a suitable 50-ohm dummy load and a separately
> reviewed and authorized test plan are mandatory.

This is a separately compiled, offline-by-default RF-producing probe based on
the waveform measured in `PCAP_TWO_TONE_50.md`. KB1JDX approved creating and
executing the fixed test with the FLEX-1500 connected to a suitable dummy load.

## Fixed parameters

- RF center: exactly 28.475 MHz (`0x25f77777`)
- keyed duration: three seconds
- sample rate and format: 48 kHz signed-16-bit little-endian complex I/Q
- tones in `I+jQ`: -700 Hz and -1900 Hz
- amplitude: 6,222 counts per tone
- maximum generated I or Q: 12,444 counts
- drive characterization: matches the observed PowerSDR 50% two-tone stream
- load: suitable 50-ohm dummy load required

Each 50-ms sample buffer contains exactly 35 cycles of the 700 Hz tone and 95
cycles of the 1900 Hz tone. Therefore every queued buffer begins and ends at
the same phase and remains continuous regardless of which completed transfer is
resubmitted first. Eight transfers provide 400 ms of queued samples.

The lifecycle and emergency cleanup are the same as the validated zero-I/Q
probe: initialization, PA filter 2, `AMP_TX1(1)`, receive center, sample
pre-roll, transition mute, exact TX center, `SET_TR(1)`, transition unmute,
three-second deadline, mute, `SET_TR(0)`, receive-center restoration, unmute,
and PA filter 0. A keyed error or signal attempts emergency `SET_TR(0)`.

## Commands

Research-only build (does not enable TX in the daemon or API):

```sh
cmake -S . -B build-tx-research -DFLEX1500_BUILD_TX_RESEARCH=ON
cmake --build build-tx-research
```

Offline plan only:

```sh
./build-tx-research/flex1500-two-tone-tx-probe
```

Fixed live command, requiring explicit permission from KB1JDX:

```sh
./build-tx-research/flex1500-two-tone-tx-probe \
  --execute-approved-two-tone-tx-28475000-50pct-3s
```

The executable accepts no frequency, duration, amplitude, waveform, or network
parameters. The zero-I/Q probe's arming token is deliberately rejected.

## First approved execution

After the exact offline plan compiled and all 21 tests passed, KB1JDX explicitly
approved execution with the suitable dummy load connected. The fixed command
was executed once on 2026-08-28. All 13 commands completed with 20/20 bytes
transferred, including scheduled `SET_TR(0)`, receive-center restoration,
transition unmute, and `SET_PA_FILTER(0)`. Endpoint `0x01` completed 3,450
two-tone isochronous packets without a reported stream failure.

The program's final counter was initially labeled `zero-I/Q packets completed`
because both probes share the same guarded streaming engine. The compiled
two-tone target did call `fill_two_tone()` for every buffer; the reporting label
was corrected after the run.

KB1JDX independently observed the transmitted tone on another radio. This
confirms actual RF transmission and modulation from the Linux-generated
endpoint-`0x01` waveform, rather than only USB command completion or relay
switching. The observation establishes functional two-tone TX at the tested
fixed frequency, amplitude, duration, startup state, and dummy-load setup; it
does not by itself characterize absolute RF power, spectral purity, harmonics,
or failure behavior outside this fixed probe.
