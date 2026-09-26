// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/rtl_tcp.h"

#include "flex1500/network.h"
#include "flex1500/publisher.h"

#include <math.h>
#include <string.h>

enum { RESAMPLER_RADIUS = 8, RESAMPLER_PHASES = 1024 };

static const double PI = 3.14159265358979323846;
static const float IQ_FULL_SCALE = 32768.0f;
static float resampler_weights[RESAMPLER_PHASES][RESAMPLER_RADIUS * 2 + 1];
static bool resampler_weights_ready;

static void prepare_resampler_weights(void)
{
    if (resampler_weights_ready) return;
    const double cutoff = 0.45;
    for (int phase = 0; phase < RESAMPLER_PHASES; ++phase) {
        double fraction = (double)phase / RESAMPLER_PHASES;
        double total = 0.0;
        for (int tap = -RESAMPLER_RADIUS; tap <= RESAMPLER_RADIUS; ++tap) {
            double distance = fraction - tap;
            double scaled = 2.0 * cutoff * distance;
            double sinc = fabs(scaled) < 1.0e-12
                              ? 1.0
                              : sin(PI * scaled) / (PI * scaled);
            double window_position = distance / (RESAMPLER_RADIUS + 1.0);
            double window = 0.42 + 0.5 * cos(PI * window_position) +
                            0.08 * cos(2.0 * PI * window_position);
            double weight = 2.0 * cutoff * sinc * window;
            resampler_weights[phase][tap + RESAMPLER_RADIUS] =
                (float)weight;
            total += weight;
        }
        for (int tap = 0; tap < RESAMPLER_RADIUS * 2 + 1; ++tap)
            resampler_weights[phase][tap] /= (float)total;
    }
    resampler_weights_ready = true;
}

static uint32_t load_be32(const uint8_t input[4])
{
    return ((uint32_t)input[0] << 24) | ((uint32_t)input[1] << 16) |
           ((uint32_t)input[2] << 8) | input[3];
}

static float load_f32le(const uint8_t input[4])
{
    uint32_t bits = (uint32_t)input[0] | ((uint32_t)input[1] << 8) |
                    ((uint32_t)input[2] << 16) |
                    ((uint32_t)input[3] << 24);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static uint8_t encode_sample(float value)
{
    if (!isfinite(value)) value = 0.0f;
    value /= IQ_FULL_SCALE;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    return (uint8_t)lrintf(value * 127.5f + 127.5f);
}

static uint8_t encode_sample_with_error_feedback(float value, float *error)
{
    if (!isfinite(value)) value = 0.0f;
    value /= IQ_FULL_SCALE;
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    float target = value * 127.5f + 127.5f + *error;
    long encoded = lrintf(target);
    if (encoded < 0) encoded = 0;
    if (encoded > 255) encoded = 255;
    *error = target - (float)encoded;
    return (uint8_t)encoded;
}

void flex1500_rtl_tcp_header(uint8_t output[FLEX1500_RTL_TCP_HEADER_SIZE])
{
    memset(output, 0, FLEX1500_RTL_TCP_HEADER_SIZE);
    memcpy(output, "RTL0", 4);
    /* Tuner type UNKNOWN and zero indexed gain values. */
}

bool flex1500_rtl_tcp_parse_byte(flex1500_rtl_tcp_parser *parser,
                                 uint8_t byte,
                                 flex1500_rtl_tcp_command *command)
{
    if (parser == NULL || command == NULL) return false;
    parser->pending[parser->pending_length++] = byte;
    if (parser->pending_length != FLEX1500_RTL_TCP_COMMAND_SIZE) return false;
    command->id = parser->pending[0];
    command->parameter = load_be32(&parser->pending[1]);
    parser->pending_length = 0;
    return true;
}

size_t flex1500_rtl_tcp_encode_iq_repeated(const uint8_t *framed_iq,
                                           size_t framed_length,
                                           uint32_t repeat, uint8_t *output,
                                           size_t capacity)
{
    if (framed_iq == NULL || output == NULL ||
        framed_length < FLEX1500_IQ_FRAME_HEADER_SIZE ||
        memcmp(framed_iq, "F15I", 4) != 0 ||
        framed_iq[4] != FLEX1500_IQ_FRAME_VERSION ||
        framed_iq[5] != FLEX1500_IQ_FORMAT_COMPLEX_F32LE || repeat == 0)
        return 0;
    uint32_t count = load_be32(&framed_iq[16]);
    size_t required_input = FLEX1500_IQ_FRAME_HEADER_SIZE + (size_t)count * 8;
    if ((size_t)count > SIZE_MAX / 2 / repeat) return 0;
    size_t required_output = (size_t)count * 2 * repeat;
    if (required_input != framed_length || required_output > capacity) return 0;
    for (uint32_t index = 0; index < count; ++index) {
        const uint8_t *sample = &framed_iq[FLEX1500_IQ_FRAME_HEADER_SIZE +
                                           (size_t)index * 8];
        uint8_t i_sample = encode_sample(load_f32le(sample));
        /* FLEX-1500 wire Q has the opposite sign from the conventional
         * complex orientation expected by rtl_tcp clients. */
        uint8_t q_sample = encode_sample(-load_f32le(sample + 4));
        for (uint32_t copy = 0; copy < repeat; ++copy) {
            size_t output_index = ((size_t)index * repeat + copy) * 2;
            output[output_index] = i_sample;
            output[output_index + 1] = q_sample;
        }
    }
    return required_output;
}

size_t flex1500_rtl_tcp_encode_iq(const uint8_t *framed_iq,
                                  size_t framed_length, uint8_t *output,
                                  size_t capacity)
{
    return flex1500_rtl_tcp_encode_iq_repeated(
        framed_iq, framed_length, 1, output, capacity);
}

void flex1500_rtl_tcp_resampler_reset(flex1500_rtl_tcp_resampler *resampler,
                                      uint32_t output_rate)
{
    if (resampler == NULL) return;
    prepare_resampler_weights();
    memset(resampler, 0, sizeof(*resampler));
    resampler->output_rate = output_rate;
}

static float source_sample(const float *current, uint32_t current_count,
                           const float *history, size_t history_length,
                           uint64_t block_start, int64_t index)
{
    if (index >= 0 && (uint64_t)index >= block_start) {
        uint64_t offset = (uint64_t)index - block_start;
        if (offset < current_count) return current[offset];
        return 0.0f;
    }
    if (index < 0 || block_start - (uint64_t)index > history_length)
        return 0.0f;
    return history[history_length - (size_t)(block_start - (uint64_t)index)];
}

static float interpolate(const float *current, uint32_t current_count,
                         const float *history, size_t history_length,
                         uint64_t block_start, double position)
{
    int64_t center = (int64_t)floor(position);
    double fraction = position - floor(position);
    int phase = (int)lrint(fraction * RESAMPLER_PHASES);
    if (phase == RESAMPLER_PHASES) {
        ++center;
        phase = 0;
    }
    double total = 0.0;
    /* Reconstruct only the useful 48 kHz passband.  The precomputed
     * Blackman-windowed sinc suppresses the former repeater's images without
     * calculating trigonometric functions in the live sample path. */
    for (int tap = -RESAMPLER_RADIUS; tap <= RESAMPLER_RADIUS; ++tap) {
        int64_t index = center + tap;
        double weight =
            resampler_weights[phase][tap + RESAMPLER_RADIUS];
        total += weight * source_sample(current, current_count, history,
                                        history_length, block_start, index);
    }
    return (float)total;
}

size_t flex1500_rtl_tcp_resample_iq(flex1500_rtl_tcp_resampler *resampler,
                                    const uint8_t *framed_iq,
                                    size_t framed_length, uint8_t *output,
                                    size_t capacity)
{
    if (resampler == NULL || framed_iq == NULL || output == NULL ||
        resampler->output_rate < FLEX1500_RTL_TCP_INPUT_RATE ||
        framed_length < FLEX1500_IQ_FRAME_HEADER_SIZE ||
        memcmp(framed_iq, "F15I", 4) != 0 ||
        framed_iq[4] != FLEX1500_IQ_FRAME_VERSION ||
        framed_iq[5] != FLEX1500_IQ_FORMAT_COMPLEX_F32LE)
        return 0;

    uint32_t count = load_be32(&framed_iq[16]);
    size_t required_input = FLEX1500_IQ_FRAME_HEADER_SIZE + (size_t)count * 8;
    if (required_input != framed_length || count > FLEX1500_PUBLISHER_MAX_SAMPLES)
        return 0;

    float current_i[FLEX1500_PUBLISHER_MAX_SAMPLES];
    float current_q[FLEX1500_PUBLISHER_MAX_SAMPLES];
    for (uint32_t index = 0; index < count; ++index) {
        const uint8_t *sample = &framed_iq[FLEX1500_IQ_FRAME_HEADER_SIZE +
                                           (size_t)index * 8];
        current_i[index] = load_f32le(sample);
        current_q[index] = -load_f32le(sample + 4);
    }

    uint64_t block_start = resampler->input_samples;
    uint64_t block_end = block_start + count;
    uint64_t block_span = (uint64_t)count * resampler->output_rate;
    size_t produced = 0;
    while (resampler->phase_numerator < block_span) {
        if (produced + 2 > capacity) return 0;
        double nominal = (double)block_start +
                         (double)resampler->phase_numerator /
                             resampler->output_rate;
        double delayed = nominal - (RESAMPLER_RADIUS + 1.0);
        float i_sample = interpolate(
            current_i, count, resampler->history_i,
            resampler->history_length, block_start, delayed);
        float q_sample = interpolate(
            current_q, count, resampler->history_q,
            resampler->history_length, block_start, delayed);
        output[produced++] = encode_sample_with_error_feedback(
            i_sample, &resampler->quantization_error_i);
        output[produced++] = encode_sample_with_error_feedback(
            q_sample, &resampler->quantization_error_q);
        resampler->phase_numerator += FLEX1500_RTL_TCP_INPUT_RATE;
    }
    resampler->phase_numerator -= block_span;

    size_t keep = count < FLEX1500_RTL_TCP_RESAMPLER_HISTORY
                      ? count
                      : FLEX1500_RTL_TCP_RESAMPLER_HISTORY;
    if (keep < FLEX1500_RTL_TCP_RESAMPLER_HISTORY) {
        size_t old_keep = FLEX1500_RTL_TCP_RESAMPLER_HISTORY - keep;
        if (old_keep > resampler->history_length)
            old_keep = resampler->history_length;
        memmove(resampler->history_i,
                resampler->history_i + resampler->history_length - old_keep,
                old_keep * sizeof(float));
        memmove(resampler->history_q,
                resampler->history_q + resampler->history_length - old_keep,
                old_keep * sizeof(float));
        memcpy(resampler->history_i + old_keep, current_i + count - keep,
               keep * sizeof(float));
        memcpy(resampler->history_q + old_keep, current_q + count - keep,
               keep * sizeof(float));
        resampler->history_length = old_keep + keep;
    } else {
        memcpy(resampler->history_i, current_i + count - keep,
               keep * sizeof(float));
        memcpy(resampler->history_q, current_q + count - keep,
               keep * sizeof(float));
        resampler->history_length = keep;
    }
    resampler->input_samples = block_end;
    return produced;
}
