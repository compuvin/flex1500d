// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network.h"
#include "flex1500/web_ui.h"

#include "test_assert.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(void)
{
    flex1500_service_status status = {
        .state = "offline",
        .radio_open = false,
        .network_listening = false,
        .sample_rate = 48000,
        .network_frames_sent = 12,
        .network_samples_sent = 3072,
        .network_would_block_events = 4,
        .network_disconnects = 2,
        .network_write_errors = 1,
        .rx_tune_operations = 3,
        .radio_command_errors = 1,
        .usb_transfer_status_errors = 1,
        .usb_transfer_timeouts = 1,
        .usb_packet_status_errors = 3,
        .usb_packet_overflows = 3,
        .usb_short_packets = 2,
        .usb_missing_bytes = 268,
        .usb_error_events = 5,
        .usb_first_error_ms = 125,
        .usb_last_error_ms = 900,
        .first_sentinel_frame = 1000,
        .last_sentinel_frame = 2000,
        .first_sentinel_ms = 200,
        .last_sentinel_ms = 400,
        .physical_status_packets = 8,
        .physical_status_changes = 2,
        .tx_starts = 11,
        .tx_stops = 10,
        .tx_underruns = 9,
        .tx_clipped_frames = 12,
        .tx_dropped_microphone_frames = 13,
        .tx_rejected_ownership_requests = 8,
        .tx_watchdog_stops = 7,
        .tx_cleanup_failures = 6,
    };
    flex1500_radio_info radio = {
        .model = "FLEX-1500",
        .firmware = "0.5.3.24",
        .usb_vendor_id = 0x2192,
        .usb_product_id = 0x1502,
        .receive_only = true,
        .transmit_enabled = false,
        .transmit_prepared = false,
        .tune_enabled = false,
        .tune_active = false,
        .tx_timeout_seconds = 180,
        .tx_drive_percent = 50,
        .tx_microphone_gain_db = 10,
        .rx_tuning_enabled = true,
        .frequency_known = true,
        .frequency_hz = 10000000,
        .rx_filter_known = true,
        .rx_filter = 5,
        .rx_mode = "usb",
        .rx_bandwidth_hz = 2700,
        .physical_inputs_known = true,
        .mic_ptt = true,
    };
    char json[4096];
    char radio_json[1024];
    char http[2048];
    uint8_t frame[64];
    const flex1500_iq_sample samples[] = {{1.0f, -1.0f}};
    size_t page_length = 0;
    const char *page = flex1500_web_ui(&page_length);

    CHECK(page != NULL && page_length > 1000);
    CHECK(strstr(page, "FLEX-1500 receive test") != NULL);
    CHECK(strstr(page, "/v1/radio/frequency/") != NULL);
    CHECK(strstr(page, "/v1/stream/iq") != NULL);
    CHECK(strstr(page, "AudioWorkletNode") != NULL);
    CHECK(strstr(page, "Start audio") != NULL);
    CHECK(strstr(page, "<option>AM</option>") != NULL);
    CHECK(strstr(page, "<option>USB</option>") != NULL);
    CHECK(strstr(page, "<option>LSB</option>") != NULL);
    CHECK(strstr(page, "<option>FM</option>") != NULL);
    CHECK(strstr(page, "<option>CW</option>") != NULL);
    CHECK(strstr(page, "DSP bandwidth") != NULL);
    CHECK(strstr(page, "/v1/radio/mode/") != NULL);
    CHECK(strstr(page, "method:'PUT'") != NULL);
    CHECK(strstr(page, "PTT control") != NULL);
    CHECK(strstr(page, "/v1/radio/ptt") == NULL);
    CHECK(strstr(page, "execute-approved") == NULL);

    size_t json_length = flex1500_build_status_json(&status, json, sizeof(json));
    CHECK(json_length > 0);
    CHECK(strstr(json, "\"api_version\": 1") != NULL);
    CHECK(strstr(json, "\"radio_open\": false") != NULL);
    CHECK(strstr(json, "\"network_frames_sent\": 12") != NULL);
    CHECK(strstr(json, "\"network_samples_sent\": 3072") != NULL);
    CHECK(strstr(json, "\"network_would_block_events\": 4") != NULL);
    CHECK(strstr(json, "\"network_disconnects\": 2") != NULL);
    CHECK(strstr(json, "\"network_write_errors\": 1") != NULL);
    CHECK(strstr(json, "\"rx_tune_operations\": 3") != NULL);
    CHECK(strstr(json, "\"radio_command_errors\": 1") != NULL);
    CHECK(strstr(json, "\"usb_transfer_status_errors\": 1") != NULL);
    CHECK(strstr(json, "\"usb_transfer_timeouts\": 1") != NULL);
    CHECK(strstr(json, "\"usb_packet_status_errors\": 3") != NULL);
    CHECK(strstr(json, "\"usb_packet_overflows\": 3") != NULL);
    CHECK(strstr(json, "\"usb_short_packets\": 2") != NULL);
    CHECK(strstr(json, "\"usb_missing_bytes\": 268") != NULL);
    CHECK(strstr(json, "\"usb_first_error_ms\": 125") != NULL);
    CHECK(strstr(json, "\"last_sentinel_frame\": 2000") != NULL);
    CHECK(strstr(json, "\"physical_status_packets\": 8") != NULL);
    CHECK(strstr(json, "\"physical_status_changes\": 2") != NULL);
    CHECK(strstr(json, "\"tx_starts\": 11") != NULL);
    CHECK(strstr(json, "\"tx_stops\": 10") != NULL);
    CHECK(strstr(json, "\"tx_underruns\": 9") != NULL);
    CHECK(strstr(json, "\"tx_clipped_frames\": 12") != NULL);
    CHECK(strstr(json,
                 "\"tx_dropped_microphone_frames\": 13") != NULL);
    CHECK(strstr(json,
                 "\"tx_rejected_ownership_requests\": 8") != NULL);
    CHECK(strstr(json, "\"tx_watchdog_stops\": 7") != NULL);
    CHECK(strstr(json, "\"tx_cleanup_failures\": 6") != NULL);

    size_t radio_length = flex1500_build_radio_json(
        &radio, radio_json, sizeof(radio_json));
    CHECK(radio_length > 0);
    CHECK(strstr(radio_json, "\"model\": \"FLEX-1500\"") != NULL);
    CHECK(strstr(radio_json, "\"firmware\": \"0.5.3.24\"") != NULL);
    CHECK(strstr(radio_json, "\"usb_vendor_id\": \"2192\"") != NULL);
    CHECK(strstr(radio_json, "\"usb_product_id\": \"1502\"") != NULL);
    CHECK(strstr(radio_json, "\"receive_only\": true") != NULL);
    CHECK(strstr(radio_json, "\"transmit_enabled\": false") != NULL);
    CHECK(strstr(radio_json, "\"transmit_prepared\": false") != NULL);
    CHECK(strstr(radio_json, "\"tune_enabled\": false") != NULL);
    CHECK(strstr(radio_json, "\"tune_active\": false") != NULL);
    CHECK(strstr(radio_json, "\"tx_timeout_seconds\": 180") != NULL);
    CHECK(strstr(radio_json, "\"tx_drive_percent\": 50") != NULL);
    CHECK(strstr(radio_json,
                 "\"tx_microphone_gain_db\": 10") != NULL);
    CHECK(strstr(radio_json, "\"pa_filter\": null") != NULL);
    CHECK(strstr(radio_json, "\"rx_tuning_enabled\": true") != NULL);
    CHECK(strstr(radio_json, "\"frequency_hz\": 10000000") != NULL);
    CHECK(strstr(radio_json, "\"rx_filter\": 5") != NULL);
    CHECK(strstr(radio_json, "\"rx_gain_db\": null") != NULL);
    CHECK(strstr(radio_json, "\"rx_mode\": \"usb\"") != NULL);
    CHECK(strstr(radio_json, "\"rx_bandwidth_hz\": 2700") != NULL);
    CHECK(strstr(radio_json, "\"physical_inputs_known\": true") != NULL);
    CHECK(strstr(radio_json, "\"mic_ptt\": true") != NULL);
    CHECK(strstr(radio_json, "\"flexwire_ptt\": false") != NULL);

    size_t http_length = flex1500_build_http_response(
        "GET /v1/status HTTP/1.1\r\n\r\n", json, radio_json, http,
        sizeof(http));
    CHECK(http_length > json_length);
    CHECK(strncmp(http, "HTTP/1.1 200 OK\r\n", 17) == 0);
    CHECK(strstr(http, "Content-Type: application/json") != NULL);

    {
        int sockets[2];
        char received[2048] = {0};
        CHECK(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
        ssize_t sent = send(sockets[0], http, http_length, 0);
        if (sent < 0 && errno == EPERM) {
            /* Some build sandboxes prohibit send() even on AF_UNIX pairs. */
        } else {
            CHECK(sent == (ssize_t)http_length);
            ssize_t received_length =
                recv(sockets[1], received, sizeof(received), 0);
            CHECK(received_length == (ssize_t)http_length);
            CHECK(memcmp(received, http, http_length) == 0);
        }
        close(sockets[0]);
        close(sockets[1]);
    }

    http_length = flex1500_build_http_response(
        "GET /v1/stream/iq HTTP/1.1\r\n\r\n", json, radio_json, http,
        sizeof(http));
    CHECK(http_length > 0);
    CHECK(strstr(http, "503 Service Unavailable") != NULL);

    http_length = flex1500_build_http_response(
        "GET /v1/radio HTTP/1.1\r\n\r\n", json, radio_json, http,
        sizeof(http));
    CHECK(http_length > radio_length);
    CHECK(strstr(http, "200 OK") != NULL);
    CHECK(strstr(http, "\"transmit_enabled\": false") != NULL);

    http_length = flex1500_build_http_response(
        "POST /v1/radio/ptt HTTP/1.1\r\n\r\n", json, radio_json, http,
        sizeof(http));
    CHECK(strstr(http, "404 Not Found") != NULL);
    http_length = flex1500_build_http_response(
        "PUT /v1/transmit HTTP/1.1\r\n\r\n", json, radio_json, http,
        sizeof(http));
    CHECK(strstr(http, "404 Not Found") != NULL);

    CHECK(!flex1500_http_request_complete("GET /v1/status HTTP/1.1\r\n", 25));
    CHECK(flex1500_http_request_complete(
        "GET /v1/status HTTP/1.1\r\n\r\n", 27));
    CHECK(flex1500_http_request_complete("GET /\n\n", 7));
    uint32_t requested_frequency = 0;
    CHECK(flex1500_parse_rx_frequency_request(
        "PUT /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
        &requested_frequency));
    CHECK(requested_frequency == 10000000);
    CHECK(!flex1500_parse_rx_frequency_request(
        "GET /v1/radio/frequency/10000000 HTTP/1.1\r\n\r\n",
        &requested_frequency));
    CHECK(!flex1500_parse_rx_frequency_request(
        "PUT /v1/radio/frequency/-1 HTTP/1.1\r\n\r\n",
        &requested_frequency));
    CHECK(!flex1500_parse_rx_frequency_request(
        "PUT /v1/radio/frequency/4294967296 HTTP/1.1\r\n\r\n",
        &requested_frequency));
    const char *requested_mode = NULL;
    CHECK(flex1500_parse_rx_mode_request(
        "PUT /v1/radio/mode/cw HTTP/1.1\r\n\r\n", &requested_mode));
    CHECK(strcmp(requested_mode, "cw") == 0);
    CHECK(flex1500_parse_rx_mode_request(
        "PUT /v1/radio/mode/lsb HTTP/1.1\r\n\r\n", &requested_mode));
    CHECK(strcmp(requested_mode, "lsb") == 0);
    CHECK(!flex1500_parse_rx_mode_request(
        "PUT /v1/radio/mode/digital HTTP/1.1\r\n\r\n", &requested_mode));
    int32_t requested_gain = 0;
    CHECK(flex1500_parse_rx_gain_request(
        "PUT /v1/radio/gain/-10 HTTP/1.1\r\n\r\n", &requested_gain));
    CHECK(requested_gain == -10);
    CHECK(flex1500_parse_rx_gain_request(
        "PUT /v1/radio/gain/30 HTTP/1.1\r\n\r\n", &requested_gain));
    CHECK(requested_gain == 30);
    CHECK(!flex1500_parse_rx_gain_request(
        "PUT /v1/radio/gain/15 HTTP/1.1\r\n\r\n", &requested_gain));
    uint32_t requested_bandwidth = 0;
    CHECK(flex1500_parse_rx_bandwidth_request(
        "PUT /v1/radio/bandwidth/2400 HTTP/1.1\r\n\r\n", &requested_bandwidth));
    CHECK(requested_bandwidth == 2400);
    CHECK(!flex1500_parse_rx_bandwidth_request(
        "PUT /v1/radio/bandwidth/50 HTTP/1.1\r\n\r\n", &requested_bandwidth));
    int32_t requested_squelch = 0;
    CHECK(flex1500_parse_rx_squelch_request(
        "PUT /v1/radio/squelch/-60 HTTP/1.1\r\n\r\n", &requested_squelch));
    CHECK(requested_squelch == -60);
    CHECK(!flex1500_parse_rx_squelch_request(
        "PUT /v1/radio/squelch/1 HTTP/1.1\r\n\r\n", &requested_squelch));

    size_t frame_length =
        flex1500_encode_iq_frame(0x01020304, samples, 1, frame, sizeof(frame));
    CHECK(frame_length == FLEX1500_IQ_FRAME_HEADER_SIZE + 8);
    CHECK(memcmp(frame, "F15I", 4) == 0);
    CHECK(frame[4] == 1 && frame[5] == 1);
    CHECK(frame[6] == 0 && frame[7] == 20);
    CHECK(frame[8] == 1 && frame[9] == 2 && frame[10] == 3 && frame[11] == 4);
    CHECK(frame[12] == 0 && frame[13] == 0 && frame[14] == 0xbb &&
           frame[15] == 0x80);
    CHECK(frame[19] == 1);
    CHECK(frame[20] == 0x00 && frame[21] == 0x00 && frame[22] == 0x80 &&
           frame[23] == 0x3f);
    CHECK(frame[24] == 0x00 && frame[25] == 0x00 && frame[26] == 0x80 &&
           frame[27] == 0xbf);
    CHECK(flex1500_encode_iq_frame(0, samples, 1, frame, 10) == 0);
    return 0;
}
