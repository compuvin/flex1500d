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

    CHECK(!flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 0, 1.0f));
    CHECK(!flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 101, 1.0f));
    CHECK(!flex1500_tx_audio_stream_init(
        &empty, FLEX1500_TX_USB, 50, 0.0f));
    return EXIT_SUCCESS;
}
