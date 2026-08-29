// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include "test_assert.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    int16_t sample_i;
    int16_t sample_q;
    const uint8_t iq_frame[] = {0x34, 0x12, 0x00, 0x80};
    uint8_t request[FLEX1500_COMMAND_PACKET_SIZE];
    const uint8_t expected_request[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x07, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xb0, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const uint8_t response[] = {0x00, 0x01, 0x07, 0x00,
                                0x00, 0x05, 0x03, 0x18};
    const uint8_t expected_initialize[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xc3, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t initialize_request[FLEX1500_COMMAND_PACKET_SIZE];
    const uint8_t expected_tune[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05, 0x43, 0x0d, 0x55,
        0x55, 0x55, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t tune_request[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t tuning_word = 0;
    const uint8_t expected_filter_5[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x0c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xe9, 0x00, 0x00,
        0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t filter_request[FLEX1500_COMMAND_PACKET_SIZE];
    const uint8_t expected_pa_filter_5[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xec, 0x00, 0x00,
        0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t pa_filter_request[FLEX1500_COMMAND_PACKET_SIZE];
    const uint8_t expected_tr_on[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x06, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0xfc, 0x00, 0x00,
        0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    const uint8_t expected_mute[FLEX1500_COMMAND_PACKET_SIZE] = {
        0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0xfc, 0x00, 0x00,
        0x00, 0x30, 0x00, 0x00, 0x25, 0x00, 0x00, 0x00, 0x00, 0x00,
    };
    uint8_t tx_request[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t response_value = 0;
    uint32_t selected_filter = UINT32_MAX;

    CHECK(flex1500_rx_probe_transfer_allowed(
        FLEX1500_TRANSFER_ISOCHRONOUS, FLEX1500_EP_SAMPLE_IN));
    CHECK(flex1500_rx_probe_transfer_allowed(
        FLEX1500_TRANSFER_INTERRUPT, FLEX1500_EP_STATUS_IN));

    CHECK(!flex1500_rx_probe_transfer_allowed(
        FLEX1500_TRANSFER_ISOCHRONOUS, FLEX1500_EP_SAMPLE_OUT));
    CHECK(!flex1500_rx_probe_transfer_allowed(
        FLEX1500_TRANSFER_INTERRUPT, FLEX1500_EP_COMMAND_OUT));

    CHECK(!flex1500_rx_probe_transfer_allowed(
        FLEX1500_TRANSFER_CONTROL, 0x00));
    CHECK(!flex1500_rx_probe_transfer_allowed(
        FLEX1500_TRANSFER_CONTROL, 0x80));

    for (unsigned int endpoint = 0; endpoint <= UINT8_MAX; ++endpoint) {
        bool iso_allowed = flex1500_rx_probe_transfer_allowed(
            FLEX1500_TRANSFER_ISOCHRONOUS, (uint8_t)endpoint);
        bool interrupt_allowed = flex1500_rx_probe_transfer_allowed(
            FLEX1500_TRANSFER_INTERRUPT, (uint8_t)endpoint);

        CHECK(iso_allowed == (endpoint == FLEX1500_EP_SAMPLE_IN));
        CHECK(interrupt_allowed == (endpoint == FLEX1500_EP_STATUS_IN));
    }

    flex1500_decode_iq_frame(iq_frame, &sample_i, &sample_q);
    CHECK(sample_i == 0x1234);
    CHECK(sample_q == INT16_MIN);

    CHECK(flex1500_firmware_read_request_allowed(
        FLEX1500_OP_GET_FIRMWARE_REV, 0, 0));
    CHECK(!flex1500_firmware_read_request_allowed(1219, 0, 0));
    CHECK(!flex1500_firmware_read_request_allowed(
        FLEX1500_OP_GET_FIRMWARE_REV, 1, 0));
    CHECK(!flex1500_firmware_read_request_allowed(
        FLEX1500_OP_GET_FIRMWARE_REV, 0, 1));

    CHECK(flex1500_build_firmware_read_request(
        7, FLEX1500_OP_GET_FIRMWARE_REV, 0, 0, request));
    CHECK(memcmp(request, expected_request, sizeof(request)) == 0);
    CHECK(!flex1500_build_firmware_read_request(7, 1219, 0, 0, request));

    CHECK(flex1500_decode_u32_response(response, sizeof(response), 7,
                                        &response_value));
    CHECK(response_value == 0x00050318);
    CHECK(!flex1500_decode_u32_response(response, sizeof(response), 8,
                                         &response_value));

    CHECK(flex1500_build_initialize_request(9, initialize_request));
    CHECK(memcmp(initialize_request, expected_initialize,
                  sizeof(initialize_request)) == 0);

    CHECK(flex1500_rx_frequency_to_tuning_word(10000000, &tuning_word));
    CHECK(tuning_word == UINT32_C(0x0d555555));
    CHECK(!flex1500_rx_frequency_to_tuning_word(99999, &tuning_word));
    CHECK(!flex1500_rx_frequency_to_tuning_word(54000001, &tuning_word));
    CHECK(flex1500_build_rx_tune_request(
        10, 10000000, tune_request, &tuning_word));
    CHECK(memcmp(tune_request, expected_tune, sizeof(tune_request)) == 0);
    CHECK(flex1500_build_rx_filter_request(12, 5, filter_request));
    CHECK(memcmp(filter_request, expected_filter_5,
                  sizeof(filter_request)) == 0);
    CHECK(!flex1500_build_rx_filter_request(12, 12, filter_request));
    CHECK(flex1500_rx_filter_for_frequency(100000, &selected_filter));
    CHECK(selected_filter == 0);
    CHECK(flex1500_rx_filter_for_frequency(479999, &selected_filter));
    CHECK(selected_filter == 0);
    CHECK(flex1500_rx_filter_for_frequency(480000, &selected_filter));
    CHECK(selected_filter == 11);
    CHECK(flex1500_rx_filter_for_frequency(10000000, &selected_filter));
    CHECK(selected_filter == 5);
    CHECK(flex1500_rx_filter_for_frequency(28475000, &selected_filter));
    CHECK(selected_filter == 2);
    CHECK(flex1500_rx_filter_for_frequency(54000000, &selected_filter));
    CHECK(selected_filter == 1);
    CHECK(!flex1500_rx_filter_for_frequency(99999, &selected_filter));
    CHECK(!flex1500_rx_filter_for_frequency(54000001, &selected_filter));
    CHECK(flex1500_build_pa_filter_request(14, 5, pa_filter_request));
    CHECK(memcmp(pa_filter_request, expected_pa_filter_5,
                  sizeof(pa_filter_request)) == 0);
    CHECK(!flex1500_build_pa_filter_request(14, 8, pa_filter_request));
    CHECK(flex1500_build_tr_request(6, true, tx_request));
    CHECK(memcmp(tx_request, expected_tr_on, sizeof(tx_request)) == 0);
    CHECK(flex1500_build_transition_mute_request(4, true, tx_request));
    CHECK(memcmp(tx_request, expected_mute, sizeof(tx_request)) == 0);
    CHECK(flex1500_build_tuning_word_request(
        5, UINT32_C(0x25f77777), tx_request));
    CHECK(tx_request[0] == 5 && tx_request[4] == 0x00 &&
           tx_request[5] == 0x00 && tx_request[6] == 0x05 &&
           tx_request[7] == 0x43 && tx_request[8] == 0x25 &&
           tx_request[9] == 0xf7 && tx_request[10] == 0x77 &&
           tx_request[11] == 0x77);
    CHECK(flex1500_build_amp_tx1_request(2, true, tx_request));
    CHECK(tx_request[6] == 0x05 && tx_request[7] == 0x12 &&
           tx_request[11] == 0x01);

    return 0;
}
