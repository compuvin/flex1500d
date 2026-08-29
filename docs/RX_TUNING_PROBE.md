# FLEX-1500 receive-frequency probe

## Source finding

The pinned PowerSDR source calls `USBHID.SetFreqTW(tw)` for the FLEX-1500.
The decompiled `Flex1500USB.dll` wrapper implements that call as:

```text
WriteOp(1347, tuning_word, 0)
```

Opcode 1347 is named `USB_OP_SET_RX1_FREQ_TW`. For a normal FLEX-1500 using
its 384 MHz reference, PowerSDR calculates:

```text
tuning_word = floor(0xFFFFFFFF * frequency_hz * 2 / 384000000)
```

PowerSDR supports an optional 400 MHz external-reference configuration and a
stored clock correction. This first probe intentionally assumes the normal
384 MHz reference and no correction, matching the application's defaults.

## Offline-default probe

The probe does nothing to USB without its explicit execution argument:

```sh
./build/flex1500-rx-tune-probe
./build/flex1500-rx-tune-probe --plan 10000000
```

The plan command prints the computed tuning word and every command byte without
opening the radio. Accepted frequencies are 100,000 through 54,000,000 Hz.

## Proposed first operation

The proposed target is 10.000 MHz, a standard WWV AM frequency. Its tuning word
is 223,696,213 (`0x0d555555`). The exact endpoint-`0x04` packet is:

```text
0b 00 00 00 00 00 05 43 0d 55 55 55 00 00 00 00 00 00 00 00
```

Decoded:

- Request index: 11
- Opcode: 1347 / `0x00000543` (`SET_RX1_FREQ_TW`)
- Parameter 1: `0x0d555555`
- Parameter 2: zero
- Result placeholder: zero

If separately approved, the execution command will be:

```sh
./build/flex1500-rx-tune-probe --execute-approved-tune 10000000
```

It opens only USB device `2192:1502`, claims interface 3, sends that single
20-byte packet, then releases and closes the interface. It sends no
`INITIALIZE`, filter, relay, gain, PTT, TX, firmware, EEPROM, reset,
configuration, or alternate-setting operation.

## Hardware-filter caveat

PowerSDR normally follows tuning with a separate receive-filter command. Its
frequency-to-filter map is:

| Frequency range (MHz) | Filter index |
|---|---:|
| below 0.48 | 0 (bypass) |
| 0.48–0.88 | 11 |
| 0.88–1.6 | 10 |
| 1.6–2.3 | 9 |
| 2.3–3.5 | 8 |
| 3.5–5.2 | 7 |
| 5.2–7.7 | 6 |
| 7.7–11.4 | 5 |
| 11.4–17.0 | 4 |
| 17.0–25.3 | 3 |
| 25.3–37.6 | 2 |
| 37.6–56.0 | 1 |

The first test excludes that second command to isolate frequency tuning. If the
radio's existing filter does not pass 10 MHz, a static result will not disprove
the tuning-word encoding. Filter index 5 can be reviewed and permission-tested
separately afterward.

## First execution result

Date: 2026-08-27

KB1JDX explicitly approved the 10.000 MHz operation. The probe opened the
radio, claimed interface 3, and successfully transferred all 20 bytes of the
documented opcode-1347 packet. It then released the interface and closed USB.
No other command or endpoint was used. The radio is expected to remain tuned to
10.000 MHz, subject to the assumed normal 384 MHz reference clock.

KB1JDX then separately approved a 1.024-second endpoint-`0x82`-only capture.
All 1,024 USB packets completed without error; all 47,616 retained complex
samples were non-sentinel data. The capture is saved as
`captures/rx-10mhz.iq16le`, and its AM-demodulated audio is
`captures/rx-10mhz-am.wav`.

KB1JDX heard a tone consistent with WWV. Offline narrow-band measurements
found the 500 Hz band at approximately -14.6 dB mean level, versus -21.6 dB at
1 kHz and below -29 dB at 440, 600, 1,200, and 1,500 Hz. This is strong evidence
that the tuning command, tuning-word formula, receive stream, and AM DSP path
work together on a real signal. The existing radio filter passed 10 MHz, so no
filter-relay command was needed for this validation.
