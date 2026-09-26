// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_RTL_TCP_H
#define FLEX1500_RTL_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLEX1500_RTL_TCP_HEADER_SIZE = 12,
    FLEX1500_RTL_TCP_COMMAND_SIZE = 5,
};

typedef enum flex1500_rtl_tcp_command_id {
    FLEX1500_RTL_TCP_SET_FREQUENCY = 0x01,
    FLEX1500_RTL_TCP_SET_SAMPLE_RATE = 0x02,
    FLEX1500_RTL_TCP_SET_GAIN_MODE = 0x03,
    FLEX1500_RTL_TCP_SET_GAIN = 0x04,
} flex1500_rtl_tcp_command_id;

typedef struct flex1500_rtl_tcp_command {
    uint8_t id;
    uint32_t parameter;
} flex1500_rtl_tcp_command;

typedef struct flex1500_rtl_tcp_parser {
    uint8_t pending[FLEX1500_RTL_TCP_COMMAND_SIZE];
    size_t pending_length;
} flex1500_rtl_tcp_parser;

enum {
    FLEX1500_RTL_TCP_INPUT_RATE = 48000,
    FLEX1500_RTL_TCP_RESAMPLER_HISTORY = 64,
};

typedef struct flex1500_rtl_tcp_resampler {
    uint32_t output_rate;
    uint64_t phase_numerator;
    uint64_t input_samples;
    float history_i[FLEX1500_RTL_TCP_RESAMPLER_HISTORY];
    float history_q[FLEX1500_RTL_TCP_RESAMPLER_HISTORY];
    size_t history_length;
} flex1500_rtl_tcp_resampler;

void flex1500_rtl_tcp_header(uint8_t output[FLEX1500_RTL_TCP_HEADER_SIZE]);
bool flex1500_rtl_tcp_parse_byte(flex1500_rtl_tcp_parser *parser,
                                 uint8_t byte,
                                 flex1500_rtl_tcp_command *command);
size_t flex1500_rtl_tcp_encode_iq(const uint8_t *framed_iq,
                                  size_t framed_length, uint8_t *output,
                                  size_t capacity);
void flex1500_rtl_tcp_resampler_reset(flex1500_rtl_tcp_resampler *resampler,
                                      uint32_t output_rate);
size_t flex1500_rtl_tcp_resample_iq(flex1500_rtl_tcp_resampler *resampler,
                                    const uint8_t *framed_iq,
                                    size_t framed_length, uint8_t *output,
                                    size_t capacity);
size_t flex1500_rtl_tcp_encode_iq_repeated(const uint8_t *framed_iq,
                                           size_t framed_length,
                                           uint32_t repeat, uint8_t *output,
                                           size_t capacity);

#endif
