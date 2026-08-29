# Receive DSP

The host must convert the FLEX-1500's 48 kHz complex I/Q stream into speaker
audio. PowerSDR performs this work on the computer as well; the radio does not
send already-demodulated AM, FM, USB, or LSB audio over endpoint `0x82`.

## Implemented pipeline

The offline DSP path currently provides:

1. Optional complex frequency translation within the 48 kHz passband.
2. A 129-tap Hamming-windowed complex FIR.
3. Symmetric receive filtering for AM and FM.
4. Sideband-selective complex filtering from 300 to 3,000 Hz for USB and LSB.
5. Envelope AM or phase-difference FM demodulation.
6. Audio DC removal and automatic gain control.
7. 48 kHz, mono, signed 16-bit PCM WAV output.

USB and LSB filtering is genuinely asymmetric: synthetic tests verify that a
tone in the selected sideband passes while the mirrored tone is suppressed by
more than 34 dB. Separate synthetic tests cover AM recovery, FM recovery, and
frequency translation.

## Offline command

```sh
./build/flex1500d --demod INPUT.iq16le MODE OUTPUT.wav [OFFSET_HZ]
```

`MODE` is `am`, `fm`, `usb`, or `lsb`. A positive offset means the desired
signal is that many hertz above the capture's center. The accepted offset range
is strictly between -24,000 and +24,000 Hz.

Output files use exclusive creation; an existing file is never overwritten.
This command performs no USB access.

Example used for initial validation:

```sh
./build/flex1500d --demod captures/rx-settled.iq16le am /tmp/rx-am.wav
```

It converted all 47,616 complex samples into a valid 95,276-byte RIFF/WAVE file
at 48 kHz. The capture was taken to validate USB reception rather than while
tuned to a known signal, so intelligible audio is not expected from it.

## Next radio-facing milestone

To receive a known station, the next protocol task is identifying and safely
testing the receive-frequency command. After that, a longer capture at a known
frequency can validate real-world demodulation, filter polarity, audio level,
and listening quality. Every state-changing radio test remains separately
permission-gated.
