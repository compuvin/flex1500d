// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_NETWORK_H
#define FLEX1500_NETWORK_H

#include "flex1500/iq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLEX1500_IQ_FRAME_HEADER_SIZE = 20,
    FLEX1500_IQ_FRAME_VERSION = 1,
    FLEX1500_IQ_FORMAT_COMPLEX_F32LE = 1,
};

typedef struct flex1500_service_status {
    const char *state;
    bool radio_open;
    bool network_listening;
    uint32_t sample_rate;
    uint64_t frames;
    uint64_t sentinel_frames;
    uint64_t usb_packets;
    uint64_t usb_packet_errors;
    uint64_t ring_dropped_frames;
    uint64_t network_frames_sent;
    uint64_t network_samples_sent;
    uint64_t network_would_block_events;
    uint64_t network_disconnects;
    uint64_t network_write_errors;
    uint64_t rx_tune_operations;
    uint64_t radio_command_errors;
    uint64_t usb_transfer_status_errors;
    uint64_t usb_transfer_timeouts;
    uint64_t usb_transfer_stalls;
    uint64_t usb_transfer_no_device;
    uint64_t usb_transfer_overflows;
    uint64_t usb_transfer_other_errors;
    uint64_t usb_packet_status_errors;
    uint64_t usb_packet_timeouts;
    uint64_t usb_packet_stalls;
    uint64_t usb_packet_no_device;
    uint64_t usb_packet_overflows;
    uint64_t usb_packet_other_errors;
    uint64_t usb_short_packets;
    uint64_t usb_zero_length_packets;
    uint64_t usb_oversized_packets;
    uint64_t usb_missing_bytes;
    uint64_t usb_trailing_bytes;
    uint64_t usb_error_events;
    uint64_t usb_first_error_ms;
    uint64_t usb_last_error_ms;
    uint64_t first_sentinel_frame;
    uint64_t last_sentinel_frame;
    uint64_t first_sentinel_ms;
    uint64_t last_sentinel_ms;
    uint64_t rx_recovery_attempts;
    uint64_t rx_recovery_successes;
} flex1500_service_status;

typedef struct flex1500_radio_info {
    const char *model;
    const char *firmware;
    uint16_t usb_vendor_id;
    uint16_t usb_product_id;
    bool receive_only;
    bool transmit_enabled;
    bool rx_tuning_enabled;
    bool frequency_known;
    uint32_t frequency_hz;
    bool rx_filter_known;
    uint32_t rx_filter;
    bool rx_gain_known;
    int32_t rx_gain_db;
    const char *rx_mode;
    uint32_t rx_bandwidth_hz;
    int32_t rx_squelch_db;
} flex1500_radio_info;

size_t flex1500_build_status_json(const flex1500_service_status *status,
                                  char *output, size_t capacity);
size_t flex1500_build_radio_json(const flex1500_radio_info *radio,
                                 char *output, size_t capacity);

size_t flex1500_encode_iq_frame(uint32_t sequence,
                                const flex1500_iq_sample *samples,
                                uint32_t sample_count, uint8_t *output,
                                size_t capacity);

/* Build a complete HTTP/1.1 response for one already-received request line. */
size_t flex1500_build_http_response(const char *request,
                                    const char *status_json,
                                    const char *radio_json, char *output,
                                    size_t capacity);

/* True only after a complete HTTP header terminator has arrived. */
bool flex1500_http_request_complete(const char *request, size_t length);

/* Parse exactly: PUT /v1/radio/frequency/FREQUENCY_HZ HTTP/... */
bool flex1500_parse_rx_frequency_request(const char *request,
                                         uint32_t *frequency_hz);

/* Parse exactly: PUT /v1/radio/mode/am|fm|usb|lsb|cw HTTP/... */
bool flex1500_parse_rx_mode_request(const char *request,
                                    const char **mode);
bool flex1500_parse_rx_gain_request(const char *request, int32_t *gain_db);
bool flex1500_parse_rx_bandwidth_request(const char *request,
                                         uint32_t *bandwidth_hz);
bool flex1500_parse_rx_squelch_request(const char *request,
                                       int32_t *squelch_db);

#endif
