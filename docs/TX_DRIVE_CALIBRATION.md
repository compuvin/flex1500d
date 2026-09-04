<!-- SPDX-License-Identifier: GPL-3.0-only -->

# TX drive calibration

This procedure maps the daemon's requested drive percentage to its exact
endpoint-`0x01` complex-I/Q magnitude and to externally measured RF power on
KB1JDX's FLEX-1500. It does not assume that amplitude percentage equals power
percentage.

PowerSDR capture analysis already establishes that its drive control scales
host-generated I/Q amplitude approximately linearly: the captured 50%/100%
ratios were 0.5073 for I RMS and 0.5072 for Q RMS. The daemon follows that
model and uses:

```text
maximum complex magnitude = round(24,890 * drive_percent / 100)
```

The non-disableable final limiter enforces that ceiling for both modulated
audio and client-supplied raw I/Q. The API accepts only 1 through 100%. These
values describe sample amplitude, not calibrated watts.

## Controlled measurement plan

Requirements:

- the radio connected to a suitable 50-ohm dummy load;
- an external wattmeter appropriate for a 5 W HF signal;
- the transmit-enabled daemon running;
- a separate explicit approval immediately before each test.

The calibration client generates a constant-envelope 700 Hz complex tone at
28.475 MHz for exactly three seconds. Tests are separate so the operator can
record the meter reading and allow several seconds between relay operations.
Only the four fixed levels 25%, 50%, 75%, and 100% are accepted. Each has a
level-specific arming string.

The ideal square-law estimates below use the independently observed 5 W Tune
result only as a comparison. Actual readings determine the calibration:

| Requested drive | I/Q ceiling (counts) | Ideal 5 W square-law estimate | Measured RF power |
| ---: | ---: | ---: | ---: |
| 25% | 6,222.5 | 0.3125 W | approximately 0.75 W |
| 50% | 12,445 | 1.25 W | approximately 3.5 W |
| 75% | 18,667.5 | 2.8125 W | approximately 4.0–4.25 W |
| 100% | 24,890 | 5 W | exactly 5 W |

The Tune comparison may differ because RF response varies with baseband
frequency, PA behavior, calibration, and meter characteristics. No result may
be represented as radio telemetry: the FLEX-1500 does not report measured
forward power.

The 25% test was run at 28.475 MHz with the constant-envelope 700 Hz raw-I/Q
waveform. The daemon reported one normal TX start and stop, zero underruns or
dropped frames, and zero USB, command, or cleanup errors. KB1JDX observed
approximately 0.75 W on the external wattmeter. This is an approximate
instrument reading, not radio telemetry, and the difference from the ideal
square-law estimate may include PA response and wattmeter accuracy near the
bottom of its range.

The otherwise identical 50% test also completed with one normal start and
stop and no stream, USB, command, or cleanup errors. KB1JDX observed
approximately 3.5 W. The large increase relative to the 25% reading confirms
that RF watts cannot be calculated by simply squaring the requested amplitude
against the 5 W Tune result. The remaining points will show whether this is PA
nonlinearity, early compression, instrument response, or a combination.

At 75%, the same waveform produced approximately 4.0–4.25 W on KB1JDX's
external wattmeter. The daemon again reported a normal start and stop with no
underruns, dropped frames, USB errors, command errors, or cleanup failures. The
smaller rise from 50% to 75% than from 25% to 50% is consistent with an RF path
approaching compression, although the 100% point and instrument limitations
must be considered before fitting a calibration curve.

At 100%, the same waveform produced exactly 5 W on KB1JDX's external
wattmeter. The daemon reported a normal start and stop with zero underruns,
dropped frames, USB errors, command errors, or cleanup failures, followed by
confirmed PA-filter and amplifier cleanup.

## Result and interpretation

The requested drive-to-I/Q mapping is verified at all four points: 25%, 50%,
75%, and 100% cap complex magnitude at 6,222.5, 12,445, 18,667.5, and 24,890
counts respectively. The external measurements at 28.475 MHz were about
0.75 W, 3.5 W, 4.0–4.25 W, and exactly 5 W.

The measured RF curve is nonlinear, especially between 25% and 75%. The daemon
therefore retains PowerSDR-compatible linear I/Q-amplitude scaling and does not
claim that drive percentage is RF-power percentage. These four readings are a
characterization of KB1JDX's radio, dummy load, wattmeter, frequency, and test
waveform—not a universal watt calibration for every FLEX-1500 or band.

## Offline preparation

This validates one waveform without opening a socket or touching the radio:

```sh
python3 tools/network_tx_test_client.py iq --calibration-drive 25
```

Supplying `--execute-live` changes that behavior and must not be done without
the dummy load and a separately approved test. The program prints the exact
level-specific arming text when an incorrect or missing value is supplied.
