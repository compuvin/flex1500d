// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/rtl_tcp.h"

#include "flex1500/network.h"

#include <math.h>
#include <string.h>

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
    if (value < -1.0f) value = -1.0f;
    if (value > 1.0f) value = 1.0f;
    return (uint8_t)lrintf(value * 127.5f + 127.5f);
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
