// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_audio_stream.h"

#include "test_assert.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static int16_t load_i16(const uint8_t *input)
{
    return (int16_t)((uint16_t)input[0] | ((uint16_t)input[1] << 8));
}

static float maximum_iq_magnitude(const uint8_t *samples, size_t frames)
{
    float maximum = 0.0f;
    for (size_t frame = 0; frame < frames; ++frame) {
        float i = load_i16(&samples[frame * 4]);
        float q = load_i16(&samples[frame * 4 + 2]);
        float magnitude = hypotf(i, q);
        if (magnitude > maximum) maximum = magnitude;
    }
    return maximum;
}

static float streamed_tone_response(flex1500_tx_sideband sideband,
                                    unsigned int drive, float probe_hz)
{
    const float pi = 3.14159265358979323846f;
    flex1500_tx_audio_stream stream;
    CHECK(flex1500_tx_audio_stream_init(&stream, sideband, drive, 1.0f));
    float real = 0.0f;
    float imag = 0.0f;
    for (int base = 0; base < 12000; base += 48) {
        uint8_t microphone[48 * 4] = {0};
        uint8_t output[48 * 4];
        for (int offset = 0; offset < 48; ++offset) {
            float phase = 2.0f * pi * 1000.0f * (base + offset) / 48000.0f;
            int16_t sample = (int16_t)lrintf(16000.0f * cosf(phase));
            microphone[offset * 4] = (uint8_t)sample;
            microphone[offset * 4 + 1] = (uint8_t)((uint16_t)sample >> 8);
        }
        CHECK(flex1500_tx_audio_stream_push_iq16le(
                  &stream, microphone, sizeof(microphone)) == 48);
        CHECK(flex1500_tx_audio_stream_render_iq16le(
                  &stream, output, 48) == 48);
        for (int offset = 0; offset < 48; ++offset) {
            int n = base + offset;
            if (n <= 2000) continue;
            float i = load_i16(&output[offset * 4]);
            float q = load_i16(&output[offset * 4 + 2]);
            float phase = 2.0f * pi * probe_hz * n / 48000.0f;
            real += i * cosf(phase) + q * sinf(phase);
            imag += q * cosf(phase) - i * sinf(phase);
        }
    }
    const flex1500_tx_audio_stats *stats =
        flex1500_tx_audio_stream_stats(&stream);
    CHECK(stats != NULL && stats->microphone_frames == 12000);
    CHECK(stats->output_frames == 12000 && stats->underrun_frames == 0);
    CHECK(stats->dropped_microphone_frames == 0);
    return hypotf(real, imag);
}

int main(void)
{
    float usb_negative = streamed_tone_response(
        FLEX1500_TX_USB, 50, -1000.0f);
    float usb_positive = streamed_tone_response(
        FLEX1500_TX_USB, 50, 1000.0f);
    float lsb_positive = streamed_tone_response(
        FLEX1500_TX_LSB, 50, 1000.0f);
    float lsb_negative = streamed_tone_response(
        FLEX1500_TX_LSB, 50, -1000.0f);
    CHECK(usb_negative > usb_positive * 20.0f);
    CHECK(lsb_positive > lsb_negative * 20.0f);

    float drive_25 = streamed_tone_response(FLEX1500_TX_USB, 25, -1000.0f);
    CHECK(usb_negative > drive_25 * 1.9f);
    CHECK(usb_negative < drive_25 * 2.1f);

    flex1500_tx_audio_stream empty;
    uint8_t silence[96 * 4];
    CHECK(flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 50, 1.0f));
    CHECK(flex1500_tx_audio_stream_render_iq16le(
              &empty, silence, 96) == 96);
    for (size_t index = 0; index < sizeof(silence); ++index) {
        CHECK(silence[index] == 0);
    }
    CHECK(flex1500_tx_audio_stream_stats(&empty)->underrun_frames == 96);

    uint8_t queued_input[32] = {0};
    CHECK(flex1500_tx_audio_stream_push_pcm16le(
              &empty, queued_input, sizeof(queued_input)) == 16);
    CHECK(flex1500_tx_audio_stream_queued(&empty) == 16);
    CHECK(flex1500_tx_audio_stream_stats(&empty)->peak_queued_frames == 16);

    CHECK(!flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 0, 1.0f));
    CHECK(!flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 101, 1.0f));
    CHECK(!flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 50, 0.0f));

    flex1500_tx_audio_stream protected_stream;
    uint8_t loud_input[2048 * 4] = {0};
    uint8_t limited_output[2048 * 4];
    for (size_t frame = 0; frame < 2048; ++frame) {
        int16_t sample = (frame & 1) != 0 ? INT16_MAX : INT16_MIN;
        loud_input[frame * 4] = (uint8_t)sample;
        loud_input[frame * 4 + 1] = (uint8_t)((uint16_t)sample >> 8);
    }
    CHECK(flex1500_tx_audio_stream_init(
        &protected_stream, FLEX1500_TX_USB, 50, 100.0f));
    CHECK(flex1500_tx_audio_stream_push_iq16le(
        &protected_stream, loud_input, sizeof(loud_input)) == 2048);
    CHECK(flex1500_tx_audio_stream_render_iq16le(
        &protected_stream, limited_output, 2048) == 2048);
    const flex1500_tx_audio_stats *protected_stats =
        flex1500_tx_audio_stream_stats(&protected_stream);
    CHECK(protected_stats->input_peak > 0.99f);
    CHECK(protected_stats->input_rms > 0.99f);
    CHECK(protected_stats->post_gain_peak > 99.0f);
    CHECK(protected_stats->limited_frames > 0);
    CHECK(protected_stats->clipped_frames == 0);
    CHECK(protected_stats->output_peak <= 0.501f);
    CHECK(protected_stats->output_rms > 0.0f);

    flex1500_tx_audio_stream compressed_stream;
    CHECK(flex1500_tx_audio_stream_init(
        &compressed_stream, FLEX1500_TX_USB, 50, 100.0f));
    flex1500_tx_audio_stream_set_compressor(&compressed_stream, true);
    CHECK(flex1500_tx_audio_stream_push_iq16le(
        &compressed_stream, loud_input, sizeof(loud_input)) == 2048);
    CHECK(flex1500_tx_audio_stream_render_iq16le(
        &compressed_stream, limited_output, 2048) == 2048);
    const flex1500_tx_audio_stats *compressed_stats =
        flex1500_tx_audio_stream_stats(&compressed_stream);
    CHECK(compressed_stats->post_gain_peak > 99.0f);
    CHECK(compressed_stats->limited_frames < protected_stats->limited_frames);
    CHECK(compressed_stats->output_rms < protected_stats->output_rms);

    flex1500_tx_audio_stream raw;
    uint8_t raw_input[1024 * 4];
    CHECK(flex1500_tx_audio_stream_init(&raw, FLEX1500_TX_USB, 25, 1.0f));
    flex1500_tx_audio_stream_set_raw_iq(&raw, true);
    CHECK(flex1500_tx_audio_stream_available(&raw) ==
          FLEX1500_TX_AUDIO_CAPACITY);
    for (size_t frame = 0; frame < 1024; ++frame) {
        int16_t i = INT16_MAX, q = INT16_MAX;
        raw_input[frame * 4] = (uint8_t)i;
        raw_input[frame * 4 + 1] = (uint8_t)((uint16_t)i >> 8);
        raw_input[frame * 4 + 2] = (uint8_t)q;
        raw_input[frame * 4 + 3] = (uint8_t)((uint16_t)q >> 8);
    }
    CHECK(flex1500_tx_audio_stream_push_iq16le(
              &raw, raw_input, sizeof(raw_input)) == 1024);
    CHECK(flex1500_tx_audio_stream_available(&raw) ==
          FLEX1500_TX_AUDIO_CAPACITY - 1024);
    CHECK(flex1500_tx_audio_stream_push_iq16le(
        &raw, raw_input, sizeof(raw_input)) == 1024);
    CHECK(flex1500_tx_audio_stream_render_iq16le(
        &raw, limited_output, 1024) == 1024);
    CHECK(flex1500_tx_audio_stream_stats(&raw)->limited_frames > 0);
    CHECK(flex1500_tx_audio_stream_stats(&raw)->output_peak <= 0.251f);
    CHECK(maximum_iq_magnitude(limited_output, 1024) <=
          FLEX1500_TX_FULL_DRIVE_PEAK * 0.25f + 1.0f);

    flex1500_tx_audio_stream raw_full_drive;
    CHECK(flex1500_tx_audio_stream_init(
        &raw_full_drive, FLEX1500_TX_USB,
        FLEX1500_TX_MAX_DRIVE_PERCENT, 1.0f));
    flex1500_tx_audio_stream_set_raw_iq(&raw_full_drive, true);
    CHECK(flex1500_tx_audio_stream_push_iq16le(
        &raw_full_drive, raw_input, sizeof(raw_input)) == 1024);
    CHECK(flex1500_tx_audio_stream_render_iq16le(
        &raw_full_drive, limited_output, 1024) == 1024);
    CHECK(maximum_iq_magnitude(limited_output, 1024) <=
          FLEX1500_TX_FULL_DRIVE_PEAK + 1.0f);
    CHECK(flex1500_tx_audio_stream_stats(&raw_full_drive)->clipped_frames == 0);

    flex1500_tx_audio_stream pcm;
    uint8_t pcm_input[4800 * 2] = {0};
    CHECK(flex1500_tx_audio_stream_init(&pcm, FLEX1500_TX_LSB, 50, 1.0f));
    CHECK(flex1500_tx_audio_stream_push_pcm16le(
        &pcm, pcm_input, sizeof(pcm_input)) == 4800);
    return EXIT_SUCCESS;
}
