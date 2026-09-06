// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/api.h"

#include "test_assert.h"

#include <stdint.h>
#include <string.h>

typedef struct fake_radio {
    unsigned calls;
    uint32_t frequency;
    int32_t gain_db;
    int result;
    unsigned tune_starts;
    unsigned tune_stops;
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

static int set_gain(void *context, int32_t gain_db)
{
    fake_radio *radio = context;
    ++radio->calls;
    radio->gain_db = gain_db;
    return radio->result;
}

static int start_tune(void *context, flex1500_tx_owner owner)
{
    fake_radio *radio = context;
    CHECK(owner == FLEX1500_TX_OWNER_TUNE || owner == FLEX1500_TX_OWNER_HTTP);
    ++radio->tune_starts;
    return radio->result;
}

static int stop_tune(void *context, flex1500_tx_owner owner)
{
    fake_radio *radio = context;
    CHECK(owner == FLEX1500_TX_OWNER_TUNE || owner == FLEX1500_TX_OWNER_HTTP);
    ++radio->tune_stops;
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
        .transmit_enabled = false, .frequency_known = true,
        .frequency_hz = 28475000,
    };
    return flex1500_api_dispatch(controller, request, &status, &radio,
                                 response, 8192, length, NULL, NULL);
}

static flex1500_api_action dispatch_at_frequency(
    flex1500_api_controller *controller, const char *request,
    uint32_t frequency_hz, char response[8192], size_t *length)
{
    const flex1500_service_status status = {
        .state = "test", .network_listening = true, .sample_rate = 48000,
    };
    const flex1500_radio_info radio = {
        .model = "FLEX-1500", .firmware = "test", .frequency_known = true,
        .frequency_hz = frequency_hz,
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
    CHECK(api.tx_drive_percent == FLEX1500_TX_DEFAULT_DRIVE_PERCENT);
    api.radio_context = &radio;
    api.tune_rx = tune_rx;
    api.set_rx_gain = set_gain;

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

    CHECK(dispatch(&api, "PUT /v1/radio/gain/-10 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(radio.calls == 2 && radio.gain_db == -10);
    CHECK(strstr(response, "\"rx_gain_db\":-10") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/gain/15 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(radio.calls == 2 && strstr(response, "404 Not Found") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/bandwidth/2400 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(flex1500_api_rx_bandwidth(&api) == 2400);
    CHECK(strstr(response, "\"rx_bandwidth_hz\":2400") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/squelch/-60 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(api.rx_squelch_db == -60);
    CHECK(strstr(response, "\"rx_squelch_db\":-60") != NULL);

    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/99999 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(radio.calls == 2);
    CHECK(strstr(response, "400 Bad Request") != NULL);

    radio.result = -1;
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RX_TUNE_FAILED);
    CHECK(radio.calls == 3);
    CHECK(strstr(response, "500 Internal Server Error") != NULL);
    radio.result = 0;

    CHECK(dispatch(&api, "GET /v1/stream/iq HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_OPEN_IQ_STREAM);
    CHECK(dispatch(&api, "GET /test HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_SERVE_TEST_PAGE);
    CHECK(dispatch(&api, "POST /v1/radio/ptt HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "404 Not Found") != NULL);

    flex1500_tune_control tune;
    flex1500_tx_control tx_control;
    flex1500_tx_control_init(&tx_control, true, 120000, &radio, start_tune,
                             stop_tune);
    flex1500_tune_control_init(&tune, true, &tx_control);
    api.tune_control = &tune;
    api.request_now_ms = 1000;
    api.next_tune_lease = 42;
    CHECK(dispatch_at_frequency(
              &api, "PUT /v1/radio/tune/start HTTP/1.1\r\n\r\n",
              30475000, response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "422 Unprocessable Content") != NULL);
    CHECK(strstr(response, "tx_frequency_not_allowed") != NULL);
    CHECK(radio.tune_starts == 0 && !tune.active);
    CHECK(dispatch_at_frequency(
              &api, "PUT /v1/radio/tune/start HTTP/1.1\r\n\r\n",
              10000000, response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "422 Unprocessable Content") != NULL);
    CHECK(radio.tune_starts == 0 && !tune.active);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-timeout/180 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL);
    CHECK(strstr(response, "\"tx_timeout_seconds\":180") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-timeout/0 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "400 Bad Request") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/tune/start HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL);
    CHECK(strstr(response, "\"lease\":42") != NULL);
    CHECK(strstr(response, "\"lease_timeout_ms\":15000") != NULL);
    CHECK(radio.tune_starts == 1 && tune.active);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/7200000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/mode/lsb HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/bandwidth/2400 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(api.rx_bandwidth_hz == 2400);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-drive/75 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-timeout/300 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    api.request_now_ms = 11000;
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tune/keepalive/42 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL && tune.active);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tune/stop/41 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "403 Forbidden") != NULL && tune.active);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tune/stop/42 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL && !tune.active);
    CHECK(radio.tune_stops == 1);
    flex1500_network_tx network_tx;
    flex1500_network_tx_init(&network_tx, true, &tx_control);
    api.network_tx = &network_tx;
    api.next_tx_lease = 77;
    api.request_now_ms = 20000;
    static const char create[] =
        "POST /v1/tx/sessions HTTP/1.1\r\ncontent-type: application/json\r\n"
        "content-length: 107\r\n\r\n"
        "{\"mode\":\"usb\",\"source\":\"audio\",\"drive_percent\":25,"
        "\"sample_rate\":48000,\"sample_format\":\"s16le\",\"channels\":1}";
    CHECK(dispatch(&api, create, response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "201 Created") != NULL);
    CHECK(strstr(response, "\"lease\":77") != NULL);
    CHECK(dispatch(&api,
        "CONNECT /v1/tx/stream HTTP/1.1\r\nX-Flex1500-TX-Lease: 77\r\n\r\n",
        response, &length) == FLEX1500_API_OPEN_TX_STREAM);
    CHECK(flex1500_network_tx_record_data(&network_tx, 77, 24000, 20001) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(dispatch(&api,
        "PUT /v1/tx/ptt/start HTTP/1.1\r\nX-Flex1500-TX-Lease: 77\r\n\r\n",
        response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "transmitting") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/7200000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(dispatch(&api, "PUT /v1/radio/mode/lsb HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/bandwidth/2400 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-drive/75 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "409 Conflict") != NULL);
    CHECK(network_tx.profile.drive_percent == 25);
    CHECK(dispatch(&api,
        "PUT /v1/tx/ptt/stop HTTP/1.1\r\nX-Flex1500-TX-Lease: 77\r\n\r\n",
        response, &length) == FLEX1500_API_RESPONSE);
    CHECK(dispatch(&api,
        "DELETE /v1/tx/sessions/current HTTP/1.1\r\nX-Flex1500-TX-Lease: 77\r\n\r\n",
        response, &length) == FLEX1500_API_RESPONSE);
    api.next_tx_lease = 78;
    CHECK(dispatch(&api, create, response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "\"lease\":78") != NULL);
    CHECK(dispatch(&api,
        "POST /v1/tx/audio HTTP/1.1\r\n"
        "x-flex1500-tx-lease: 78\r\ncontent-type: application/octet-stream\r\n"
        "content-length: 4\r\n\r\n\0\0\0\0",
        response, &length) == FLEX1500_API_PUSH_TX_AUDIO);
    CHECK(network_tx.stream_connected && network_tx.buffered_frames == 2);
    CHECK(dispatch(&api,
        "DELETE /v1/tx/sessions/current HTTP/1.1\r\nX-Flex1500-TX-Lease: 78\r\n\r\n",
        response, &length) == FLEX1500_API_RESPONSE);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-drive/75 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "\"tx_drive_percent\":75") != NULL);
    CHECK(api.tx_drive_percent == 75);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/mic-gain/10 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "\"tx_microphone_gain_db\":10") != NULL);
    CHECK(api.tx_microphone_gain_db == 10);
    CHECK(!api.tx_compressor_enabled);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-compressor/on HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "\"tx_compressor_enabled\":true") != NULL);
    CHECK(api.tx_compressor_enabled);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-compressor/maybe HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "400 Bad Request") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/mic-gain/71 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "400 Bad Request") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-drive/101 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "400 Bad Request") != NULL);
    CHECK(api.tx_drive_percent == 75);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-drive/0 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "400 Bad Request") != NULL);
    CHECK(api.tx_drive_percent == 75);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tx-drive/100 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "\"tx_drive_percent\":100") != NULL);

    flex1500_api_controller_init(&api, false, false, false);
    api.radio_context = &radio;
    api.tune_rx = tune_rx;
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(radio.calls == 3);
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
    CHECK(dispatch(&api,
        "POST /v1/tx/sessions HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
        response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "404 Not Found") != NULL);

    flex1500_station_owner station_owner;
    flex1500_station_owner_init(&station_owner);
    flex1500_api_controller_init(&api, true, true, true);
    api.station_owner = &station_owner;
    api.next_station_lease = 900;
    api.request_now_ms = 50000;
    CHECK(dispatch(&api, "POST /v1/control/owner HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "201 Created") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/frequency/14225000 HTTP/1.1\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "station_owned") != NULL);
    CHECK(dispatch(&api,
                   "POST /v1/tx/sessions HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: 2\r\n\r\n{}",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "station_owned") != NULL);
    api.tune_control = &tune;
    api.next_tune_lease = 910;
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tune/start HTTP/1.1\r\nX-Flex1500-Control-Lease: 900\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL && tune.active);
    CHECK(dispatch(&api,
                   "DELETE /v1/control/owner HTTP/1.1\r\nX-Flex1500-Control-Lease: 900\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "transmitter_active") != NULL);
    CHECK(dispatch(&api,
                   "PUT /v1/radio/tune/stop/910 HTTP/1.1\r\nX-Flex1500-Control-Lease: 900\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL && !tune.active);
    CHECK(dispatch(&api,
                   "DELETE /v1/control/owner HTTP/1.1\r\nX-Flex1500-Control-Lease: 901\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "410 Gone") != NULL);
    CHECK(dispatch(&api,
                   "DELETE /v1/control/owner HTTP/1.1\r\nX-Flex1500-Control-Lease: 900\r\n\r\n",
                   response, &length) == FLEX1500_API_RESPONSE);
    CHECK(strstr(response, "200 OK") != NULL);
    return 0;
}
