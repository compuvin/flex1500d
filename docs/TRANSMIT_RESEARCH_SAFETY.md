# Transmit research safety and scope

`flex1500d` is intentionally receive-only. The daemon has no TX sample
producer, endpoint-`0x01` output scheduler, PTT/MOX control route, or network
transmit endpoint. `GET /v1/radio` reports `transmit_enabled: false`, and tests
require representative TX/PTT API requests to remain unavailable.

The repository retains three standalone TX-owned research programs so the
reverse-engineered protocol and experimental evidence remain auditable:

- PA-filter relay probe
- fixed zero-I/Q transmit-switching probe
- fixed 700/1900 Hz two-tone transmit probe

These executables are excluded from the default build. They are created only
when CMake is explicitly configured with:

```sh
-DFLEX1500_BUILD_TX_RESEARCH=ON
```

That option does not enable transmit in the daemon or API.

## Minimal validation

The probes have only been exercised in a few fixed, controlled experiments.
They do not establish general transmitter safety, spectral purity, output
power accuracy, safe operation at other frequencies or amplitudes, reliable
behavior after arbitrary USB/process failures, or suitability for on-air use.
They are research artifacts, not supported radio controls.

## Arming strings are not security

The exact long execution strings reduce accidental invocation and prevent one
probe's approval from being reused for another. They are safety interlocks for
a controlled workflow. They are not authentication, authorization, access
control, or a security boundary. A person with local source/executable access
can inspect, alter, or bypass them.

## Dummy-load-only policy

Do not execute a TX probe on an antenna. Any deliberate hardware execution
requires all of the following:

- a suitable 50-ohm dummy load connected before the radio is opened;
- an independently reviewed, fixed test plan;
- explicit authorization for that exact execution;
- appropriate RF observation and an immediate means to remove power.

A dummy load is mandatory but is not, by itself, sufficient authorization or
proof that an experiment is safe. The safest default is not to compile or run
these probes.
