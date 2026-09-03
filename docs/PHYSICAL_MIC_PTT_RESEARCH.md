<!-- SPDX-License-Identifier: GPL-3.0-only -->

# Physical microphone and PTT research

## Why the microphone does not transmit under `flex1500d`

The FLEX-1500 is not an autonomous analog transmitter when controlled by the
current Linux daemon. PowerSDR continuously participates in both PTT handling
and the full-duplex USB audio pipeline. The receive-only daemon intentionally
does neither of the transmit-side jobs.

The pinned PowerSDR source establishes the following architecture:

1. PowerSDR continuously submits an interrupt-IN listener on endpoint `0x83`.
2. Byte 0 of each status message carries hardware input bits. PowerSDR watches
   mask `0x01` for microphone PTT, `0x08` for FlexWire PTT, `0x10` for dash,
   and `0x20` for dot, invoking a callback whenever a bit changes.
3. Its audio worker continuously reads interleaved samples from isochronous
   endpoint `0x82`, passes both channels through the application's DSP callback,
   converts the two DSP output channels back to signed 16-bit samples, and
   writes them continuously to endpoint `0x01`.
4. Transmit also requires the already researched mute, T/R, PA-filter, drive,
   and amplifier sequencing. PTT detection by itself must never key the radio.

This explains the observation that the physical mic/PTT works after PowerSDR
starts but not while `flex1500d` is running. Our daemon receives endpoint
`0x82` as RX I/Q, does not maintain the asynchronous `0x83` hardware-event
listener, does not configure/interpret the microphone input path, and never
creates or sends TX I/Q on endpoint `0x01`.

## Confirmed microphone input path

An approved live test on August 31, 2026 used 28.475 MHz, the recorded 50%
drive setting, a three-second key interval, and a suitable dummy load and
watt/SWR meter. The probe continuously sent zero-valued I/Q samples to endpoint
`0x01`, so it exercised the T/R and input paths without transmitting voice
modulation. It simultaneously captured endpoint `0x82` while KB1JDX spoke into
the physical microphone.

The capture conclusively showed that `SET_TR(1)` changes the endpoint-`0x82`
input path:

- before keying and after restoration, both channels contained correlated
  receive I/Q;
- while keyed, channel 0 contained recognizable microphone audio and channel 1
  was exactly zero;
- 663,552 input bytes (165,888 stereo frames at 48 kHz) were captured over
  3.456 seconds; and
- all normal unkey and receiver-restoration commands completed successfully.

The raw research capture is intentionally outside the repository at
`/tmp/flex1500-mic-input-28475000.iq16le`. A mono listening copy, trimmed to the
keyed interval and high-pass filtered at 80 Hz, is at
`/tmp/flex1500-mic-input-28475000.wav` on the test machine.

The endpoint-`0x83` listener reports PTT changes but did not provide an initial
released event during daemon testing. The daemon therefore arms physical PTT
after initialization or recovery has successfully established its explicit
unkeyed RX state and completed TX preparation. It does not discard the first
real press while waiting for a release event that the radio will not emit.

During initial daemon integration, endpoint-`0x82` microphone packets were
accidentally enqueued twice: once in the USB completion callback and again in
the packet-processing function. Because endpoint-`0x01` consumed only one
frame for each output frame, this duplication produced half-speed, deep audio
and eventually overflowed the microphone FIFO. The duplicate enqueue was
removed; each completed input packet now reaches the TX audio stream exactly
once.

This establishes the physical microphone sample transport, but it does not yet
establish a safe modulation chain. The keyed microphone channel has a DC offset
and still needs high-pass filtering, speech-band filtering, level limiting,
USB/LSB conversion, bounded I/Q scaling, and offline spectral tests before any
microphone-derived samples are sent to endpoint `0x01`.

## Offline SSB modulation result

The first offline transmit DSP stage is now implemented in `tx_dsp.c`. It:

- removes microphone DC with a first-order high-pass stage;
- applies a 129-tap, Hamming-windowed 300–3,000 Hz speech filter;
- uses a 129-tap Hilbert transformer to form analytic I/Q;
- generates negative-frequency complex samples for USB, matching the convention
  measured in PowerSDR's proven two-tone capture, and positive-frequency samples
  for LSB; and
- applies 10 ms edge fades and scales the complete recording to a maximum
  12,000-count complex magnitude, below the approximately 12,444-count peak of
  the previously demonstrated 50%-drive two-tone waveform.

`flex1500-mic-iq-offline` performs this conversion without linking to libusb or
opening the radio. For example:

```console
./build/flex1500-mic-iq-offline input.pcm output.iq16le usb 50
```

The input is 48 kHz interleaved signed-16 stereo PCM; only channel 0 is used.
The output is 48 kHz interleaved signed-16 I/Q. Drive must be explicitly set
from 1 through 100 percent and linearly scales the captured PowerSDR full-drive
reference of 24,890 counts; 50% therefore targets 12,445 counts. A synthetic-
tone unit test checks both sideband orientations and requires at least 20:1
wanted-to-image response.

The keyed 2.800-second portion of KB1JDX's microphone capture produced
`/tmp/flex1500-mic-usb-28475000.iq16le`. Its measured complex peak is 11,999.958
counts, complex RMS is 1,388.749 counts, negative-sideband energy exceeds its
positive-frequency image by 32.96 dB, and speech-band energy exceeds energy
outside the selected band by 19.02 dB.

## Live prerecorded-speech result

With KB1JDX's explicit approval, the bounded prerecorded USB waveform was sent
at 28.475 MHz using the sample scaling associated with PowerSDR's 50% drive
setting. Three three-second tests into a suitable dummy load completed their
normal key, unkey, receiver-restoration, and PA-filter-reset sequences. KB1JDX
heard the recording on a monitor receiver and measured approximately 1.75 W
peak speech power after calibrating the wattmeter. No emergency cleanup was
required.

This is a successful modulation-path test, not a full power calibration. SSB
speech has a changing envelope, and the test waveform's complex RMS was only
1,388.749 counts even though its conservative peak limit was 12,000 counts.
A fixed-tone measurement is required to characterize RF output power.

## Full-drive two-tone measurement

The PowerSDR captures show that 50% drive produces approximately 0.507 times
the endpoint-`0x01` I/Q amplitude of 100% drive. On that evidence, an approved
three-second 100%-drive two-tone test used equal −700 and −1900 Hz components
of 12,444 counts each, with a combined digital peak of 24,888 counts. The test
completed its normal unkey and restoration sequence, and KB1JDX measured about
3 W on the wattmeter.

That reading must not be treated as proof that more sample amplitude is needed.
Two equal tones have twice as much peak-envelope power as average power, while
many wattmeters primarily indicate average power. A roughly 3 W average reading
could therefore correspond to roughly 6 W PEP if the meter and transmitter are
responding linearly. A conservative constant-envelope single-tone measurement
at 12,444 counts is the next calibration point.

That approved three-second −700 Hz single-tone test subsequently completed its
normal unkey and restoration sequence. With a constant complex magnitude of
12,444 counts, KB1JDX observed approximately 3.5–4 W. This is a substantially
clearer reference than the two-tone or speech readings and is reasonably close
to the radio's nominal 5 W rating. The difference between the approximately
3 W two-tone indication and the higher single-tone indication also shows that
the present wattmeter/transmitter combination cannot be modeled reliably from
the ideal two-tone average-to-PEP ratio alone. Further amplitude increases must
therefore be incremental, separately approved, and measured into the dummy
load; 12,444 counts remains the established conservative constant-envelope
reference.

## Evidence and remaining unknowns

PowerSDR's callback code proves the status-bit masks and event-driven listener.
Its `checkBit` implementation also proves that these inputs are active-low:
the logical pressed state is true when the corresponding status bit is clear.

An August 31, 2026 Linux-only test then confirmed the physical microphone PTT
transition directly, without PowerSDR and without transmitting. The standalone
listener claimed interface 3 and performed interrupt-IN reads from endpoint
`0x83` only; it sent no initialization packet, command, sample output, or other
USB OUT transfer. Pressing PTT produced the one-byte packet `38`, decoded as
microphone PTT pressed with FlexWire PTT, dash, and dot released. Releasing PTT
produced `39`, decoded as all four inputs released. This confirms both polarity
and exact press/release values under Linux.
Existing captures contain endpoint-`0x83` command responses whose first byte is
`0x39`, but the transmit captures used software MOX rather than a deliberately
captured physical mic-PTT press. They therefore do not establish the electrical
polarity or exact press/release byte sequence. That must be captured or tested
read-only before implementing PTT state handling.

The audio worker proves that the host transforms the two incoming channels into
two outgoing channels. The live test establishes that the existing T/R command
sequence performs the receive-I/Q to microphone-input switch.

The reusable `tx_audio_stream` component now accepts arbitrary chunks of the
endpoint-style interleaved input, extracts the physical microphone channel,
preserves DC-removal, bandpass, and Hilbert-transform state across chunks, and
continuously renders mode-correct USB or LSB I/Q. It applies the established
24,890-count full-drive reference, configurable drive and microphone gain,
startup fade, clipping accounting, input-overflow accounting, and zero-I/Q
output during microphone underruns. Offline spectral tests confirm the radio's
USB-negative/LSB-positive orientation and drive linearity.

The production USB backend now contains that scheduler connection. While its
internal microphone-TX operation is active, each completed endpoint-`0x82`
packet feeds the persistent microphone DSP buffer and each completed
endpoint-`0x01` transfer is refilled with newly rendered USB or LSB I/Q before
resubmission. Initial and underrun output is zero-I/Q, never stale samples. The
start sequence reuses the validated PA selection, amplifier preparation,
pre-roll, transition-mute, exact-frequency, key, and transition-unmute order;
stop uses the shared guaranteed-unkey cleanup executor.

The endpoint-`0x82` packet path is exclusive while physical microphone TX is
active: after keying, packets feed the microphone DSP and are not published as
receive I/Q. This prevents a browser or Soapy RX client from demodulating the
microphone input as a transmit monitor. The known-working eight-transfer,
50-packet endpoint-`0x01` geometry is retained while timing is measured more
carefully. The microphone FIFO was increased from 8,192 to 65,536 frames to
absorb the initial zero-IQ backlog and bursty libusb callback delivery without
discarding proven 48 kHz microphone samples.

The transmit-enabled daemon now invokes this backend only from a changed
physical microphone PTT edge. TX-disabled startup remains observational.
After initialization or recovery has explicitly established unkeyed RX state
and completed TX preparation, physical PTT is armed immediately; endpoint
`0x83` does not emit an initial release event, so waiting for one discarded
the first legitimate press. Key-down snapshots the known frequency, USB/LSB
mode, drive, and microphone gain. Unsupported modes, unset frequencies, and
frequencies outside the configured voice-allocation policy are rejected before
ownership is requested. HTTP and SoapySDR still expose no general PTT or TX
sample stream.

## Integrated live physical-microphone validation

On September 2, 2026, KB1JDX repeatedly tested physical-microphone USB
transmission with the transmit-enabled daemon, a suitable dummy load and watt/
SWR meter, and a separate monitor receiver. An initial integration defect had
enqueued every endpoint-`0x82` microphone packet twice, producing half-speed,
deep audio and FIFO overflow. Removing the duplicate enqueue restored natural
voice pitch. Subsequent repeated key/unkey tests produced intelligible audio,
adequate subjective modulation level, and normal receive restoration.

The first press after daemon startup was initially rejected because the
release-to-arm policy waited for an endpoint-`0x83` release event that the radio
does not send until after a press. Live counters showed all three press/release
pairs, two TX starts/stops, one rejected request, and no USB, radio-command, or
cleanup errors. The daemon now arms PTT after its successful explicit startup
or recovery cleanup/preparation sequence; repeated testing confirmed that the
first and later presses operate normally.

This validates the integrated USB voice path but is not an ALC or calibrated
modulation-level measurement. The 10 dB microphone-gain default remains
unchanged pending objective level or ALC instrumentation. LSB orientation is
covered by deterministic offline spectral tests but still needs a separate
live dummy-load test.

## Safe implementation order

- [x] Capture physical mic PTT press/release under Linux without transmitting;
  this superseded the proposed PowerSDR/dummy-load capture.
- [x] Add a receive-only endpoint-`0x83` status listener and report decoded PTT,
  FlexWire, dot, and dash state without acting on it.
- [x] Determine and test the microphone input path while sending zero-valued TX
  I/Q so that microphone audio is not modulated onto RF.
- [x] Build offline microphone DSP and TX-IQ framing tests from recorded input.
- [x] Convert arbitrarily chunked live microphone samples into continuous USB
  or LSB I/Q with persistent DSP state and safe underrun output.
- [ ] Complete the exclusive controlling-client model, maximum-key timer,
  disconnect unkey, startup inhibit, and explicit TX-enable interlocks.
- [ ] Independently review the complete key, modulate, and guaranteed-unkey
  state machine before connecting it to the daemon or API.

The fixed Tune carrier and physical microphone USB/LSB path are enabled only in
the distinct transmit-enabled daemon mode. General HTTP PTT, arbitrary network
TX samples, the web page, and SoapySDR transmit remain disconnected from the
transmitter.
