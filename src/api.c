// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/api.h"

#include "flex1500/protocol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t mode_bandwidth(const char *mode)
{
    if (strcmp(mode, "fm") == 0) return 12000;
    if (strcmp(mode, "usb") == 0 || strcmp(mode, "lsb") == 0) return 2700;
    if (strcmp(mode, "cw") == 0) return 500;
    return 6000;
}

static size_t json_response(const char *status_line, const char *body,
                            char *response, size_t capacity)
{
    int written = snprintf(
        response, capacity,
        "HTTP/1.1 %s\r\nContent-Type: application/json\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n"
        "Cache-Control: no-store\r\n\r\n%s",
        status_line, strlen(body), body);
    if (written <= 0 || (size_t)written >= capacity) return 0;
    return (size_t)written;
}

static bool parse_lease_path(const char *request, const char *prefix,
                             uint64_t *lease)
{
    size_t prefix_length = strlen(prefix);
    if (strncmp(request, prefix, prefix_length) != 0) return false;
    const char *start = request + prefix_length;
    char *end = NULL;
    unsigned long long value = strtoull(start, &end, 10);
    if (end == start || value == 0 || strncmp(end, " HTTP/", 6) != 0) {
        return false;
    }
    *lease = (uint64_t)value;
    return true;
}

static bool parse_tx_timeout_request(const char *request, uint32_t *seconds)
{
    static const char prefix[] = "PUT /v1/radio/tx-timeout/";
    if (strncmp(request, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *start = request + sizeof(prefix) - 1;
    char *end = NULL;
    unsigned long value = strtoul(start, &end, 10);
    if (end == start || value > UINT32_MAX ||
        strncmp(end, " HTTP/", 6) != 0) return false;
    *seconds = (uint32_t)value;
    return true;
}

static bool parse_uint_setting(const char *request, const char *prefix,
                               uint32_t *value)
{
    size_t prefix_length = strlen(prefix);
    if (strncmp(request, prefix, prefix_length) != 0) return false;
    const char *start = request + prefix_length;
    char *end = NULL;
    unsigned long parsed = strtoul(start, &end, 10);
    if (end == start || parsed > UINT32_MAX ||
        strncmp(end, " HTTP/", 6) != 0) return false;
    *value = (uint32_t)parsed;
    return true;
}

static bool transmitter_active(const flex1500_api_controller *controller)
{
    return controller->tune_control != NULL &&
        controller->tune_control->tx_control != NULL &&
        controller->tune_control->tx_control->owner != FLEX1500_TX_OWNER_NONE;
}

static bool request_is(const char *request, const char *method,
                       const char *path)
{
    char prefix[160];
    int n = snprintf(prefix, sizeof(prefix), "%s %s HTTP/", method, path);
    return n > 0 && (size_t)n < sizeof(prefix) &&
           strncmp(request, prefix, (size_t)n) == 0;
}

static const char *request_header(const char *request, const char *name)
{
    const size_t name_length = strlen(name);
    const char *line = request;
    while (line != NULL && *line != '\0') {
        const char *next = strstr(line, "\r\n");
        if (next == NULL || next == line) break;
        if (strncasecmp(line, name, name_length) == 0 &&
            line[name_length] == ':') {
            const char *value = line + name_length + 1;
            while (*value == ' ' || *value == '\t') ++value;
            return value;
        }
        line = next + 2;
    }
    return NULL;
}

static bool request_lease(const char *request, uint64_t *lease)
{
    const char *value = request_header(request, "X-Flex1500-TX-Lease");
    if (value == NULL) return false;
    char *end = NULL;
    unsigned long long parsed = strtoull(value, &end, 10);
    if (end == value || parsed == 0 || (*end != '\r' && *end != '\n')) {
        return false;
    }
    *lease = (uint64_t)parsed;
    return true;
}

static bool station_request_authorized(
    const flex1500_api_controller *controller, const char *request)
{
    if (controller->station_owner == NULL) return true;
    if (!controller->station_owner->held) return false;
    const char *value = request_header(request, "X-Flex1500-Control-Lease");
    if (value == NULL) return false;
    char *end = NULL;
    const unsigned long long lease = strtoull(value, &end, 10);
    return end != value && flex1500_station_owner_matches(
        controller->station_owner, (uint64_t)lease);
}

static bool parse_tx_profile(const char *request,
                             flex1500_network_tx_profile *profile)
{
    const char *body = strstr(request, "\r\n\r\n");
    if (body == NULL) return false;
    body += 4;
    const char *mode = strstr(body, "\"mode\":");
    const char *source = strstr(body, "\"source\":");
    const char *drive = strstr(body, "\"drive_percent\":");
    const char *rate = strstr(body, "\"sample_rate\":");
    const char *format = strstr(body, "\"sample_format\":");
    const char *channels = strstr(body, "\"channels\":");
    if (mode == NULL || source == NULL || drive == NULL || rate == NULL ||
        format == NULL || channels == NULL) {
        return false;
    }
    char mode_text[8] = {0}, source_text[8] = {0}, format_text[8] = {0};
    unsigned int drive_value = 0, rate_value = 0, channel_value = 0;
    if (sscanf(mode, "\"mode\": \"%7[a-z]\"", mode_text) != 1 &&
        sscanf(mode, "\"mode\":\"%7[a-z]\"", mode_text) != 1) return false;
    if (sscanf(source, "\"source\": \"%7[a-z]\"", source_text) != 1 &&
        sscanf(source, "\"source\":\"%7[a-z]\"", source_text) != 1) return false;
    if (sscanf(drive, "\"drive_percent\": %u", &drive_value) != 1 &&
        sscanf(drive, "\"drive_percent\":%u", &drive_value) != 1) return false;
    if (sscanf(rate, "\"sample_rate\": %u", &rate_value) != 1 &&
        sscanf(rate, "\"sample_rate\":%u", &rate_value) != 1) return false;
    if (sscanf(format, "\"sample_format\": \"%7[a-z0-9]\"", format_text) != 1 &&
        sscanf(format, "\"sample_format\":\"%7[a-z0-9]\"", format_text) != 1) return false;
    if (sscanf(channels, "\"channels\": %u", &channel_value) != 1 &&
        sscanf(channels, "\"channels\":%u", &channel_value) != 1) return false;
    profile->mode = strcmp(mode_text, "usb") == 0 ? FLEX1500_NETWORK_TX_USB :
        strcmp(mode_text, "lsb") == 0 ? FLEX1500_NETWORK_TX_LSB :
        FLEX1500_NETWORK_TX_IQ_MODE;
    profile->source = strcmp(source_text, "audio") == 0
        ? FLEX1500_NETWORK_TX_AUDIO : FLEX1500_NETWORK_TX_IQ;
    profile->drive_percent = drive_value;
    profile->sample_rate = rate_value;
    return (strcmp(mode_text, "usb") == 0 || strcmp(mode_text, "lsb") == 0 ||
            strcmp(mode_text, "iq") == 0) &&
           (strcmp(source_text, "audio") == 0 || strcmp(source_text, "iq") == 0) &&
           ((strcmp(source_text, "audio") == 0 && strcmp(format_text, "s16le") == 0 && channel_value == 1) ||
            (strcmp(source_text, "iq") == 0 && strcmp(format_text, "cs16le") == 0 && channel_value == 1));
}

static size_t tune_error_response(flex1500_tune_result result,
                                  char *response, size_t capacity)
{
    const char *status = result == FLEX1500_TUNE_BUSY ? "409 Conflict" :
        result == FLEX1500_TUNE_INVALID_LEASE ? "403 Forbidden" :
        result == FLEX1500_TUNE_DISABLED ? "404 Not Found" :
        "500 Internal Server Error";
    char body[128];
    snprintf(body, sizeof(body), "{\"error\":\"tune_%s\"}\n",
             flex1500_tune_result_name(result));
    return json_response(status, body, response, capacity);
}

void flex1500_api_controller_init(flex1500_api_controller *controller,
                                  bool rx_tuning_enabled,
                                  bool iq_stream_active,
                                  bool test_page_enabled)
{
    *controller = (flex1500_api_controller){
        .rx_tuning_enabled = rx_tuning_enabled,
        .iq_stream_active = iq_stream_active,
        .test_page_enabled = test_page_enabled,
        .rx_mode = "am",
        .rx_bandwidth_hz = 6000,
        .rx_squelch_db = -120,
        .tx_drive_percent = FLEX1500_TX_DEFAULT_DRIVE_PERCENT,
        .tx_microphone_gain_db = 10,
        .next_tune_lease = 1,
        .next_tx_lease = 1,
        .next_station_lease = 1,
    };
}

uint32_t flex1500_api_rx_bandwidth(const flex1500_api_controller *controller)
{
    return controller->rx_bandwidth_hz;
}

flex1500_api_action flex1500_api_dispatch(
    flex1500_api_controller *controller, const char *request,
    const flex1500_service_status *status, const flex1500_radio_info *radio,
    char *response, size_t capacity, size_t *response_length,
    uint32_t *tuned_frequency, uint32_t *tuned_filter)
{
    *response_length = 0;
    uint32_t frequency = 0;
    const bool frequency_request = flex1500_parse_rx_frequency_request(
        request, &frequency);
    const bool frequency_path = strncmp(
        request, "PUT /v1/radio/frequency/", 24) == 0;
    const char *mode = NULL;
    const bool mode_request = flex1500_parse_rx_mode_request(request, &mode);
    const bool mode_path = strncmp(request, "PUT /v1/radio/mode/", 19) == 0;
    int32_t gain_db = 0;
    const bool gain_request = flex1500_parse_rx_gain_request(request, &gain_db);
    const bool gain_path = strncmp(request, "PUT /v1/radio/gain/", 19) == 0;
    uint32_t bandwidth_hz = 0;
    const bool bandwidth_request = flex1500_parse_rx_bandwidth_request(request, &bandwidth_hz);
    const bool bandwidth_path = strncmp(request, "PUT /v1/radio/bandwidth/", 24) == 0;
    int32_t squelch_db = 0;
    const bool squelch_request = flex1500_parse_rx_squelch_request(request, &squelch_db);
    const bool squelch_path = strncmp(request, "PUT /v1/radio/squelch/", 22) == 0;
    static const char tune_start_prefix[] =
        "PUT /v1/radio/tune/start HTTP/";
    static const char tune_path_prefix[] = "PUT /v1/radio/tune/";
    const bool tune_start = strncmp(
        request, tune_start_prefix, sizeof(tune_start_prefix) - 1) == 0;
    const bool tune_path = strncmp(
        request, tune_path_prefix, sizeof(tune_path_prefix) - 1) == 0;
    uint64_t tune_lease = 0;
    const bool tune_keepalive = parse_lease_path(
        request, "PUT /v1/radio/tune/keepalive/", &tune_lease);
    uint64_t stop_lease = 0;
    const bool tune_stop = parse_lease_path(
        request, "PUT /v1/radio/tune/stop/", &stop_lease);
    uint32_t tx_timeout_seconds = 0;
    const bool tx_timeout_request = parse_tx_timeout_request(
        request, &tx_timeout_seconds);
    const bool tx_timeout_path = strncmp(
        request, "PUT /v1/radio/tx-timeout/", 25) == 0;
    uint32_t tx_drive_percent = 0;
    static const char tx_drive_prefix[] = "PUT /v1/radio/tx-drive/";
    const bool tx_drive_request = parse_uint_setting(
        request, tx_drive_prefix, &tx_drive_percent);
    const bool tx_drive_path = strncmp(
        request, tx_drive_prefix, sizeof(tx_drive_prefix) - 1) == 0;
    uint32_t tx_microphone_gain_db = 0;
    static const char tx_mic_gain_prefix[] = "PUT /v1/radio/mic-gain/";
    const bool tx_mic_gain_request = parse_uint_setting(
        request, tx_mic_gain_prefix, &tx_microphone_gain_db);
    const bool tx_mic_gain_path = strncmp(
        request, tx_mic_gain_prefix, sizeof(tx_mic_gain_prefix) - 1) == 0;
    static const char tx_compressor_prefix[] =
        "PUT /v1/radio/tx-compressor/";
    static const char tx_compressor_on_prefix[] =
        "PUT /v1/radio/tx-compressor/on HTTP/";
    static const char tx_compressor_off_prefix[] =
        "PUT /v1/radio/tx-compressor/off HTTP/";
    const bool tx_compressor_on = strncmp(
        request, tx_compressor_on_prefix,
        sizeof(tx_compressor_on_prefix) - 1) == 0;
    const bool tx_compressor_off = strncmp(
        request, tx_compressor_off_prefix,
        sizeof(tx_compressor_off_prefix) - 1) == 0;
    const bool tx_compressor_path = strncmp(
        request, tx_compressor_prefix, sizeof(tx_compressor_prefix) - 1) == 0;

    const bool tx_session_create = request_is(request, "POST", "/v1/tx/sessions");
    const bool owner_acquire = request_is(request, "POST", "/v1/control/owner");
    const bool owner_keepalive = request_is(request, "PUT", "/v1/control/owner/keepalive");
    const bool owner_release = request_is(request, "DELETE", "/v1/control/owner");
    if (owner_acquire || owner_keepalive || owner_release) {
        uint64_t lease = 0;
        flex1500_station_owner_result result = FLEX1500_STATION_OWNER_INVALID;
        if (controller->station_owner == NULL) {
            *response_length = json_response("404 Not Found", "{\"error\":\"not found\"}\n", response, capacity);
            return FLEX1500_API_RESPONSE;
        }
        if (owner_acquire) {
            lease = controller->next_station_lease++;
            if (lease == 0) lease = controller->next_station_lease++;
            result = flex1500_station_owner_acquire(controller->station_owner,
                                                     lease, controller->request_now_ms);
        } else {
            const char *value = request_header(request, "X-Flex1500-Control-Lease");
            if (value != NULL) lease = strtoull(value, NULL, 10);
            if (owner_release && transmitter_active(controller)) {
                *response_length = json_response(
                    "409 Conflict", "{\"error\":\"transmitter_active\"}\n",
                    response, capacity);
                return FLEX1500_API_RESPONSE;
            }
            result = owner_keepalive
                ? flex1500_station_owner_keepalive(controller->station_owner,
                      lease, controller->request_now_ms)
                : flex1500_station_owner_release(controller->station_owner, lease);
        }
        char body[128];
        if (result == FLEX1500_STATION_OWNER_OK) {
            snprintf(body, sizeof(body), "{\"owner\":%s,\"lease\":%llu}\n",
                     owner_release ? "false" : "true", (unsigned long long)lease);
            *response_length = json_response(owner_acquire ? "201 Created" : "200 OK", body, response, capacity);
        } else {
            snprintf(body, sizeof(body), "{\"error\":\"owner_%s\"}\n",
                     result == FLEX1500_STATION_OWNER_BUSY ? "busy" : "stale");
            *response_length = json_response(result == FLEX1500_STATION_OWNER_BUSY ? "409 Conflict" : "410 Gone", body, response, capacity);
        }
        return FLEX1500_API_RESPONSE;
    }
    const bool tx_keepalive = request_is(request, "PUT", "/v1/tx/sessions/keepalive");
    const bool tx_stream = request_is(request, "CONNECT", "/v1/tx/stream");
    const bool tx_audio = request_is(request, "POST", "/v1/tx/audio");
    const bool tx_ptt_start = request_is(request, "PUT", "/v1/tx/ptt/start");
    const bool tx_ptt_stop = request_is(request, "PUT", "/v1/tx/ptt/stop");
    const bool tx_release = request_is(request, "DELETE", "/v1/tx/sessions/current");
    const bool tx_general_path = strncmp(request, "POST /v1/tx/", 12) == 0 ||
        strncmp(request, "PUT /v1/tx/", 11) == 0 ||
        strncmp(request, "CONNECT /v1/tx/", 15) == 0 ||
        strncmp(request, "DELETE /v1/tx/", 14) == 0;

    const bool station_controlled = tx_general_path || tune_path ||
        frequency_path || mode_path || gain_path || bandwidth_path ||
        tx_drive_path || tx_mic_gain_path || tx_timeout_path ||
        tx_compressor_path;
    if (station_controlled && !station_request_authorized(controller, request)) {
        *response_length = json_response(
            "409 Conflict", "{\"error\":\"station_owned\"}\n",
            response, capacity);
        return FLEX1500_API_RESPONSE;
    }

    if (tx_general_path) {
        if (controller->network_tx == NULL || !controller->network_tx->enabled) {
            *response_length = json_response("404 Not Found", "{\"error\":\"not found\"}\n", response, capacity);
            return FLEX1500_API_RESPONSE;
        }
        flex1500_network_tx_result result = FLEX1500_NETWORK_TX_INVALID;
        uint64_t lease = 0;
        if (tx_session_create) {
            flex1500_network_tx_profile profile;
            const char *content_type = request_header(request, "Content-Type");
            if (content_type != NULL &&
                strncasecmp(content_type, "application/json", 16) == 0 &&
                parse_tx_profile(request, &profile)) {
                lease = controller->next_tx_lease++;
                if (lease == 0) lease = controller->next_tx_lease++;
                result = flex1500_network_tx_acquire(controller->network_tx,
                    &profile, lease, controller->request_now_ms);
            }
        } else if (request_lease(request, &lease)) {
            if (tx_keepalive) result = flex1500_network_tx_keepalive(controller->network_tx, lease, controller->request_now_ms);
            else if (tx_stream) result = flex1500_network_tx_attach_stream(controller->network_tx, lease, controller->request_now_ms);
            else if (tx_ptt_start) {
                const flex1500_network_tx_profile *profile =
                    &controller->network_tx->profile;
                bool frequency_allowed = radio->frequency_known &&
                    (profile->source == FLEX1500_NETWORK_TX_IQ
                        ? flex1500_network_iq_frequency_allowed(
                              radio->frequency_hz)
                        : flex1500_physical_mic_frequency_allowed(
                              radio->frequency_hz,
                              profile->mode == FLEX1500_NETWORK_TX_USB));
                result = frequency_allowed
                    ? flex1500_network_tx_ptt_start(controller->network_tx,
                          lease, controller->request_now_ms)
                    : FLEX1500_NETWORK_TX_INVALID;
            }
            else if (tx_ptt_stop) result = flex1500_network_tx_ptt_stop(controller->network_tx, lease);
            else if (tx_release) result = flex1500_network_tx_release(controller->network_tx, lease);
            else if (tx_audio) {
                const char *body = strstr(request, "\r\n\r\n");
                const char *length = request_header(request, "Content-Length");
                unsigned int bytes = 0;
                bool audio_profile =
                    controller->network_tx->profile.source ==
                        FLEX1500_NETWORK_TX_AUDIO;
                if (body != NULL && length != NULL &&
                    sscanf(length, "%u", &bytes) == 1 &&
                    bytes > 0 && bytes <= 9600 && (bytes % 2) == 0 &&
                    audio_profile) {
                    if (!controller->network_tx->stream_connected) {
                        result = flex1500_network_tx_attach_stream(
                            controller->network_tx, lease,
                            controller->request_now_ms);
                    } else {
                        result = FLEX1500_NETWORK_TX_OK;
                    }
                    if (result == FLEX1500_NETWORK_TX_OK) {
                        result = flex1500_network_tx_record_data(
                            controller->network_tx, lease, bytes / 2,
                            controller->request_now_ms);
                    }
                }
            }
        }
        if (result == FLEX1500_NETWORK_TX_OK && tx_stream) return FLEX1500_API_OPEN_TX_STREAM;
        if (result == FLEX1500_NETWORK_TX_OK && tx_audio) {
            *response_length = json_response(
                "200 OK", "{\"accepted\":true}\n", response, capacity);
            return FLEX1500_API_PUSH_TX_AUDIO;
        }
        if (result == FLEX1500_NETWORK_TX_BUSY ||
            result == FLEX1500_NETWORK_TX_STALE) {
            ++controller->tune_control->diagnostics.rejected_ownership_requests;
        }
        char body[256];
        if (result == FLEX1500_NETWORK_TX_OK && tx_session_create) {
            snprintf(body, sizeof(body), "{\"lease\":%llu,\"state\":\"reserved\",\"lease_timeout_ms\":%u}\n",
                     (unsigned long long)lease, FLEX1500_NETWORK_TX_LEASE_MS);
            *response_length = json_response("201 Created", body, response, capacity);
        } else if (result == FLEX1500_NETWORK_TX_OK) {
            snprintf(body, sizeof(body), "{\"state\":\"%s\"}\n",
                     controller->network_tx->keyed ? "transmitting" : "reserved");
            *response_length = json_response("200 OK", body, response, capacity);
        } else {
            const char *status_line = result == FLEX1500_NETWORK_TX_DISABLED ? "404 Not Found" :
                result == FLEX1500_NETWORK_TX_BUSY || result == FLEX1500_NETWORK_TX_NOT_READY ? "409 Conflict" :
                result == FLEX1500_NETWORK_TX_STALE ? "410 Gone" :
                result == FLEX1500_NETWORK_TX_HARDWARE_ERROR ? "500 Internal Server Error" :
                result == FLEX1500_NETWORK_TX_INVALID ? "422 Unprocessable Content" : "400 Bad Request";
            snprintf(body, sizeof(body), "{\"error\":\"tx_%s\"}\n", flex1500_network_tx_result_name(result));
            *response_length = json_response(status_line, body, response, capacity);
        }
        return FLEX1500_API_RESPONSE;
    }

    if (tx_compressor_path) {
        if (!tx_compressor_on && !tx_compressor_off) {
            *response_length = json_response(
                "400 Bad Request", "{\"error\":\"invalid_tx_compressor\"}\n",
                response, capacity);
        } else if (controller->tune_control == NULL ||
                   controller->tune_control->tx_control == NULL ||
                   !controller->tune_control->tx_control->enabled) {
            *response_length = json_response(
                "404 Not Found", "{\"error\":\"not found\"}\n",
                response, capacity);
        } else if (transmitter_active(controller)) {
            *response_length = json_response(
                "409 Conflict", "{\"error\":\"transmitter_active\"}\n",
                response, capacity);
        } else {
            controller->tx_compressor_enabled = tx_compressor_on;
            *response_length = json_response(
                "200 OK", tx_compressor_on
                    ? "{\"tx_compressor_enabled\":true}\n"
                    : "{\"tx_compressor_enabled\":false}\n",
                response, capacity);
        }
        return FLEX1500_API_RESPONSE;
    }

    if (tx_drive_path || tx_mic_gain_path) {
        const bool drive = tx_drive_path;
        uint32_t value = drive ? tx_drive_percent : tx_microphone_gain_db;
        bool parsed = drive ? tx_drive_request : tx_mic_gain_request;
        uint32_t maximum = drive ? FLEX1500_TX_MAX_DRIVE_PERCENT : 70;
        if (!parsed ||
            (drive && value < FLEX1500_TX_MIN_DRIVE_PERCENT) ||
            value > maximum) {
            *response_length = json_response(
                "400 Bad Request", "{\"error\":\"invalid_tx_setting\"}\n",
                response, capacity);
        } else if (controller->tune_control == NULL ||
                   controller->tune_control->tx_control == NULL ||
                   !controller->tune_control->tx_control->enabled) {
            *response_length = json_response(
                "404 Not Found", "{\"error\":\"not found\"}\n",
                response, capacity);
        } else if (transmitter_active(controller)) {
            *response_length = json_response(
                "409 Conflict", "{\"error\":\"transmitter_active\"}\n",
                response, capacity);
        } else {
            char body[96];
            if (drive) {
                controller->tx_drive_percent = value;
                snprintf(body, sizeof(body),
                         "{\"tx_drive_percent\":%u}\n", value);
            } else {
                controller->tx_microphone_gain_db = value;
                snprintf(body, sizeof(body),
                         "{\"tx_microphone_gain_db\":%u}\n", value);
            }
            *response_length = json_response("200 OK", body, response,
                                             capacity);
        }
        return FLEX1500_API_RESPONSE;
    }

    if (tx_timeout_path) {
        flex1500_tx_control *tx = controller->tune_control != NULL
            ? controller->tune_control->tx_control : NULL;
        if (!tx_timeout_request ||
            tx_timeout_seconds < FLEX1500_TX_TIMEOUT_MIN_SECONDS ||
            tx_timeout_seconds > FLEX1500_TX_TIMEOUT_MAX_SECONDS) {
            *response_length = json_response(
                "400 Bad Request", "{\"error\":\"invalid_tx_timeout\"}\n",
                response, capacity);
        } else if (tx == NULL || !tx->enabled) {
            *response_length = json_response(
                "404 Not Found", "{\"error\":\"not found\"}\n",
                response, capacity);
        } else if (tx->owner != FLEX1500_TX_OWNER_NONE) {
            *response_length = json_response(
                "409 Conflict", "{\"error\":\"transmitter_active\"}\n",
                response, capacity);
        } else if (!flex1500_tx_control_set_timeout_seconds(
                       tx, tx_timeout_seconds)) {
            *response_length = json_response(
                "500 Internal Server Error",
                "{\"error\":\"tx_timeout_change_failed\"}\n",
                response, capacity);
        } else {
            char body[96];
            snprintf(body, sizeof(body), "{\"tx_timeout_seconds\":%u}\n",
                     tx_timeout_seconds);
            *response_length = json_response("200 OK", body, response,
                                             capacity);
        }
        return FLEX1500_API_RESPONSE;
    }

    if (tune_path) {
        if (controller->tune_control == NULL) {
            *response_length = json_response(
                "404 Not Found", "{\"error\":\"not found\"}\n",
                response, capacity);
            return FLEX1500_API_RESPONSE;
        }
        flex1500_tune_result result = FLEX1500_TUNE_INVALID_LEASE;
        if (tune_start) {
            if (!radio->frequency_known ||
                !flex1500_tune_frequency_allowed(radio->frequency_hz)) {
                ++controller->tune_control->diagnostics
                      .rejected_ownership_requests;
                *response_length = json_response(
                    "422 Unprocessable Content",
                    "{\"error\":\"tx_frequency_not_allowed\"}\n",
                    response, capacity);
                return FLEX1500_API_RESPONSE;
            }
            uint64_t lease = controller->next_tune_lease++;
            if (lease == 0) lease = controller->next_tune_lease++;
            result = flex1500_tune_control_start(
                controller->tune_control, controller->request_now_ms, lease);
            if (result == FLEX1500_TUNE_OK) {
                char body[192];
                snprintf(body, sizeof(body),
                    "{\"tune_active\":true,\"lease\":%llu,"
                    "\"lease_timeout_ms\":%u,\"hard_limit_ms\":%u}\n",
                    (unsigned long long)lease, FLEX1500_TUNE_LEASE_MS,
                    FLEX1500_TUNE_HARD_LIMIT_MS);
                *response_length = json_response(
                    "200 OK", body, response, capacity);
                return FLEX1500_API_RESPONSE;
            }
        } else if (tune_keepalive) {
            result = flex1500_tune_control_keepalive(
                controller->tune_control, controller->request_now_ms,
                tune_lease);
            if (result == FLEX1500_TUNE_OK) {
                *response_length = json_response(
                    "200 OK", "{\"tune_active\":true}\n", response,
                    capacity);
                return FLEX1500_API_RESPONSE;
            }
        } else if (tune_stop) {
            result = flex1500_tune_control_stop(
                controller->tune_control, stop_lease);
            if (result == FLEX1500_TUNE_OK) {
                *response_length = json_response(
                    "200 OK", "{\"tune_active\":false}\n", response,
                    capacity);
                return FLEX1500_API_RESPONSE;
            }
        }
        *response_length = tune_error_response(result, response, capacity);
        return FLEX1500_API_RESPONSE;
    }

    if ((frequency_request || mode_request || bandwidth_request) &&
        transmitter_active(controller)) {
        *response_length = json_response(
            "409 Conflict", "{\"error\":\"transmitter_active\"}\n",
            response, capacity);
        return FLEX1500_API_RESPONSE;
    }
    if (frequency_request && controller->rx_tuning_enabled &&
        controller->tune_rx != NULL) {
        uint32_t mapped_filter = 0;
        const bool valid = flex1500_rx_filter_for_frequency(
            frequency, &mapped_filter);
        uint32_t filter = 0;
        const int result = valid ? controller->tune_rx(
            controller->radio_context, frequency, &filter) : -1;
        char body[256];
        if (result == 0) {
            snprintf(body, sizeof(body),
                     "{\"frequency_hz\":%u,\"rx_filter\":%u}\n",
                     frequency, filter);
            *response_length = json_response("200 OK", body, response,
                                             capacity);
            if (tuned_frequency != NULL) *tuned_frequency = frequency;
            if (tuned_filter != NULL) *tuned_filter = filter;
            return FLEX1500_API_TUNED_RX;
        }
        snprintf(body, sizeof(body), "{\"error\":\"RX tune failed\"}\n");
        *response_length = json_response(
            valid ? "500 Internal Server Error" : "400 Bad Request", body,
            response, capacity);
        return valid ? FLEX1500_API_RX_TUNE_FAILED : FLEX1500_API_RESPONSE;
    }
    if (mode_request && controller->iq_stream_active) {
        controller->rx_mode = mode;
        controller->rx_bandwidth_hz = mode_bandwidth(mode);
        char body[128];
        snprintf(body, sizeof(body),
                 "{\"rx_mode\":\"%s\",\"rx_bandwidth_hz\":%u}\n",
                 mode, flex1500_api_rx_bandwidth(controller));
        *response_length = json_response("200 OK", body, response, capacity);
        return FLEX1500_API_RESPONSE;
    }
    if (bandwidth_request && controller->iq_stream_active) {
        controller->rx_bandwidth_hz = bandwidth_hz;
        char body[96];
        snprintf(body, sizeof(body), "{\"rx_bandwidth_hz\":%u}\n", bandwidth_hz);
        *response_length = json_response("200 OK", body, response, capacity);
        return FLEX1500_API_RESPONSE;
    }
    if (squelch_request && controller->iq_stream_active) {
        controller->rx_squelch_db = squelch_db;
        char body[96];
        snprintf(body, sizeof(body), "{\"rx_squelch_db\":%d}\n", squelch_db);
        *response_length = json_response("200 OK", body, response, capacity);
        return FLEX1500_API_RESPONSE;
    }
    if (gain_request && controller->rx_tuning_enabled &&
        controller->set_rx_gain != NULL) {
        char body[128];
        if (controller->set_rx_gain(controller->radio_context, gain_db) == 0) {
            snprintf(body, sizeof(body), "{\"rx_gain_db\":%d}\n", gain_db);
            *response_length = json_response("200 OK", body, response, capacity);
        } else {
            *response_length = json_response("500 Internal Server Error",
                "{\"error\":\"RX gain change failed\"}\n", response, capacity);
        }
        return FLEX1500_API_RESPONSE;
    }
    if (frequency_path || mode_path || gain_path || bandwidth_path || squelch_path) {
        *response_length = json_response(
            "404 Not Found", "{\"error\":\"not found\"}\n", response,
            capacity);
        return FLEX1500_API_RESPONSE;
    }
    if (strncmp(request, "GET /v1/stream/iq ", 18) == 0 &&
        controller->iq_stream_active) {
        return FLEX1500_API_OPEN_IQ_STREAM;
    }
    if (strncmp(request, "GET /test HTTP/", 15) == 0 &&
        controller->test_page_enabled) {
        return FLEX1500_API_SERVE_TEST_PAGE;
    }

    char status_json[4096];
    char radio_json[1024];
    flex1500_radio_info presented_radio = *radio;
    presented_radio.rx_tuning_enabled = controller->rx_tuning_enabled;
    presented_radio.rx_mode = controller->rx_mode;
    presented_radio.rx_bandwidth_hz = flex1500_api_rx_bandwidth(controller);
    presented_radio.rx_squelch_db = controller->rx_squelch_db;
    if (controller->tune_control != NULL &&
        controller->tune_control->tx_control != NULL) {
        flex1500_tx_control *tx = controller->tune_control->tx_control;
        presented_radio.tx_timeout_seconds =
            flex1500_tx_control_timeout_seconds(
                tx);
        presented_radio.tx_owner = flex1500_tx_owner_name(tx->owner);
        presented_radio.tx_state = flex1500_tx_state_name(tx->state);
    }
    if (controller->network_tx != NULL) {
        presented_radio.network_tx_reserved = controller->network_tx->reserved;
        presented_radio.network_tx_stream_connected =
            controller->network_tx->stream_connected;
    }
    presented_radio.tx_drive_percent = controller->tx_drive_percent;
    presented_radio.tx_microphone_gain_db =
        controller->tx_microphone_gain_db;
    presented_radio.tx_compressor_enabled =
        controller->tx_compressor_enabled;
    if (flex1500_build_status_json(status, status_json, sizeof(status_json)) == 0 ||
        flex1500_build_radio_json(&presented_radio, radio_json,
                                  sizeof(radio_json)) == 0) {
        return FLEX1500_API_RESPONSE;
    }
    *response_length = flex1500_build_http_response(
        request, status_json, radio_json, response, capacity);
    return FLEX1500_API_RESPONSE;
}
