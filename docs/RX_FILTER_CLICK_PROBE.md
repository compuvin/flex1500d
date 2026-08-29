# FLEX-1500 RX filter relay click probe

## Purpose

Test whether opcode 1257 (`SET_RX1_FILTER`) accounts for the audible relay
clicks observed during PowerSDR startup, band changes, and shutdown.

The frequency-change capture shows that PowerSDR sends RX filter 2 only 3.013 ms
after changing to the 28 MHz band. The application-start capture also shows RX
filter writes in both audible startup relay clusters.

## Exact proposed operation

The guarded probe will:

1. Open only USB device `2192:1502` and claim interface 3.
2. Send RX filter 5, covering PowerSDR's 7.7–11.4 MHz range.
3. Wait three seconds without performing USB traffic, making the two clicks
   easier to distinguish by ear.
4. Send RX filter 2, covering the 25.3–37.6 MHz range.
5. Release interface 3 and close USB.

Packet 1, `SET_RX1_FILTER(5)`:

```text
0c 00 00 00 00 00 04 e9 00 00 00 05 00 00 00 00 00 00 00 00
```

Packet 2, `SET_RX1_FILTER(2)`:

```text
0d 00 00 00 00 00 04 e9 00 00 00 02 00 00 00 00 00 00 00 00
```

Filter 2 is the last RX-filter state recorded before PowerSDR exited. PowerSDR
did not reset the RX filter on exit. If the radio has been reset or re-powered
since that capture, ending at filter 2 is a known state rather than a guaranteed
restoration of an unknown current state.

## Exclusions

The executable has no filter-number argument. Its armed path excludes:

- PA filter changes
- Frequency changes
- Opcode-1219 initialization
- Preamp, attenuation, gain, antenna, or other routing
- PTT, MOX, transmit, or endpoint-`0x01` traffic
- Sample streaming
- Firmware, EEPROM, reset, configuration, or alternate-setting operations

Running without the exact execution flag prints this plan and does not initialize
libusb. Unit tests verify both packets byte-for-byte and reject filter indices
outside 0 through 11.

The armed command must not be run without separate permission from KB1JDX:

```sh
./build/flex1500-rx-filter-click-probe \
  --execute-approved-rx-filter-click-test
```

## First execution

Date: 2026-08-28

KB1JDX explicitly approved the fixed three-second test. Both endpoint-`0x04`
packets transferred successfully at 20/20 bytes: filter 5 first, followed three
seconds later by filter 2. No other radio command or sample endpoint was used.
The radio ended in known RX-filter state 2. KB1JDX heard no click from either
write. The radio had remained powered since its Windows session, so this is
valid negative evidence for that state but does not yet establish behavior from
a fresh power cycle. The application click is now more likely associated with
the PA-filter or `SET_TR` cluster; neither should be tested without a separate
packet review and approval.

After the radio was power-cycled, KB1JDX explicitly requested the identical
test again. Both packets again transferred at 20/20 bytes, three seconds apart,
and KB1JDX again heard no click. This rules out opcode 1257 as the audible
click source under both tested starting conditions. No further RX-filter click
testing is warranted.
