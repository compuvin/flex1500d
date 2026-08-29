// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_PROTOCOL_H
#define FLEX1500_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLEX1500_USB_VENDOR_ID = 0x2192,
    FLEX1500_USB_PRODUCT_ID = 0x1502,
    FLEX1500_STREAMING_INTERFACE = 3,
    FLEX1500_EP_SAMPLE_OUT = 0x01,
    FLEX1500_EP_SAMPLE_IN = 0x82,
    FLEX1500_EP_STATUS_IN = 0x83,
    FLEX1500_EP_COMMAND_OUT = 0x04,
    FLEX1500_SAMPLE_PACKET_SIZE = 192,
    FLEX1500_STATUS_PACKET_SIZE = 42,
    FLEX1500_COMMAND_PACKET_SIZE = 20,
    FLEX1500_OP_I2C_WRITE_2_VALUE = 1020,
    FLEX1500_OP_GET_FIRMWARE_REV = 1200,
    FLEX1500_OP_INITIALIZE = 1219,
    FLEX1500_OP_SET_RX1_FILTER = 1257,
    FLEX1500_OP_SET_PA_FILTER = 1260,
    FLEX1500_OP_SET_TR = 1276,
    FLEX1500_OP_SET_AMP_TX1 = 1298,
    FLEX1500_OP_SET_RX1_FREQ_TW = 1347,
};

#define FLEX1500_MIN_RX_FREQUENCY_HZ UINT32_C(100000)
#define FLEX1500_MAX_RX_FREQUENCY_HZ UINT32_C(54000000)

typedef enum flex1500_transfer_kind {
    FLEX1500_TRANSFER_ISOCHRONOUS,
    FLEX1500_TRANSFER_INTERRUPT,
    FLEX1500_TRANSFER_CONTROL,
} flex1500_transfer_kind;

/*
 * Safety policy for the first hardware-probing phase.
 * Only transfers that receive from known IN endpoints are permitted.
 */
bool flex1500_rx_probe_transfer_allowed(flex1500_transfer_kind kind,
                                        uint8_t endpoint);

const char *flex1500_endpoint_name(uint8_t endpoint);

/* Decode one little-endian, signed-16-bit interleaved I/Q sample frame. */
void flex1500_decode_iq_frame(const uint8_t frame[4], int16_t *sample_i,
                              int16_t *sample_q);

/* The first command probe permits this one semantically read-only request. */
bool flex1500_firmware_read_request_allowed(uint32_t opcode, uint32_t param1,
                                            uint32_t param2);

bool flex1500_build_firmware_read_request(
    uint8_t index, uint32_t opcode, uint32_t param1, uint32_t param2,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);

bool flex1500_decode_u32_response(
    const uint8_t *packet, size_t packet_length, uint8_t expected_index,
    uint32_t *result);

/* Build the one exact state-changing command allowed by the minimum RX test. */
bool flex1500_build_initialize_request(
    uint8_t index, uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);

/* Normal 384 MHz reference-clock tuning used by a non-XREF FLEX-1500. */
bool flex1500_rx_frequency_to_tuning_word(uint32_t frequency_hz,
                                         uint32_t *tuning_word);
bool flex1500_build_rx_tune_request(
    uint8_t index, uint32_t frequency_hz,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE], uint32_t *tuning_word);

/* RX filter indices 0..11 observed in the PowerSDR filter map. */
bool flex1500_build_rx_filter_request(
    uint8_t index, uint32_t filter,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);
bool flex1500_rx_filter_for_frequency(uint32_t frequency_hz,
                                      uint32_t *filter);

/* PA filter indices 0..7 observed in the PowerSDR filter map. */
bool flex1500_build_pa_filter_request(
    uint8_t index, uint32_t filter,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);

/* Fixed command builders used only by explicitly armed TX experiments. */
bool flex1500_build_tuning_word_request(
    uint8_t index, uint32_t tuning_word,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);
bool flex1500_build_tr_request(
    uint8_t index, bool transmit,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);
bool flex1500_build_amp_tx1_request(
    uint8_t index, bool enabled,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);
bool flex1500_build_transition_mute_request(
    uint8_t index, bool muted,
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE]);

#endif
