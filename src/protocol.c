// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <string.h>

bool flex1500_rx_probe_transfer_allowed(flex1500_transfer_kind kind,
                                        uint8_t endpoint)
{
    if (kind == FLEX1500_TRANSFER_ISOCHRONOUS) {
        return endpoint == FLEX1500_EP_SAMPLE_IN;
    }

    if (kind == FLEX1500_TRANSFER_INTERRUPT) {
        return endpoint == FLEX1500_EP_STATUS_IN;
    }

    /* All control transfers are blocked during the RX-only probe phase. */
    return false;
}

const char *flex1500_endpoint_name(uint8_t endpoint)
{
    switch (endpoint) {
    case FLEX1500_EP_SAMPLE_OUT:
        return "sample OUT";
    case FLEX1500_EP_SAMPLE_IN:
        return "probable RX sample IN";
    case FLEX1500_EP_STATUS_IN:
        return "probable status IN";
    case FLEX1500_EP_COMMAND_OUT:
        return "probable command OUT";
    default:
        return "unknown";
    }
}

void flex1500_decode_iq_frame(const uint8_t frame[4], int16_t *sample_i,
                              int16_t *sample_q)
{
    uint16_t raw_i = (uint16_t)frame[0] | ((uint16_t)frame[1] << 8);
    uint16_t raw_q = (uint16_t)frame[2] | ((uint16_t)frame[3] << 8);

    *sample_i = (int16_t)raw_i;
    *sample_q = (int16_t)raw_q;
}

static void store_be32(uint8_t destination[4], uint32_t value)
{
    destination[0] = (uint8_t)(value >> 24);
    destination[1] = (uint8_t)(value >> 16);
    destination[2] = (uint8_t)(value >> 8);
    destination[3] = (uint8_t)value;
}

bool flex1500_firmware_read_request_allowed(uint32_t opcode, uint32_t param1,
                                            uint32_t param2)
{
    return opcode == FLEX1500_OP_GET_FIRMWARE_REV && param1 == 0 && param2 == 0;
}

bool flex1500_build_firmware_read_request(
    uint8_t index, uint32_t opcode, uint32_t param1, uint32_t param2,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    if (!flex1500_firmware_read_request_allowed(opcode, param1, param2)) {
        return false;
    }

    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], opcode);
    store_be32(&packet[8], param1);
    store_be32(&packet[12], param2);
    return true;
}

bool flex1500_decode_u32_response(const uint8_t *packet, size_t packet_length,
                                  uint8_t expected_index, uint32_t *result)
{
    if (packet_length < 8 || packet[1] != 1 || packet[2] != expected_index) {
        return false;
    }

    *result = ((uint32_t)packet[4] << 24) |
              ((uint32_t)packet[5] << 16) |
              ((uint32_t)packet[6] << 8) | (uint32_t)packet[7];
    return true;
}

bool flex1500_build_initialize_request(
    uint8_t index, uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_INITIALIZE);
    return true;
}

bool flex1500_rx_frequency_to_tuning_word(uint32_t frequency_hz,
                                         uint32_t *tuning_word)
{
    if (tuning_word == NULL || frequency_hz < FLEX1500_MIN_RX_FREQUENCY_HZ ||
        frequency_hz > FLEX1500_MAX_RX_FREQUENCY_HZ) {
        return false;
    }
    double word = (double)UINT32_MAX * 2.0 * (double)frequency_hz /
                  384000000.0;
    *tuning_word = (uint32_t)word;
    return true;
}

bool flex1500_build_rx_tune_request(
    uint8_t index, uint32_t frequency_hz,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE], uint32_t *tuning_word)
{
    uint32_t word;
    if (!flex1500_rx_frequency_to_tuning_word(frequency_hz, &word)) {
        return false;
    }
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_RX1_FREQ_TW);
    store_be32(&packet[8], word);
    if (tuning_word != NULL) *tuning_word = word;
    return true;
}

bool flex1500_build_rx_filter_request(
    uint8_t index, uint32_t filter,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    if (filter > 11) return false;
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_RX1_FILTER);
    store_be32(&packet[8], filter);
    return true;
}

bool flex1500_rx_filter_for_frequency(uint32_t frequency_hz,
                                      uint32_t *filter)
{
    static const uint32_t upper_bounds[] = {
        480000, 880000, 1600000, 2300000, 3500000, 5200000,
        7700000, 11400000, 17000000, 25300000, 37600000,
        56000000,
    };
    static const uint32_t filters[] = {0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};

    if (filter == NULL || frequency_hz < FLEX1500_MIN_RX_FREQUENCY_HZ ||
        frequency_hz > FLEX1500_MAX_RX_FREQUENCY_HZ) {
        return false;
    }
    for (size_t index = 0; index < sizeof(upper_bounds) / sizeof(upper_bounds[0]);
         ++index) {
        if (frequency_hz < upper_bounds[index]) {
            *filter = filters[index];
            return true;
        }
    }
    *filter = 1;
    return true;
}

bool flex1500_build_pa_filter_request(
    uint8_t index, uint32_t filter,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    if (filter > 7) return false;
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_PA_FILTER);
    store_be32(&packet[8], filter);
    return true;
}

bool flex1500_build_tuning_word_request(
    uint8_t index, uint32_t tuning_word,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_RX1_FREQ_TW);
    store_be32(&packet[8], tuning_word);
    return true;
}

bool flex1500_build_tr_request(
    uint8_t index, bool transmit,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_TR);
    store_be32(&packet[8], transmit ? 1U : 0U);
    return true;
}

bool flex1500_build_amp_tx1_request(
    uint8_t index, bool enabled,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_AMP_TX1);
    store_be32(&packet[8], enabled ? 1U : 0U);
    return true;
}

bool flex1500_build_transition_mute_request(
    uint8_t index, bool muted,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_I2C_WRITE_2_VALUE);
    store_be32(&packet[8], UINT32_C(0x30));
    store_be32(&packet[12], muted ? UINT32_C(0x2500) : UINT32_C(0x25c0));
    return true;
}
