# PowerSDR 700/1900 Hz two-tone test at 50% drive

Capture: `pcaps/flex1500-two-tone-test.pcapng`

KB1JDX recorded PowerSDR's built-in two-tone transmitter test into a suitable
dummy load. The configured tones were 700 Hz and 1900 Hz, the displayed drive
was 50%, and the radio was tuned to exactly 28.475 MHz. This analysis was
performed offline and did not access the radio.

## Radio command sequence

The capture contains the same eight-command key/unkey sequence already
observed:

| Time | Operation |
|---:|---|
| 5.374248 s | transition-mute register `0x25` = `0x00` |
| 5.377334 s | exact TX word `0x25f77777` = 28,474,999.986 Hz |
| 5.378967 s | `SET_TR(1)` |
| 5.582284 s | transition-mute register `0x25` = `0xc0` |
| 8.924539 s | transition-mute register `0x25` = `0x00` |
| 8.925894 s | `SET_TR(0)` |
| 8.973229 s | restore RX word `0x25f60000` |
| 9.129058 s | transition-mute register `0x25` = `0xc0` |

There is no separate tone, modulation, or drive command. PowerSDR synthesizes
the complete modulated waveform on endpoint `0x01`.

## Exact steady-state waveform

Endpoint `0x01` is 48,000 complex signed-16-bit samples per second. Treating a
frame as `z[n] = I[n] + j Q[n]`, least-squares fitting of 139,392 steady-state
frames gives two negative-frequency complex rotations:

```text
z[n] = C700  * exp(-j 2 pi  700 n / 48000)
     + C1900 * exp(-j 2 pi 1900 n / 48000)
```

The fitted magnitudes are:

| Component | Complex amplitude |
|---|---:|
| -700 Hz | 6,222.2246 counts |
| -1900 Hz | 6,222.2454 counts |

Their phases in the selected analysis window were approximately 1.5000 and
55.5001 degrees. Absolute starting phase is not operationally significant; the
equal amplitudes and negative rotation direction are significant.

After removing the two fitted tones, residual complex RMS is only about 0.573
count, consistent with signed-integer quantization. The measured steady-state
values are:

- I RMS: 6,222.355 counts
- Q RMS: 6,222.141 counts
- complex RMS: 8,799.588 counts
- maximum absolute I or Q: 12,445 counts
- no signed-16-bit clipping

Each 50%-drive tone therefore uses approximately 18.99% of signed-16-bit peak
amplitude, and their instantaneous sum reaches approximately 37.98%.

## Transition behavior

Before keying, endpoint `0x01` changes from duplicated receive audio to zero and
then small non-mono transition samples. Full-amplitude two-tone samples are
present in queued USB data shortly after that transition. They continue briefly
after `SET_TR(0)` because buffers were already queued, then endpoint `0x01`
passes through zero and returns to duplicated receive audio.

The samples do not establish a simple standalone amplitude-ramp formula. The
observed I²C transition-mute sequence remains part of the required safe relay/RF
timing, and a Linux implementation should prequeue deterministic samples before
keying and continue them through unkey cleanup to prevent underruns.

## Implementation consequence

A fixed Linux validation probe can reproduce the observed test without guessing
TX amplitude: generate equal -700 Hz and -1900 Hz complex tones with amplitude
6,222 counts each at 48 kHz, round and clamp the sum to signed 16-bit, and use
the already validated three-second key/unkey lifecycle. This would intentionally
produce RF and therefore requires a new executable review, the dummy load, and
separate explicit permission from KB1JDX before execution.
