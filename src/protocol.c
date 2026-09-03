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

bool flex1500_decode_physical_inputs(const uint8_t *packet,
                                     size_t packet_length,
                                     flex1500_physical_inputs *inputs)
{
    if (packet == NULL || packet_length < 1 || inputs == NULL) return false;
    *inputs = (flex1500_physical_inputs){
        .raw_status = packet[0],
        .mic_ptt = (packet[0] & UINT8_C(0x01)) == 0,
        .flexwire_ptt = (packet[0] & UINT8_C(0x08)) == 0,
        .dash = (packet[0] & UINT8_C(0x10)) == 0,
        .dot = (packet[0] & UINT8_C(0x20)) == 0,
    };
    return true;
}

bool flex1500_physical_inputs_equal(const flex1500_physical_inputs *left,
                                    const flex1500_physical_inputs *right)
{
    return left != NULL && right != NULL && left->mic_ptt == right->mic_ptt &&
           left->flexwire_ptt == right->flexwire_ptt &&
           left->dash == right->dash && left->dot == right->dot;
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

bool flex1500_build_rx_gain_request(
    uint8_t index, flex1500_rx_gain gain,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    if (gain < FLEX1500_RX_GAIN_MINUS_10_DB ||
        gain > FLEX1500_RX_GAIN_PLUS_30_DB) {
        return false;
    }
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_TRX_PREAMP);
    store_be32(&packet[8], (uint32_t)gain);
    return true;
}

bool flex1500_build_rx_antenna_request(
    uint8_t index, flex1500_rx_antenna antenna,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    if (antenna < FLEX1500_RX_ANTENNA_PA ||
        antenna > FLEX1500_RX_ANTENNA_XVTX_COM) {
        return false;
    }
    memset(packet, 0, FLEX1500_COMMAND_PACKET_SIZE);
    packet[0] = index;
    store_be32(&packet[4], FLEX1500_OP_SET_RX1_ANT);
    store_be32(&packet[8], (uint32_t)antenna);
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

bool flex1500_pa_filter_for_frequency(uint32_t frequency_hz,
                                      uint32_t *filter)
{
    static const uint32_t upper_bounds[] = {
        2500000, 5000000, 8800000, 17500000, 24000000, 35000000,
    };
    static const uint32_t filters[] = {7, 6, 5, 4, 3, 2};

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

bool flex1500_physical_mic_frequency_allowed(
    uint32_t frequency_hz, bool upper_sideband)
{
    static const struct {
        uint32_t low;
        uint32_t high;
    } voice_allocations[] = {
        {1800000, 2000000}, {3500000, 4000000},
        {7000000, 7300000}, {14000000, 14350000},
        {18068000, 18168000}, {21000000, 21450000},
        {24890000, 24990000}, {28000000, 29700000},
        {50000000, 54000000},
    };
    for (size_t index = 0;
         index < sizeof(voice_allocations) / sizeof(voice_allocations[0]);
         ++index) {
        if (frequency_hz >= voice_allocations[index].low &&
            frequency_hz <= voice_allocations[index].high) return true;
    }

    if (!upper_sideband) return false;
    if (frequency_hz >= 5351500 && frequency_hz <= 5363500) return true;
    static const uint32_t sixty_meter_carriers[] = {
        5330500, 5346500, 5371500, 5403500,
    };
    for (size_t index = 0;
         index < sizeof(sixty_meter_carriers) /
                     sizeof(sixty_meter_carriers[0]); ++index) {
        if (frequency_hz == sixty_meter_carriers[index]) return true;
    }
    return false;
}

bool flex1500_usb_tune_frequency_to_tuning_word(uint32_t carrier_hz,
                                                uint32_t tone_hz,
                                                uint32_t *tuning_word)
{
    if (carrier_hz < FLEX1500_MIN_RX_FREQUENCY_HZ ||
        carrier_hz > FLEX1500_MAX_RX_FREQUENCY_HZ || tone_hz >= carrier_hz) {
        return false;
    }
    return flex1500_rx_frequency_to_tuning_word(carrier_hz - tone_hz,
                                                tuning_word);
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
