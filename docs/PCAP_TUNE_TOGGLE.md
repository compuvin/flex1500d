<!-- SPDX-License-Identifier: GPL-3.0-only -->

# PowerSDR Tune toggle at 100% drive

Capture: `pcaps/flex1500-tune-toggle.pcapng`

KB1JDX captured PowerSDR Tune at a 28.475 MHz dial frequency and observed 5 W
into the dummy load. Offline analysis did not access the radio.

> **A fun bit of project history:** KB1JDX chose 28.475 MHz for these tests
> because it is the frequency used by the Flying Saucer Net, held on Thursday
> nights with K1ZE as net control. No aliens were required to reverse-engineer
> the FLEX-1500 USB protocol—although they might have finished it faster.

PowerSDR keys with hardware tuning word `0x25f74309`, corresponding to
28,474,399.976 Hz, rather than the exact dial-frequency word used for normal
SSB transmit. During the steady Tune interval, endpoint `0x01` contains a
single −600 Hz complex tone. The radio's mixer orientation therefore places
the resulting RF carrier at the 28.475 MHz dial frequency.

Analysis of 224,640 steady samples (4.68 seconds) found:

- I and Q means: exactly zero;
- I RMS and Q RMS: 17,599.790 counts each;
- complex RMS: 24,889.862 counts;
- complex peak: 24,890.330 counts;
- largest absolute I or Q component: 24,890 counts;
- fitted −600 Hz amplitude: 24,889.862 counts; and
- opposite-sideband +600 Hz amplitude below numerical significance.

This explains why the earlier 12,444-count constant tone produced only about
3.5–4 W. PowerSDR Tune uses almost exactly twice its complex amplitude. The
capture provides an exact known-5-W reference without extrapolating sample
levels, but any Linux reproduction still requires a dummy load, a fixed plan,
and separate approval.

An approved Linux reproduction subsequently used the captured tuning word,
tone frequency, and amplitude for three seconds. All key, unkey, receive-
restoration, and PA-filter-reset operations completed normally, and KB1JDX
measured 5 W. The value 24,890 counts is therefore the project's established
100%-drive constant-envelope reference for this radio.

## PowerSDR cross-check

PowerSDR's `Set1500Filters()` supplies the complete PA low-pass-filter map:
filter 7 below 2.5 MHz, 6 below 5.0 MHz, 5 below 8.8 MHz, 4 below 17.5 MHz,
3 below 24.0 MHz, 2 below 35.0 MHz, and 1 at higher FLEX-1500 frequencies.
That independently agrees with captured filter 5 near 7 MHz and filter 2 at
28.475 MHz.

Its USB/DIGU Tune path also subtracts the CW pitch from the requested carrier
before calculating the FLEX-1500 tuning word. With a 600 Hz pitch and a
28.475 MHz requested carrier, this gives the captured 28,474,399.976 Hz
hardware center. The daemon policy and boundary tests now derive these values
instead of relying on a frequency-specific constant.
