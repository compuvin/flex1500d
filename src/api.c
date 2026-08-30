// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/api.h"

#include "flex1500/protocol.h"

#include <stdio.h>
#include <string.h>

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
    char radio_json[512];
    flex1500_radio_info presented_radio = *radio;
    presented_radio.rx_tuning_enabled = controller->rx_tuning_enabled;
    presented_radio.rx_mode = controller->rx_mode;
    presented_radio.rx_bandwidth_hz = flex1500_api_rx_bandwidth(controller);
    presented_radio.rx_squelch_db = controller->rx_squelch_db;
    if (flex1500_build_status_json(status, status_json, sizeof(status_json)) == 0 ||
        flex1500_build_radio_json(&presented_radio, radio_json,
                                  sizeof(radio_json)) == 0) {
        return FLEX1500_API_RESPONSE;
    }
    *response_length = flex1500_build_http_response(
        request, status_json, radio_json, response, capacity);
    return FLEX1500_API_RESPONSE;
}
