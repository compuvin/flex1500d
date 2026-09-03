<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Transmit preparation shutdown and recovery

This document enumerates the partial states that the daemon must clean up.
It is an engineering verification record, not a project goal.

| Partial state | Conservative cleanup |
| --- | --- |
| Transmit mode requested, no frequency known | Set PA filter 0; send `SET_AMP_TX1(0)` |
| PA filter selected, amplifier enable not attempted | Set PA filter 0 |
| Amplifier-enable write attempted or confirmed | Set PA filter 0; send `SET_AMP_TX1(0)` |
| Tune stream allocated or partially submitted | Cancel every endpoint-`0x01` transfer and drain callbacks |
| Transition mute or Tune-frequency write attempted | Restore the known RX frequency and transition-unmute |
| `SET_TR(1)` attempted, including timeout/short-write ambiguity | Transition-mute and send `SET_TR(0)` even if key success was not confirmed |
| Tune confirmed active | Transition-mute; `SET_TR(0)`; restore RX frequency; restore the mapped PA filter; stop TX stream |
| Normal transmit-enabled shutdown | Stop Tune if needed; set PA filter 0; send `SET_AMP_TX1(0)`; then close USB |
| USB failure while prepared | Attempt the same cleanup; close the failed handle; reopen, initialize, restore RX state, then re-prepare TX |
| USB device physically absent | Record cleanup failure and remain logically unkeyed; never assume a command reached absent hardware |

The implementation records command *attempts*, not only confirmed successes,
for the two ambiguous safety-critical writes. If `SET_AMP_TX1(1)` fails or is
short, cleanup still attempts `SET_AMP_TX1(0)`. If `SET_TR(1)` fails, times
out, or is short after submission, cleanup still attempts `SET_TR(0)`. This
handles the case where the radio accepted a command but the host did not
receive successful transfer completion.

## Verification status

Complete. The offline state combinations, cleanup-command failure behavior,
normal live shutdown, unkeyed USB recovery, partial stream startup, and
ambiguous post-key cleanup have all been verified.

The checklist item is not complete until these cases have been exercised or
independently reviewed with evidence. No test may intentionally key the radio
without a separate fixed plan, suitable RF load, and explicit approval.

## Live unkeyed verification — 2026-09-01

KB1JDX approved live testing of transmit-prepared states without Tune or
keying. At least five seconds were allowed between band operations to avoid
unnecessary relay cycling.

- Shutdown before selecting a frequency confirmed `SET_PA_FILTER(0)` and
  `SET_AMP_TX1(0)` cleanup.
- Shutdown at 5.300 MHz selected PA filter 5, kept Tune inactive, and confirmed
  PA-filter/amplifier cleanup.
- Shutdown at 14.200 MHz selected PA filter 4, kept Tune inactive, and
  confirmed PA-filter/amplifier cleanup.
- Shutdown at 28.475 MHz selected PA filter 2, kept Tune inactive, and
  confirmed PA-filter/amplifier cleanup.
- USB was unplugged and reconnected at 7.200 MHz while transmit-prepared and
  unkeyed. The daemon detected the loss, reopened the radio on recovery attempt
  4, restored +20 dB gain and 7.200 MHz, reselected PA filter 5, and kept Tune
  inactive. The post-recovery status reported one successful recovery and zero
  radio-command or USB errors. Final shutdown confirmed PA-filter/amplifier
  cleanup.

The daemon now logs whether normal transmit-prepared shutdown successfully
completed the PA-filter reset and amplifier-disable writes instead of only
reporting that the process stopped.

## Offline lifecycle verification — 2026-09-01

The safety decision has been separated into a pure lifecycle planner shared by
the production USB backend and `tx-lifecycle-offline`. The test exhaustively
checks all 64 combinations of these partial states: PA selected, amplifier
enable attempted, stream started, transition command attempted, TX-frequency
command attempted, and key command attempted. In particular, an attempted key
always plans transition-mute plus unkey even when key completion is ambiguous;
an attempted amplifier enable always plans amplifier disable; and a started
stream always plans cancellation. The complete 43-test suite passes, including
the loopback-only Soapy integration test.

The production USB backend now executes cleanup through a shared command
executor. The mock test injects a failure at each of its seven actions in turn
and proves that every later action is still attempted while the first error is
preserved. Thus a failed transition-mute cannot suppress `SET_TR(0)`, and a
failed PA reset cannot suppress amplifier disable.

## Live partial-Tune verification — 2026-09-01

With KB1JDX's approval and the FLEX-1500 connected to a dummy load and
watt/SWR meter, a separately armed fault probe exercised two production cleanup
paths at 28.475 MHz:

- After the endpoint-`0x01` Tune stream had pre-rolled for 250 ms but before
  any key command, an injected failure produced the expected error, left Tune
  inactive, and confirmed final PA-filter reset and amplifier disable.
- Immediately after a successful `SET_TR(1)` transfer, an injected ambiguous
  host-side failure bypassed the normal 200 ms transition delay. Cleanup
  immediately attempted transition-mute and `SET_TR(0)`, restored receive,
  stopped the stream, left Tune inactive, and confirmed final PA-filter reset
  and amplifier disable. KB1JDX observed a brief transmission on the wattmeter,
  followed by an immediate return to zero with the radio no longer keyed.

The probe is offline by default and requires distinct exact arming strings for
the never-key and brief-key stages. It is not exposed through the daemon or
network API.
