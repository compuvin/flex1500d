// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network.h"
#include "flex1500/rtl_tcp.h"
#include "test_assert.h"

#include <string.h>

int main(void)
{
    uint8_t header[FLEX1500_RTL_TCP_HEADER_SIZE];
    flex1500_rtl_tcp_header(header);
    CHECK(memcmp(header, "RTL0", 4) == 0);
    for (size_t i = 4; i < sizeof(header); ++i) CHECK(header[i] == 0);

    flex1500_rtl_tcp_parser parser = {0};
    flex1500_rtl_tcp_command command;
    const uint8_t bytes[] = {0x01, 0x01, 0xb2, 0xb8, 0x60};
    for (size_t i = 0; i < sizeof(bytes) - 1; ++i)
        CHECK(!flex1500_rtl_tcp_parse_byte(&parser, bytes[i], &command));
    CHECK(flex1500_rtl_tcp_parse_byte(&parser, bytes[4], &command));
    CHECK(command.id == FLEX1500_RTL_TCP_SET_FREQUENCY);
    CHECK(command.parameter == 28489824);

    const flex1500_iq_sample samples[] = {
        {.i = -32768.0f, .q = 0.0f},
        {.i = 32768.0f, .q = 16384.0f},
    };
    uint8_t framed[64];
    size_t framed_length = flex1500_encode_iq_frame(
        7, samples, 2, framed, sizeof(framed));
    uint8_t output[4];
    CHECK(flex1500_rtl_tcp_encode_iq(framed, framed_length, output,
                                     sizeof(output)) == sizeof(output));
    CHECK(output[0] == 0);
    CHECK(output[1] == 128);
    CHECK(output[2] == 255);
    CHECK(output[3] == 64);

    uint8_t repeated[8];
    CHECK(flex1500_rtl_tcp_encode_iq_repeated(
              framed, framed_length, 2, repeated, sizeof(repeated)) ==
          sizeof(repeated));
    CHECK(repeated[0] == 0 && repeated[1] == 128);
    CHECK(repeated[2] == 0 && repeated[3] == 128);
    CHECK(repeated[4] == 255 && repeated[5] == 64);
    CHECK(repeated[6] == 255 && repeated[7] == 64);

    flex1500_rtl_tcp_resampler resampler;
    flex1500_rtl_tcp_resampler_reset(&resampler, 250000);
    uint8_t resampled[32];
    size_t resampled_length = flex1500_rtl_tcp_resample_iq(
        &resampler, framed, framed_length, resampled, sizeof(resampled));
    CHECK(resampled_length == 22);
    CHECK(resampler.output_rate == 250000);

    /* State and the fractional phase continue across frame boundaries. */
    resampled_length = flex1500_rtl_tcp_resample_iq(
        &resampler, framed, framed_length, resampled, sizeof(resampled));
    CHECK(resampled_length == 20);
    CHECK(resampler.input_samples == 4);

    flex1500_iq_sample ramp[64];
    for (size_t i = 0; i < 64; ++i) {
        ramp[i].i = 32768.0f * (float)i / 64.0f;
        ramp[i].q = 0.0f;
    }
    uint8_t ramp_frame[FLEX1500_IQ_FRAME_HEADER_SIZE + sizeof(ramp)];
    size_t ramp_length = flex1500_encode_iq_frame(
        8, ramp, 64, ramp_frame, sizeof(ramp_frame));
    flex1500_rtl_tcp_resampler_reset(&resampler, 960000);
    uint8_t filtered[64 * 20 * 2];
    size_t filtered_length = flex1500_rtl_tcp_resample_iq(
        &resampler, ramp_frame, ramp_length, filtered, sizeof(filtered));
    CHECK(filtered_length == sizeof(filtered));
    bool changed_between_output_samples = false;
    for (size_t i = 2; i < filtered_length; i += 2) {
        if (filtered[i] != filtered[i - 2] ||
            filtered[i + 1] != filtered[i - 1]) {
            changed_between_output_samples = true;
            break;
        }
    }
    CHECK(changed_between_output_samples);

    flex1500_iq_sample zero[64] = {0};
    uint8_t zero_frame[FLEX1500_IQ_FRAME_HEADER_SIZE + sizeof(zero)];
    size_t zero_length = flex1500_encode_iq_frame(
        9, zero, 64, zero_frame, sizeof(zero_frame));
    flex1500_rtl_tcp_resampler_reset(&resampler, 250000);
    uint8_t centered[64 * 6 * 2];
    size_t centered_length = flex1500_rtl_tcp_resample_iq(
        &resampler, zero_frame, zero_length, centered, sizeof(centered));
    CHECK(centered_length == 668);
    unsigned long sum_i = 0, sum_q = 0;
    for (size_t i = 0; i < centered_length; i += 2) {
        sum_i += centered[i];
        sum_q += centered[i + 1];
    }
    CHECK(sum_i * 2 == (centered_length / 2) * 255);
    CHECK(sum_q * 2 == (centered_length / 2) * 255);
    return 0;
}
