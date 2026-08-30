// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/api.h"

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

typedef struct fake_radio {
    unsigned calls;
    uint32_t frequency;
    int result;
} fake_radio;

static int tune_rx(void *context, uint32_t frequency, uint32_t *filter)
{
    fake_radio *radio = context;
    ++radio->calls;
    radio->frequency = frequency;
    if (radio->result != 0) return radio->result;
    *filter = 5;
    return 0;
}

static flex1500_api_action dispatch(
    flex1500_api_controller *controller, const char *request,
    char response[8192], size_t *length)
{
    const flex1500_service_status status = {
        .state = "test", .network_listening = true, .sample_rate = 48000,
    };
    const flex1500_radio_info radio = {
        .model = "FLEX-1500", .firmware = "test", .receive_only = true,
        .transmit_enabled = false,
    };
    return flex1500_api_dispatch(controller, request, &status, &radio,
                                 response, 8192, length, NULL, NULL);
}

int main(void)
{
    flex1500_api_controller api;
    fake_radio radio = {0};
    char response[8192];
    size_t length = 0;

    flex1500_api_controller_init(&api, true, true, true);
    api.radio_context = &radio;
    api.tune_rx = tune_rx;

    CHECK(dispatch(&api, "GET /v1/status HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(length > 0 && strstr(response, "200 OK") != NULL);
    CHECK(strstr(response, "\"state\": \"test\"") != NULL);

    CHECK(dispatch(&api, "PUT /v1/radio/mode/usb HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strcmp(api.rx_mode, "usb") == 0);
    CHECK(flex1500_api_rx_bandwidth(&api) == 2700);
    CHECK(strstr(response, "\"rx_mode\":\"usb\"") != NULL);

    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_TUNED_RX);
    CHECK(radio.calls == 1 && radio.frequency == 10000000);
    CHECK(strstr(response, "\"rx_filter\":5") != NULL);

    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/99999 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(radio.calls == 1);
    CHECK(strstr(response, "400 Bad Request") != NULL);

    radio.result = -1;
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RX_TUNE_FAILED);
    CHECK(radio.calls == 2);
    CHECK(strstr(response, "500 Internal Server Error") != NULL);
    radio.result = 0;

    CHECK(dispatch(&api, "GET /v1/stream/iq HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_OPEN_IQ_STREAM);
    CHECK(dispatch(&api, "GET /test HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_SERVE_TEST_PAGE);
    CHECK(dispatch(&api, "POST /v1/radio/ptt HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "404 Not Found") != NULL);

    flex1500_api_controller_init(&api, false, false, false);
    api.radio_context = &radio;
    api.tune_rx = tune_rx;
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(radio.calls == 2);
    CHECK(strstr(response, "404 Not Found") != NULL);
    CHECK(dispatch(&api, "GET /v1/stream/iq HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "503 Service Unavailable") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/mode/usb HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strcmp(api.rx_mode, "am") == 0);
    CHECK(strstr(response, "404 Not Found") != NULL);
    CHECK(dispatch(&api, "GET /test HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "404 Not Found") != NULL);
    return 0;
}
