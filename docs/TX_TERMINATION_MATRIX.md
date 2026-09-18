<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Transmit termination and cleanup matrix

All transmitter owners converge on the shared `flex1500_tx_control` stop
callback and the USB lifecycle cleanup executor. A normal operator stop may
briefly drain already queued audio, while faults and watchdog events stop
accepting samples and request immediate unkey.

The cleanup executor derives its actions from every TX preparation step that
was attempted. For a fully prepared transmitter it attempts, in safety order:

1. transition mute;
2. `SET_TR(0)` unkey;
3. restoration of the receive frequency;
4. transition unmute;
5. a safe PA-filter state;
6. TX-amplifier disable; and
7. TX USB-stream cancellation.

Every planned action is attempted even when an earlier command fails. The
first error is returned to the controller, which clears the owner, enters the
faulted state, and exposes the cleanup failure through diagnostics.

| Termination source | Controller route | Stop behavior |
| --- | --- | --- |
| Browser/API or Soapy PTT stop | Network session graceful release | Bounded queue drain, then full cleanup |
| Physical microphone PTT release | Physical-owner graceful release | Bounded queue drain, then full cleanup |
| TX stream socket error or disconnect | Network stream disconnect | Immediate full cleanup |
| Missing network samples | Network data watchdog | Immediate full cleanup and session invalidation |
| TX lease expiry | Network lease watchdog | Immediate full cleanup and session invalidation |
| Maximum-key timeout | Shared controller timer | Immediate full cleanup and owner release |
| Tune lease or hard-limit expiry | Tune controller release | Immediate full cleanup and lease invalidation |
| USB pump/device failure | Recovery shutdown route | Immediate owner cleanup before USB stop/reopen |
| `SIGINT`, `SIGTERM`, or normal daemon exit | Shared controller shutdown | Immediate full cleanup before PA preparation is disabled |
| Failure during TX preparation | Start-failure rollback | Cleanup of every step that may have been attempted |
| Failure during cleanup | Cleanup executor | Continue attempting every later safety action; report failure |

An audio-scheduler underrun is deliberately not itself an unkey event. The
scheduler substitutes zero I/Q, records the missing frames, and lets the data
watchdog unkey if useful samples do not resume within its bounded interval.
This avoids relay chatter for a single late buffer while retaining a hard
failure limit.

## Offline verification

`tx-termination-matrix-test` composes the real ownership controller, network TX
session, watchdogs, and cleanup planner with a mock cleanup backend. It covers
explicit stop, physical PTT release, stream disconnect, missing-data timeout,
lease expiry, maximum-key timeout, process shutdown, partial start failure, and
every individual cleanup-action failure. Each case must attempt the full plan
and leave no transmitter owner behind.

The existing `tx-failure-injection-test` separately fails every preparation and
cleanup USB operation, while `tx-lifecycle-test` exhaustively checks all
partial-state combinations and confirms that one failed cleanup action never
suppresses another.

These tests prove the daemon's requested command sequence and state cleanup
without a radio. Physical USB and RF behavior remains covered by the separate
live dummy-load and disconnect/recovery validation requirements.
