// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network.h"

#include <stdio.h>
#include <string.h>

static void store_be16(uint8_t output[2], uint16_t value)
{
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void store_be32(uint8_t output[4], uint32_t value)
{
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void store_f32le(uint8_t output[4], float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    output[0] = (uint8_t)bits;
    output[1] = (uint8_t)(bits >> 8);
    output[2] = (uint8_t)(bits >> 16);
    output[3] = (uint8_t)(bits >> 24);
}

size_t flex1500_build_status_json(const flex1500_service_status *status,
                                  char *output, size_t capacity)
{
    int written = snprintf(
        output, capacity,
        "{\n"
        "  \"service\": \"flex1500d\",\n"
        "  \"api_version\": 1,\n"
        "  \"state\": \"%s\",\n"
        "  \"radio_open\": %s,\n"
        "  \"network_listening\": %s,\n"
        "  \"sample_rate\": %u,\n"
        "  \"sample_format\": \"complex_f32le\",\n"
        "  \"frames\": %llu,\n"
        "  \"sentinel_frames\": %llu,\n"
        "  \"usb_packets\": %llu,\n"
        "  \"usb_packet_errors\": %llu,\n"
        "  \"ring_dropped_frames\": %llu,\n"
        "  \"network_frames_sent\": %llu,\n"
        "  \"network_samples_sent\": %llu,\n"
        "  \"network_would_block_events\": %llu,\n"
        "  \"network_disconnects\": %llu,\n"
        "  \"network_write_errors\": %llu,\n"
        "  \"rx_tune_operations\": %llu,\n"
        "  \"radio_command_errors\": %llu,\n"
        "  \"usb_transfer_status_errors\": %llu,\n"
        "  \"usb_transfer_timeouts\": %llu,\n"
        "  \"usb_transfer_stalls\": %llu,\n"
        "  \"usb_transfer_no_device\": %llu,\n"
        "  \"usb_transfer_overflows\": %llu,\n"
        "  \"usb_transfer_other_errors\": %llu,\n"
        "  \"usb_packet_status_errors\": %llu,\n"
        "  \"usb_packet_timeouts\": %llu,\n"
        "  \"usb_packet_stalls\": %llu,\n"
        "  \"usb_packet_no_device\": %llu,\n"
        "  \"usb_packet_overflows\": %llu,\n"
        "  \"usb_packet_other_errors\": %llu,\n"
        "  \"usb_short_packets\": %llu,\n"
        "  \"usb_zero_length_packets\": %llu,\n"
        "  \"usb_oversized_packets\": %llu,\n"
        "  \"usb_missing_bytes\": %llu,\n"
        "  \"usb_trailing_bytes\": %llu,\n"
        "  \"usb_error_events\": %llu,\n"
        "  \"usb_first_error_ms\": %llu,\n"
        "  \"usb_last_error_ms\": %llu,\n"
        "  \"first_sentinel_frame\": %llu,\n"
        "  \"last_sentinel_frame\": %llu,\n"
        "  \"first_sentinel_ms\": %llu,\n"
        "  \"last_sentinel_ms\": %llu,\n"
        "  \"rx_recovery_attempts\": %llu,\n"
        "  \"rx_recovery_successes\": %llu\n"
        "}\n",
        status->state, status->radio_open ? "true" : "false",
        status->network_listening ? "true" : "false", status->sample_rate,
        (unsigned long long)status->frames,
        (unsigned long long)status->sentinel_frames,
        (unsigned long long)status->usb_packets,
        (unsigned long long)status->usb_packet_errors,
        (unsigned long long)status->ring_dropped_frames,
        (unsigned long long)status->network_frames_sent,
        (unsigned long long)status->network_samples_sent,
        (unsigned long long)status->network_would_block_events,
        (unsigned long long)status->network_disconnects,
        (unsigned long long)status->network_write_errors,
        (unsigned long long)status->rx_tune_operations,
        (unsigned long long)status->radio_command_errors,
        (unsigned long long)status->usb_transfer_status_errors,
        (unsigned long long)status->usb_transfer_timeouts,
        (unsigned long long)status->usb_transfer_stalls,
        (unsigned long long)status->usb_transfer_no_device,
        (unsigned long long)status->usb_transfer_overflows,
        (unsigned long long)status->usb_transfer_other_errors,
        (unsigned long long)status->usb_packet_status_errors,
        (unsigned long long)status->usb_packet_timeouts,
        (unsigned long long)status->usb_packet_stalls,
        (unsigned long long)status->usb_packet_no_device,
        (unsigned long long)status->usb_packet_overflows,
        (unsigned long long)status->usb_packet_other_errors,
        (unsigned long long)status->usb_short_packets,
        (unsigned long long)status->usb_zero_length_packets,
        (unsigned long long)status->usb_oversized_packets,
        (unsigned long long)status->usb_missing_bytes,
        (unsigned long long)status->usb_trailing_bytes,
        (unsigned long long)status->usb_error_events,
        (unsigned long long)status->usb_first_error_ms,
        (unsigned long long)status->usb_last_error_ms,
        (unsigned long long)status->first_sentinel_frame,
        (unsigned long long)status->last_sentinel_frame,
        (unsigned long long)status->first_sentinel_ms,
        (unsigned long long)status->last_sentinel_ms,
        (unsigned long long)status->rx_recovery_attempts,
        (unsigned long long)status->rx_recovery_successes);
    if (written < 0 || (size_t)written >= capacity) return 0;
    return (size_t)written;
}

size_t flex1500_build_radio_json(const flex1500_radio_info *radio,
                                 char *output, size_t capacity)
{
    char frequency[32];
    char filter[32];
    char gain[32];
    if (radio->frequency_known) {
        snprintf(frequency, sizeof(frequency), "%u", radio->frequency_hz);
    } else {
        snprintf(frequency, sizeof(frequency), "null");
    }
    if (radio->rx_filter_known) {
        snprintf(filter, sizeof(filter), "%u", radio->rx_filter);
    } else {
        snprintf(filter, sizeof(filter), "null");
    }
    if (radio->rx_gain_known) snprintf(gain, sizeof(gain), "%d", radio->rx_gain_db);
    else snprintf(gain, sizeof(gain), "null");
    int written = snprintf(
        output, capacity,
        "{\n"
        "  \"model\": \"%s\",\n"
        "  \"firmware\": \"%s\",\n"
        "  \"usb_vendor_id\": \"%04x\",\n"
        "  \"usb_product_id\": \"%04x\",\n"
        "  \"receive_only\": %s,\n"
        "  \"transmit_enabled\": %s,\n"
        "  \"rx_tuning_enabled\": %s,\n"
        "  \"frequency_hz\": %s,\n"
        "  \"rx_filter\": %s,\n"
        "  \"rx_gain_db\": %s,\n"
        "  \"rx_mode\": \"%s\",\n"
        "  \"rx_bandwidth_hz\": %u,\n"
        "  \"rx_squelch_db\": %d\n"
        "}\n",
        radio->model, radio->firmware, radio->usb_vendor_id,
        radio->usb_product_id, radio->receive_only ? "true" : "false",
        radio->transmit_enabled ? "true" : "false",
        radio->rx_tuning_enabled ? "true" : "false", frequency, filter, gain,
        radio->rx_mode != NULL ? radio->rx_mode : "am",
        radio->rx_bandwidth_hz, radio->rx_squelch_db);
    if (written < 0 || (size_t)written >= capacity) return 0;
    return (size_t)written;
}

size_t flex1500_encode_iq_frame(uint32_t sequence,
                                const flex1500_iq_sample *samples,
                                uint32_t sample_count, uint8_t *output,
                                size_t capacity)
{
    size_t required = FLEX1500_IQ_FRAME_HEADER_SIZE + (size_t)sample_count * 8;
    if (required > capacity) return 0;

    output[0] = 'F';
    output[1] = '1';
    output[2] = '5';
    output[3] = 'I';
    output[4] = FLEX1500_IQ_FRAME_VERSION;
    output[5] = FLEX1500_IQ_FORMAT_COMPLEX_F32LE;
    store_be16(&output[6], FLEX1500_IQ_FRAME_HEADER_SIZE);
    store_be32(&output[8], sequence);
    store_be32(&output[12], 48000);
    store_be32(&output[16], sample_count);

    for (uint32_t index = 0; index < sample_count; ++index) {
        store_f32le(&output[FLEX1500_IQ_FRAME_HEADER_SIZE + index * 8],
                    samples[index].i);
        store_f32le(&output[FLEX1500_IQ_FRAME_HEADER_SIZE + index * 8 + 4],
                    samples[index].q);
    }
    return required;
}

size_t flex1500_build_http_response(const char *request,
                                    const char *status_json,
                                    const char *radio_json, char *output,
                                    size_t capacity)
{
    const char *status_line;
    const char *content_type;
    const char *body;

    if (strncmp(request, "GET /v1/status ", 15) == 0) {
        status_line = "200 OK";
        content_type = "application/json";
        body = status_json;
    } else if (strncmp(request, "GET /v1/radio ", 14) == 0) {
        status_line = "200 OK";
        content_type = "application/json";
        body = radio_json;
    } else if (strncmp(request, "GET /v1/stream/iq ", 18) == 0) {
        status_line = "503 Service Unavailable";
        content_type = "application/json";
        body = "{\"error\":\"IQ stream is not active\"}\n";
    } else {
        status_line = "404 Not Found";
        content_type = "application/json";
        body = "{\"error\":\"not found\"}\n";
    }

    size_t body_length = strlen(body);
    int written = snprintf(
        output, capacity,
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n"
        "\r\n%s",
        status_line, content_type, body_length, body);
    if (written < 0 || (size_t)written >= capacity) return 0;
    return (size_t)written;
}

bool flex1500_http_request_complete(const char *request, size_t length)
{
    if (request == NULL) return false;
    for (size_t index = 0; index + 1 < length; ++index) {
        if (request[index] == '\n' && request[index + 1] == '\n') return true;
        if (index + 3 < length && request[index] == '\r' &&
            request[index + 1] == '\n' && request[index + 2] == '\r' &&
            request[index + 3] == '\n') return true;
    }
    return false;
}

bool flex1500_parse_rx_frequency_request(const char *request,
                                         uint32_t *frequency_hz)
{
    static const char prefix[] = "PUT /v1/radio/frequency/";
    if (request == NULL || frequency_hz == NULL ||
        strncmp(request, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *cursor = request + sizeof(prefix) - 1;
    if (*cursor < '0' || *cursor > '9') return false;
    uint64_t value = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        value = value * 10 + (uint64_t)(*cursor - '0');
        if (value > UINT32_MAX) return false;
        ++cursor;
    }
    if (*cursor != ' ') return false;
    *frequency_hz = (uint32_t)value;
    return true;
}

bool flex1500_parse_rx_mode_request(const char *request, const char **mode)
{
    static const char prefix[] = "PUT /v1/radio/mode/";
    static const char *modes[] = {"am", "fm", "usb", "lsb", "cw"};
    if (request == NULL || mode == NULL ||
        strncmp(request, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *value = request + sizeof(prefix) - 1;
    for (size_t index = 0; index < sizeof(modes) / sizeof(modes[0]); ++index) {
        size_t length = strlen(modes[index]);
        if (strncmp(value, modes[index], length) == 0 &&
            value[length] == ' ') {
            *mode = modes[index];
            return true;
        }
    }
    return false;
}

bool flex1500_parse_rx_gain_request(const char *request, int32_t *gain_db)
{
    static const char prefix[] = "PUT /v1/radio/gain/";
    static const int32_t gains[] = {-10, 0, 10, 20, 30};
    if (request == NULL || gain_db == NULL ||
        strncmp(request, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *value = request + sizeof(prefix) - 1;
    for (size_t index = 0; index < sizeof(gains) / sizeof(gains[0]); ++index) {
        char expected[8];
        snprintf(expected, sizeof(expected), "%d", gains[index]);
        size_t length = strlen(expected);
        if (strncmp(value, expected, length) == 0 && value[length] == ' ') {
            *gain_db = gains[index];
            return true;
        }
    }
    return false;
}

static bool parse_u32_path(const char *request, const char *prefix,
                           uint32_t *value)
{
    if (request == NULL || value == NULL || strncmp(request, prefix, strlen(prefix)) != 0)
        return false;
    const char *cursor = request + strlen(prefix);
    if (*cursor < '0' || *cursor > '9') return false;
    uint64_t parsed = 0;
    while (*cursor >= '0' && *cursor <= '9') {
        parsed = parsed * 10 + (uint64_t)(*cursor++ - '0');
        if (parsed > UINT32_MAX) return false;
    }
    if (*cursor != ' ') return false;
    *value = (uint32_t)parsed;
    return true;
}

bool flex1500_parse_rx_bandwidth_request(const char *request,
                                         uint32_t *bandwidth_hz)
{
    return parse_u32_path(request, "PUT /v1/radio/bandwidth/", bandwidth_hz) &&
           *bandwidth_hz >= 100 && *bandwidth_hz <= 20000;
}

bool flex1500_parse_rx_squelch_request(const char *request, int32_t *squelch_db)
{
    static const char prefix[] = "PUT /v1/radio/squelch/";
    if (request == NULL || squelch_db == NULL ||
        strncmp(request, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *cursor = request + sizeof(prefix) - 1;
    bool negative = *cursor == '-';
    if (negative) ++cursor;
    if (*cursor < '0' || *cursor > '9') return false;
    int32_t parsed = 0;
    while (*cursor >= '0' && *cursor <= '9') parsed = parsed * 10 + (*cursor++ - '0');
    if (*cursor != ' ') return false;
    if (negative) parsed = -parsed;
    if (parsed < -120 || parsed > 0) return false;
    *squelch_db = parsed;
    return true;
}
