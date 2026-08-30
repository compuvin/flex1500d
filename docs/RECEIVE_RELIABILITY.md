<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Receive reliability and recovery

The receive daemon reports USB transfer, packet, short-packet, missing-byte,
sentinel, ring-overflow, network backpressure, disconnect, and command-error
counters through `GET /v1/status`.

Eight consecutive failed USB transfers now mark the receive stream unhealthy.
For a fatal pump, transfer, or device-disconnect failure, the daemon:

1. closes the affected IQ client while keeping the listening socket alive;
2. releases the failed USB session;
3. retries opening and initializing receive up to sixty times, one second apart;
4. restores the last receive gain and frequency/filter selection;
5. clears stale IQ and resumes accepting stream clients.

Recovery attempts and successes are reported as `rx_recovery_attempts` and
`rx_recovery_successes`. Each stage also has a concise `[recovery]` console
message. After sixty failed attempts the daemon exits unsuccessfully so a future
service manager can apply its own restart policy.

This recovery path contains only the already-armed receive initialization,
gain, frequency, and RX-preselector operations. It contains no transmit path.
Live unplug/replug validation requires KB1JDX's explicit permission because a
successful recovery reinitializes and restores state on the radio.

## FLEX-1500 disconnect behavior

The first live disconnect test showed that unplugging USB while receive was
active can leave the radio's USB controller unable to enumerate after merely
reconnecting the cable. The daemon correctly detected the failure and retried,
but Linux did not see device `2192:1502` until KB1JDX power-cycled the radio.
The recovery log therefore explicitly recommends a radio power cycle when the
USB device does not reappear. Automatic state restoration can begin only after
the hardware enumerates; software cannot reopen a device absent from the bus.

The follow-up test power-cycled the radio within the recovery window. The
daemon reopened it on attempt 9, reinitialized receive, restored +20 dB gain,
10.000 MHz, and RX filter 5, and returned to `receiving`. The status endpoint
reported one successful recovery, and a subsequent one-second IQ connection
received 760,000 bytes. The daemon then shut down cleanly.
