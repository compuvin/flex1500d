// SPDX-License-Identifier: GPL-3.0-only

#define _POSIX_C_SOURCE 200809L

#include "flex1500/api.h"
#include "flex1500/config.h"
#include "flex1500/dsp.h"
#include "flex1500/iq.h"
#include "flex1500/network.h"
#include "flex1500/publisher.h"
#include "flex1500/protocol.h"
#include "flex1500/rtl_tcp.h"
#include "flex1500/usb_rx.h"
#include "flex1500/web_ui.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/random.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <poll.h>
#include <unistd.h>

enum {
    INPUT_BYTES = 4096,
    INPUT_FRAMES = INPUT_BYTES / 4,
    RING_CAPACITY = 48000,
};

static const flex1500_radio_info RADIO_INFO = {
    .model = "FLEX-1500",
    .firmware = "0.5.3.24",
    .usb_vendor_id = 0x2192,
    .usb_product_id = 0x1502,
    .receive_only = true,
    .transmit_enabled = false,
    .rx_mode = "am",
    .rx_bandwidth_hz = 6000,
};

typedef struct http_client_state {
    int fd;
    char request[16384];
    size_t length;
} http_client_state;

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static uint64_t initial_tune_lease(void)
{
    uint64_t lease = 0;
    ssize_t count = getrandom(&lease, sizeof(lease), GRND_NONBLOCK);
    if (count != (ssize_t)sizeof(lease) || lease == 0) {
        lease = monotonic_ms() ^ ((uint64_t)(unsigned int)getpid() << 32);
    }
    lease &= (UINT64_C(1) << 53) - 1;
    return lease != 0 ? lease : UINT64_C(1);
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--help] [--status] [--analyze IQ16LE_FILE]\n", program);
    printf("       %s --frame-capture IQ16LE_FILE OUTPUT_FILE\n", program);
    printf("       %s --demod IQ16LE_FILE am|fm|usb|lsb WAV_FILE [OFFSET_HZ]\n",
           program);
    printf("       %s --serve-offline PORT\n", program);
    printf("       %s --serve-offline PORT --enable-test-page\n", program);
    printf("       %s --serve-live-rx PORT --initialize-radio\n", program);
    printf("       %s --serve-live-rx PORT --initialize-radio-and-enable-rx-tuning\n",
           program);
    printf("       %s --serve-live-rx PORT --initialize-radio-and-enable-rx-tuning --enable-test-page\n",
           program);
    printf("       %s --serve-live-rx PORT --initialize-radio-and-enable-transmit [--enable-test-page]\n",
           program);
    printf("       %s [--config PATH] [--daemon|--no-daemon]\n", program);
    printf("          [--radio-mode offline|receive|rx-tuning|transmit]\n");
    printf("          [--http-bind IPV4] [--http-port PORT]\n");
    printf("          [--enable-test-page|--disable-test-page]\n");
    printf("          [--enable-rtl-tcp|--disable-rtl-tcp]\n");
    printf("          [--rtl-tcp-bind IPV4] [--rtl-tcp-port PORT]\n");
    printf("       %s [--config PATH] --check-config|--print-effective-config\n",
           program);
    printf("Default configuration: %s (optional when not explicitly named).\n",
           FLEX1500_DEFAULT_CONFIG_PATH);
    puts("Default, status, analysis, demod, framing, and offline-server modes");
    puts("do not open the radio. --frame-capture is offline file conversion.");
    puts("--demod is offline and writes 48 kHz mono PCM16 WAV audio.");
    puts("Server modes listen on all IPv4 interfaces. Restrict TCP port 15000");
    puts("to trusted LAN hosts with a firewall; authentication is not implemented.");
    puts("Live RX always sends opcode-1219 INITIALIZE. The separately armed");
    puts("tuning mode may also send RX-frequency and RX-filter commands.");
    puts("Never run live hardware modes without the radio operator's explicit permission.");
    puts("Only --initialize-radio-and-enable-transmit prepares the PA path and");
    puts("enables Tune, physical-mic, and leased HTTP audio/IQ TX. Leases");
    puts("are safety ownership values, not authentication credentials.");
}

static void print_idle_status(void)
{
    flex1500_service_status status = {
        .software_version = FLEX1500_SOFTWARE_VERSION,
        .git_revision = FLEX1500_GIT_REVISION,
        .state = "offline",
        .radio_open = false,
        .network_listening = false,
        .sample_rate = 48000,
    };
    char json[4096];
    if (flex1500_build_status_json(&status, json, sizeof(json)) > 0) {
        fputs(json, stdout);
    }
}

static volatile sig_atomic_t server_stop_requested;

static void request_server_stop(int signal_number)
{
    (void)signal_number;
    server_stop_requested = 1;
}

static int send_all(int socket_fd, const char *data, size_t length)
{
    size_t sent = 0;
    unsigned int waits = 0;
    while (sent < length) {
        ssize_t result = send(socket_fd, data + sent, length - sent,
                              MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR) continue;
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (++waits > 50) return -1;
            struct pollfd writable = {.fd = socket_fd, .events = POLLOUT};
            int ready = poll(&writable, 1, 100);
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0 || (ready > 0 &&
                (writable.revents & (POLLERR | POLLHUP | POLLNVAL)))) return -1;
            continue;
        }
        if (result <= 0) return -1;
        waits = 0;
        sent += (size_t)result;
    }
    return 0;
}

static const char *tx_api_operation(const char *request)
{
    if (strncmp(request, "POST /v1/tx/sessions HTTP/", 26) == 0)
        return "session acquire";
    if (strncmp(request, "DELETE /v1/tx/sessions/current HTTP/", 36) == 0)
        return "session release";
    if (strncmp(request, "PUT /v1/tx/ptt/start HTTP/", 26) == 0)
        return "PTT start";
    if (strncmp(request, "PUT /v1/tx/ptt/stop HTTP/", 25) == 0)
        return "PTT stop";
    if (strncmp(request, "CONNECT /v1/tx/stream HTTP/", 27) == 0)
        return "stream connect";
    if (strncmp(request, "POST /v1/tx/audio HTTP/", 23) == 0)
        return "audio chunk";
    return NULL;
}

static void log_tx_api_result(const char *request, const char *response,
                              size_t response_length,
                              const flex1500_network_tx *tx)
{
    const char *operation = tx_api_operation(request);
    if (operation == NULL) return;
    const char *status = response_length >= 12 ? response + 9 : "accepted";
    const bool success = response_length == 0 ||
        strncmp(status, "200", 3) == 0 || strncmp(status, "201", 3) == 0;
    const char *reason = NULL;
    if (!success) {
        if (strstr(response, "\"error\":\"tx_not_ready\"") != NULL)
            reason = "prebuffer-not-ready";
        else if (strstr(response, "\"error\":\"station_owned\"") != NULL)
            reason = "station-owned";
        else if (strstr(response, "\"error\":\"tx_busy\"") != NULL)
            reason = "transmitter-busy";
        else if (strstr(response, "\"error\":\"tx_stale\"") != NULL)
            reason = "stale-lease";
        else if (strstr(response, "\"error\":\"tx_invalid\"") != NULL)
            reason = "invalid-request";
    }
    /* Successful audio chunks arrive frequently; only log their failures. */
    if (strcmp(operation, "audio chunk") == 0 && success) return;
    fprintf(success ? stdout : stderr,
            "[tx-api] %s -> %.3s; reserved=%s keyed=%s stream=%s%s%s\n",
            operation, status, tx->reserved ? "yes" : "no",
            tx->keyed ? "yes" : "no",
            tx->stream_connected ? "yes" : "no",
            reason != NULL ? "; reason=" : "",
            reason != NULL ? reason : "");
    fflush(success ? stdout : stderr);
}

static int send_web_ui(int socket_fd)
{
    size_t page_length = 0;
    const char *page = flex1500_web_ui(&page_length);
    char header[256];
    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %zu\r\nConnection: close\r\n"
        "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n",
        page_length);
    if (header_length <= 0 || (size_t)header_length >= sizeof(header)) return -1;
    return send_all(socket_fd, header, (size_t)header_length) == 0 ?
        send_all(socket_fd, page, page_length) : -1;
}

static int open_listener(const char *bind_address, const char *port_text,
                         unsigned long *port)
{
    char *end = NULL;
    *port = strtoul(port_text, &end, 10);
    if (end == port_text || *end != '\0' || *port == 0 || *port > 65535) {
        fprintf(stderr, "Invalid port: %s\n", port_text);
        return -1;
    }
    int listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0) return -1;
    int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    struct sockaddr_in address = {
        .sin_family = AF_INET,
        .sin_port = htons((uint16_t)*port),
    };
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 bind address: %s\n", bind_address);
        close(listener);
        return -1;
    }
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 8) != 0) {
        close(listener);
        return -1;
    }
    return listener;
}

static int serve_offline_at(const char *bind_address, const char *port_text,
                            bool test_page_enabled)
{
    unsigned long parsed;
    int listener = open_listener(bind_address, port_text, &parsed);
    if (listener < 0) {
        perror("open listener");
        return EXIT_FAILURE;
    }

    server_stop_requested = 0;
    signal(SIGINT, request_server_stop);
    signal(SIGTERM, request_server_stop);
    printf("flex1500d offline API listening on %s:%lu (unauthenticated trusted-LAN access)\n",
           bind_address, parsed);
    fflush(stdout);

    flex1500_service_status status = {
        .software_version = FLEX1500_SOFTWARE_VERSION,
        .git_revision = FLEX1500_GIT_REVISION,
        .state = "offline_serving",
        .radio_open = false,
        .network_listening = true,
        .sample_rate = 48000,
    };
    flex1500_api_controller api;
    flex1500_api_controller_init(&api, false, false, test_page_enabled);

    while (!server_stop_requested) {
        struct pollfd poll_fd = {.fd = listener, .events = POLLIN};
        int poll_result = poll(&poll_fd, 1, 500);
        if (poll_result <= 0) continue;
        int client = accept(listener, NULL, NULL);
        if (client < 0) {
            if (server_stop_requested) break;
            continue;
        }
        char request[2048] = {0};
        ssize_t received = 0;
        while ((size_t)received < sizeof(request) - 1 &&
               !flex1500_http_request_complete(request, (size_t)received)) {
            ssize_t count = recv(client, request + received,
                                 sizeof(request) - 1 - (size_t)received, 0);
            if (count <= 0) break;
            received += count;
        }
        if (received > 0 &&
            flex1500_http_request_complete(request, (size_t)received)) {
            api.request_now_ms = monotonic_ms();
            char response[4096];
            size_t response_length = 0;
            flex1500_api_action action = flex1500_api_dispatch(
                &api, request, &status, &RADIO_INFO, response,
                sizeof(response), &response_length, NULL, NULL);
            if (action == FLEX1500_API_SERVE_TEST_PAGE) {
                send_web_ui(client);
            } else if (response_length > 0) {
                send_all(client, response, response_length);
            }
        }
        close(client);
    }
    close(listener);
    return EXIT_SUCCESS;
}

static int serve_offline(const char *port_text, bool test_page_enabled)
{
    return serve_offline_at("0.0.0.0", port_text, test_page_enabled);
}

enum { MAX_IQ_CLIENTS = 4 };
typedef struct iq_clients {
    int fd[MAX_IQ_CLIENTS];
    size_t pending_offset[MAX_IQ_CLIENTS];
    int rtl_fd;
    flex1500_rtl_tcp_resampler rtl_resampler;
    uint8_t rtl_pending[FLEX1500_PUBLISHER_MAX_SAMPLES * 2 * 64];
    size_t rtl_pending_length;
    size_t rtl_pending_offset;
    bool rtl_source_complete;
} iq_clients;

static flex1500_publish_result write_iq_clients(
    void *context, const uint8_t *bytes, size_t length, size_t *written)
{
    iq_clients *clients = context;
    bool any = false;
    bool incomplete = false;
    if (clients->rtl_fd >= 0) {
        if (!clients->rtl_source_complete &&
            clients->rtl_pending_length == 0) {
            clients->rtl_pending_length = flex1500_rtl_tcp_resample_iq(
                &clients->rtl_resampler, bytes, length, clients->rtl_pending,
                sizeof(clients->rtl_pending));
            clients->rtl_pending_offset = 0;
            if (clients->rtl_pending_length == 0) {
                close(clients->rtl_fd);
                clients->rtl_fd = -1;
            }
        }
        if (clients->rtl_fd >= 0) {
            size_t remaining = clients->rtl_pending_length -
                               clients->rtl_pending_offset;
            ssize_t result = send(
                clients->rtl_fd,
                clients->rtl_pending + clients->rtl_pending_offset,
                remaining, MSG_NOSIGNAL | MSG_DONTWAIT);
            if (result > 0) {
                clients->rtl_pending_offset += (size_t)result;
                if (clients->rtl_pending_offset < clients->rtl_pending_length) {
                    incomplete = true;
                } else {
                    clients->rtl_pending_length = 0;
                    clients->rtl_pending_offset = 0;
                    clients->rtl_source_complete = true;
                    any = true;
                }
            } else if (result < 0 &&
                       (errno == EAGAIN || errno == EWOULDBLOCK)) {
                incomplete = true;
            } else {
                close(clients->rtl_fd);
                clients->rtl_fd = -1;
                clients->rtl_pending_length = 0;
                clients->rtl_pending_offset = 0;
                clients->rtl_source_complete = false;
                printf("[rtl_tcp] client disconnected while streaming\n");
                fflush(stdout);
            }
        } else if (clients->rtl_source_complete) {
            any = true;
        }
    }
    for (size_t i = 0; i < MAX_IQ_CLIENTS; ++i) {
        if (clients->fd[i] < 0) continue;
        if (clients->pending_offset[i] == length) {
            any = true;
            continue;
        }
        ssize_t result = send(clients->fd[i],
                              bytes + clients->pending_offset[i],
                              length - clients->pending_offset[i],
                              MSG_NOSIGNAL | MSG_DONTWAIT);
        if (result > 0) {
            any = true;
            clients->pending_offset[i] += (size_t)result;
            if (clients->pending_offset[i] < length) incomplete = true;
            continue;
        }
        if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            any = true;
            incomplete = true;
            continue;
        }
        close(clients->fd[i]);
        clients->fd[i] = -1;
        clients->pending_offset[i] = 0;
    }
    if (!any) {
        *written = 0;
        return FLEX1500_PUBLISH_DISCONNECTED;
    }
    if (incomplete) {
        *written = 0;
        return FLEX1500_PUBLISH_WOULD_BLOCK;
    }
    for (size_t i = 0; i < MAX_IQ_CLIENTS; ++i)
        clients->pending_offset[i] = 0;
    clients->rtl_source_complete = false;
    *written = length;
    return FLEX1500_PUBLISH_OK;
}

static int api_tune_rx(void *context, uint32_t frequency_hz,
                       uint32_t *rx_filter)
{
    flex1500_usb_rx *receiver = context;
    if (flex1500_usb_rx_tune(receiver, frequency_hz) != 0) return -1;
    return flex1500_usb_rx_filter(receiver, rx_filter) ? 0 : -1;
}

static int api_set_rx_gain(void *context, int32_t gain_db)
{
    return flex1500_usb_rx_set_gain(context, gain_db) == 0 ? 0 : -1;
}

static float meter_dbfs(float linear)
{
    return linear > 0.000001f ? 20.0f * log10f(linear) : -120.0f;
}

static flex1500_service_status live_service_status(
    const flex1500_usb_rx *receiver, const flex1500_iq_ring *ring,
    const flex1500_iq_publisher *publisher,
    const flex1500_tune_control *tune_control, uint64_t recovery_attempts,
    uint64_t recovery_successes)
{
    const flex1500_usb_rx_counters *usb =
        flex1500_usb_rx_get_counters(receiver);
    const flex1500_iq_stats *iq = flex1500_usb_rx_get_iq_stats(receiver);
    const flex1500_tx_diagnostics *tx =
        flex1500_tune_control_diagnostics(tune_control);
    const flex1500_tx_audio_stats *audio =
        flex1500_usb_rx_microphone_tx_stats(receiver);
    bool audio_tx_active = tune_control != NULL &&
        tune_control->tx_control != NULL &&
        (tune_control->tx_control->owner == FLEX1500_TX_OWNER_HTTP ||
         tune_control->tx_control->owner == FLEX1500_TX_OWNER_PHYSICAL_MIC);
    bool meter_valid = audio != NULL && audio->microphone_frames != 0;
    uint64_t queued_frames = flex1500_usb_rx_tx_pending_frames(receiver);
    uint64_t peak_queued_frames = tx != NULL ? tx->peak_queued_frames : 0;
    if (audio != NULL && audio->peak_queued_frames > peak_queued_frames) {
        peak_queued_frames = audio->peak_queued_frames;
    }
    if (queued_frames > peak_queued_frames) {
        peak_queued_frames = queued_frames;
    }
    return (flex1500_service_status){
        .software_version = FLEX1500_SOFTWARE_VERSION,
        .git_revision = FLEX1500_GIT_REVISION,
        .state = "receiving", .radio_open = true, .network_listening = true,
        .sample_rate = 48000, .frames = iq->frames,
        .sentinel_frames = iq->sentinel_frames, .usb_packets = usb->usb_packets,
        .usb_packet_errors = usb->usb_packet_errors,
        .ring_dropped_frames = ring->dropped,
        .network_frames_sent = publisher->stats.frames_sent,
        .network_samples_sent = publisher->stats.samples_sent,
        .network_would_block_events = publisher->stats.would_block_events,
        .network_disconnects = publisher->stats.disconnects,
        .network_write_errors = publisher->stats.write_errors,
        .rx_tune_operations = usb->rx_tune_operations,
        .radio_command_errors = usb->command_errors,
        .usb_transfer_status_errors = usb->transfer_status_errors,
        .usb_transfer_timeouts = usb->transfer_timeouts,
        .usb_transfer_stalls = usb->transfer_stalls,
        .usb_transfer_no_device = usb->transfer_no_device,
        .usb_transfer_overflows = usb->transfer_overflows,
        .usb_transfer_other_errors = usb->transfer_other_errors,
        .usb_packet_status_errors = usb->packet_status_errors,
        .usb_packet_timeouts = usb->packet_timeouts,
        .usb_packet_stalls = usb->packet_stalls,
        .usb_packet_no_device = usb->packet_no_device,
        .usb_packet_overflows = usb->packet_overflows,
        .usb_packet_other_errors = usb->packet_other_errors,
        .usb_short_packets = usb->short_packets,
        .usb_zero_length_packets = usb->zero_length_packets,
        .usb_oversized_packets = usb->oversized_packets,
        .usb_missing_bytes = usb->missing_bytes,
        .usb_trailing_bytes = usb->trailing_bytes,
        .usb_error_events = usb->error_events,
        .physical_status_packets = usb->status_packets,
        .physical_status_changes = usb->status_changes,
        .physical_status_errors = usb->status_errors,
        .usb_first_error_ms = usb->first_error_ms,
        .usb_last_error_ms = usb->last_error_ms,
        .first_sentinel_frame = usb->first_sentinel_frame,
        .last_sentinel_frame = usb->last_sentinel_frame,
        .first_sentinel_ms = usb->first_sentinel_ms,
        .last_sentinel_ms = usb->last_sentinel_ms,
        .rx_recovery_attempts = recovery_attempts,
        .rx_recovery_successes = recovery_successes,
        .tx_starts = tx != NULL ? tx->starts : 0,
        .tx_stops = tx != NULL ? tx->stops : 0,
        .tx_underruns = tx != NULL ? tx->underruns : 0,
        .tx_clipped_frames = tx != NULL ? tx->clipped_frames : 0,
        .tx_limited_frames = (tx != NULL ? tx->limited_frames : 0) +
            (audio_tx_active && audio != NULL ? audio->limited_frames : 0),
        .tx_dropped_microphone_frames =
            tx != NULL ? tx->dropped_microphone_frames : 0,
        .tx_queued_frames = queued_frames,
        .tx_queued_ms = (queued_frames + 47) / 48,
        .tx_peak_queued_frames = peak_queued_frames,
        .tx_stop_requested_frames =
            tx != NULL ? tx->stop_requested_frames : 0,
        .tx_graceful_drained_frames =
            tx != NULL ? tx->graceful_drained_frames : 0,
        .tx_graceful_discarded_frames =
            tx != NULL ? tx->graceful_discarded_frames : 0,
        .tx_graceful_drain_ms = tx != NULL ? tx->graceful_drain_ms : 0,
        .tx_audio_meter_valid = meter_valid,
        .tx_input_peak_dbfs = meter_valid
            ? meter_dbfs(audio->input_peak) : -120.0f,
        .tx_input_rms_dbfs = meter_valid
            ? meter_dbfs(audio->input_rms) : -120.0f,
        .tx_post_gain_peak_dbfs = meter_valid
            ? meter_dbfs(audio->post_gain_peak) : -120.0f,
        .tx_post_gain_rms_dbfs = meter_valid
            ? meter_dbfs(audio->post_gain_rms) : -120.0f,
        .tx_output_peak_dbfs = meter_valid
            ? meter_dbfs(audio->output_peak) : -120.0f,
        .tx_output_rms_dbfs = meter_valid
            ? meter_dbfs(audio->output_rms) : -120.0f,
        .tx_rejected_ownership_requests =
            tx != NULL ? tx->rejected_ownership_requests : 0,
        .tx_watchdog_stops = tx != NULL ? tx->watchdog_stops : 0,
        .tx_cleanup_failures = tx != NULL ? tx->cleanup_failures : 0,
    };
}

static flex1500_radio_info live_radio_info(const flex1500_usb_rx *receiver,
                                           bool transmit_enabled)
{
    flex1500_radio_info radio = RADIO_INFO;
    radio.receive_only = !transmit_enabled;
    radio.transmit_enabled = transmit_enabled;
    radio.transmit_prepared = flex1500_usb_rx_transmit_prepared(receiver);
    radio.tune_enabled = transmit_enabled;
    radio.tune_active = flex1500_usb_rx_tune_carrier_active(receiver);
    radio.pa_filter_known = flex1500_usb_rx_pa_filter(receiver,
                                                      &radio.pa_filter);
    flex1500_physical_inputs inputs;
    radio.frequency_known = flex1500_usb_rx_frequency(
        receiver, &radio.frequency_hz);
    radio.rx_filter_known = flex1500_usb_rx_filter(receiver, &radio.rx_filter);
    radio.rx_gain_known = flex1500_usb_rx_gain(receiver, &radio.rx_gain_db);
    radio.physical_inputs_known = flex1500_usb_rx_physical_inputs(
        receiver, &inputs);
    if (radio.physical_inputs_known) {
        radio.mic_ptt = inputs.mic_ptt;
        radio.flexwire_ptt = inputs.flexwire_ptt;
        radio.dash = inputs.dash;
        radio.dot = inputs.dot;
    }
    return radio;
}

typedef struct daemon_tx_context {
    flex1500_usb_rx *receiver;
    flex1500_api_controller *api;
    flex1500_tune_control *tune_control;
    uint32_t frequency_hz;
    const char *mode;
    uint32_t drive_percent;
    uint32_t microphone_gain_db;
    bool compressor_enabled;
    flex1500_network_tx *network_tx;
    uint8_t network_prebuffer[131072];
    size_t network_prebuffer_bytes;
    uint8_t network_tail[4];
    size_t network_tail_bytes;
} daemon_tx_context;

static bool resolve_physical_tx_mode(daemon_tx_context *tx,
                                     uint32_t frequency_hz,
                                     flex1500_tx_mode *mode,
                                     const char **mode_name)
{
    if (tx == NULL || tx->api == NULL || mode == NULL || mode_name == NULL) {
        return false;
    }
    flex1500_station_owner *station = tx->api->station_owner;
    if (station != NULL && station->held && !station->mode_aware) {
        bool upper_sideband = false;
        if (!flex1500_default_ssb_upper_sideband(
                frequency_hz, &upper_sideband)) {
            return false;
        }
        *mode = upper_sideband ? FLEX1500_TX_USB : FLEX1500_TX_LSB;
        *mode_name = upper_sideband ? "usb" : "lsb";
        return true;
    }
    if (strcmp(tx->api->rx_mode, "usb") == 0) {
        *mode = FLEX1500_TX_USB;
    } else if (strcmp(tx->api->rx_mode, "lsb") == 0) {
        *mode = FLEX1500_TX_LSB;
    } else if (strcmp(tx->api->rx_mode, "am") == 0) {
        *mode = FLEX1500_TX_AM;
    } else {
        return false;
    }
    *mode_name = tx->api->rx_mode;
    return *mode == FLEX1500_TX_AM
        ? flex1500_am_frequency_allowed(frequency_hz)
        : flex1500_physical_mic_frequency_allowed(
              frequency_hz, *mode == FLEX1500_TX_USB);
}

static int daemon_tx_owner_start(void *context, flex1500_tx_owner owner)
{
    daemon_tx_context *tx = context;
    if (owner == FLEX1500_TX_OWNER_TUNE) {
        if (!flex1500_usb_rx_frequency(tx->receiver, &tx->frequency_hz) ||
            !flex1500_tune_frequency_allowed(tx->frequency_hz)) {
            return -1;
        }
        return flex1500_usb_rx_tune_carrier_start(tx->receiver);
    }
    if (owner == FLEX1500_TX_OWNER_HTTP && tx->network_tx != NULL &&
        tx->network_tx->reserved &&
        flex1500_usb_rx_frequency(tx->receiver, &tx->frequency_hz)) {
        bool raw_iq = tx->network_tx->profile.source == FLEX1500_NETWORK_TX_IQ;
        float dc_carrier_ratio = raw_iq
            ? flex1500_tx_raw_iq_dc_carrier_ratio(
                  tx->network_prebuffer, tx->network_prebuffer_bytes)
            : 0.0f;
        bool translate_raw_iq = raw_iq &&
            flex1500_tx_raw_iq_should_translate(
                tx->network_prebuffer, tx->network_prebuffer_bytes);
        flex1500_tx_mode mode =
            tx->network_tx->profile.mode == FLEX1500_NETWORK_TX_LSB
                ? FLEX1500_TX_LSB :
            tx->network_tx->profile.mode == FLEX1500_NETWORK_TX_AM
                ? FLEX1500_TX_AM : FLEX1500_TX_USB;
        bool frequency_allowed = raw_iq
            ? flex1500_network_iq_frequency_allowed(tx->frequency_hz)
            : mode == FLEX1500_TX_AM
                ? flex1500_am_frequency_allowed(tx->frequency_hz)
                : flex1500_physical_mic_frequency_allowed(
                      tx->frequency_hz, mode == FLEX1500_TX_USB);
        if (!frequency_allowed) return -1;
        int result = flex1500_usb_rx_network_tx_start(
            tx->receiver, mode, tx->network_tx->profile.drive_percent,
            raw_iq, translate_raw_iq, tx->network_prebuffer,
            tx->network_prebuffer_bytes);
        if (result == 0) {
            if (raw_iq) {
                printf("[tx] raw-IQ DC-carrier ratio=%.3f; "
                       "11.025 kHz translation=%s\n",
                       dc_carrier_ratio,
                       translate_raw_iq ? "enabled" : "disabled");
                fflush(stdout);
            }
            tx->network_prebuffer_bytes = 0;
            if (tx->tune_control != NULL) {
                ++tx->tune_control->diagnostics.starts;
            }
        }
        return result;
    }
    if (owner != FLEX1500_TX_OWNER_PHYSICAL_MIC || tx->api == NULL ||
        !flex1500_usb_rx_frequency(tx->receiver, &tx->frequency_hz)) {
        return -1;
    }
    flex1500_tx_mode mode;
    const char *mode_name = NULL;
    if (!resolve_physical_tx_mode(
            tx, tx->frequency_hz, &mode, &mode_name)) return -1;
    /* Freeze the complete profile before issuing the first TX command. */
    tx->mode = mode_name;
    tx->drive_percent = tx->api->tx_drive_percent;
    tx->microphone_gain_db = tx->api->tx_microphone_gain_db;
    tx->compressor_enabled = tx->api->tx_compressor_enabled;
    int result = flex1500_usb_rx_microphone_tx_start(
        tx->receiver, mode, tx->drive_percent,
        powf(10.0f, (float)tx->microphone_gain_db / 20.0f),
        tx->compressor_enabled);
    if (result == 0 && tx->tune_control != NULL) {
        ++tx->tune_control->diagnostics.starts;
    }
    return result;
}

static int daemon_tx_owner_stop(void *context, flex1500_tx_owner owner,
                                bool graceful)
{
    daemon_tx_context *tx = context;
    if (owner == FLEX1500_TX_OWNER_TUNE) {
        return flex1500_usb_rx_tune_carrier_stop(tx->receiver);
    }
    if (owner != FLEX1500_TX_OWNER_PHYSICAL_MIC &&
        owner != FLEX1500_TX_OWNER_HTTP) return -1;
    int result = graceful
        ? flex1500_usb_rx_microphone_tx_stop_graceful(
              tx->receiver, FLEX1500_TX_GRACEFUL_DRAIN_MS)
        : flex1500_usb_rx_microphone_tx_stop(tx->receiver);
    const flex1500_tx_audio_stats *stats =
        flex1500_usb_rx_microphone_tx_stats(tx->receiver);
    uint64_t underruns = stats != NULL ? stats->underrun_frames : 0;
    uint64_t clipped = stats != NULL ? stats->clipped_frames : 0;
    uint64_t limited = stats != NULL ? stats->limited_frames : 0;
    uint64_t dropped = stats != NULL
        ? stats->dropped_microphone_frames : 0;
    if (tx->tune_control != NULL) {
        ++tx->tune_control->diagnostics.stops;
        flex1500_tune_control_record_underrun(tx->tune_control, underruns);
        flex1500_tune_control_record_audio_quality(
            tx->tune_control, clipped, limited, dropped);
        if (stats != NULL) {
            uint64_t peak = stats->peak_queued_frames;
            if (stats->stop_requested_frames > peak) {
                peak = stats->stop_requested_frames;
            }
            flex1500_tune_control_record_graceful_drain(
                tx->tune_control, peak, stats->stop_requested_frames,
                stats->graceful_drained_frames,
                stats->graceful_discarded_frames,
                stats->graceful_drain_ms);
            if (graceful) {
                printf("[tx] graceful stop: pending=%llu frames (%.1f ms), "
                       "drained=%llu, discarded=%llu, elapsed=%llu ms\n",
                       (unsigned long long)stats->stop_requested_frames,
                       (double)stats->stop_requested_frames / 48.0,
                       (unsigned long long)stats->graceful_drained_frames,
                       (unsigned long long)stats->graceful_discarded_frames,
                       (unsigned long long)stats->graceful_drain_ms);
                fflush(stdout);
            }
        }
    }
    return result;
}

static void daemon_tx_shutdown(flex1500_tx_control *tx_control,
                               flex1500_tune_control *tune_control)
{
    bool tune_was_active = tune_control != NULL && tune_control->active;
    flex1500_tx_control_shutdown(tx_control);
    flex1500_tune_control_reconcile(tune_control, false);
    if (tune_was_active && tx_control != NULL && tx_control->cleanup_failed) {
        flex1500_tune_control_record_cleanup_failure(tune_control);
    }
}

static int serve_live_rx_at(const char *bind_address, const char *port_text,
                            bool rx_tuning_enabled, bool test_page_enabled,
                            bool transmit_enabled, bool rtl_tcp_enabled,
                            const char *rtl_bind_address,
                            const char *rtl_port_text)
{
    unsigned long port;
    int listener = open_listener(bind_address, port_text, &port);
    if (listener < 0) {
        perror("open listener");
        return EXIT_FAILURE;
    }
    if (fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK) < 0) {
        perror("set listener nonblocking");
        close(listener);
        return EXIT_FAILURE;
    }
    unsigned long rtl_port = 0;
    int rtl_listener = -1;
    if (rtl_tcp_enabled) {
        rtl_listener = open_listener(rtl_bind_address, rtl_port_text,
                                     &rtl_port);
        if (rtl_listener < 0 ||
            fcntl(rtl_listener, F_SETFL,
                  fcntl(rtl_listener, F_GETFL) | O_NONBLOCK) < 0) {
            perror("open rtl_tcp listener");
            if (rtl_listener >= 0) close(rtl_listener);
            close(listener);
            return EXIT_FAILURE;
        }
    }

    flex1500_iq_ring ring;
    if (!flex1500_iq_ring_init(&ring, RING_CAPACITY)) {
        if (rtl_listener >= 0) close(rtl_listener);
        close(listener);
        return EXIT_FAILURE;
    }
    flex1500_usb_rx *receiver = flex1500_usb_rx_create(&ring);
    flex1500_iq_publisher publisher;
    if (receiver == NULL ||
        !flex1500_iq_publisher_init(&publisher, &ring, 256)) {
        flex1500_usb_rx_destroy(receiver);
        flex1500_iq_ring_destroy(&ring);
        if (rtl_listener >= 0) close(rtl_listener);
        close(listener);
        return EXIT_FAILURE;
    }

    int usb_result = flex1500_usb_rx_start(receiver);
    if (usb_result != 0) {
        fprintf(stderr, "Live RX start failed: %s\n",
                flex1500_usb_rx_last_error(receiver));
        flex1500_usb_rx_destroy(receiver);
        flex1500_iq_ring_destroy(&ring);
        if (rtl_listener >= 0) close(rtl_listener);
        close(listener);
        return EXIT_FAILURE;
    }
    usb_result = flex1500_usb_rx_set_gain(receiver, 20);
    if (usb_result != 0) {
        fprintf(stderr, "Live RX gain initialization failed: %s\n",
                flex1500_usb_rx_last_error(receiver));
        flex1500_usb_rx_destroy(receiver);
        flex1500_iq_ring_destroy(&ring);
        if (rtl_listener >= 0) close(rtl_listener);
        close(listener);
        return EXIT_FAILURE;
    }
    if (transmit_enabled) {
        usb_result = flex1500_usb_rx_enable_transmit_preparation(receiver);
        if (usb_result != 0) {
            fprintf(stderr, "Transmit preparation failed: %s\n",
                    flex1500_usb_rx_last_error(receiver));
            flex1500_usb_rx_destroy(receiver);
            flex1500_iq_ring_destroy(&ring);
            if (rtl_listener >= 0) close(rtl_listener);
            close(listener);
            return EXIT_FAILURE;
        }
    }

    server_stop_requested = 0;
    signal(SIGINT, request_server_stop);
    signal(SIGTERM, request_server_stop);
    printf("[startup] FLEX-1500 initialized; receive-only USB stream active\n");
    printf("[startup] receive gain set to +20 dB\n");
    printf("[startup] API listening on %s:%lu (unauthenticated, tuning=%s, test-page=%s)\n",
           bind_address, port, rx_tuning_enabled ? "enabled" : "disabled",
           test_page_enabled ? "enabled" : "disabled");
    printf("[startup] 5 W Tune API: %s%s\n",
           transmit_enabled ? "enabled" : "disabled",
           transmit_enabled ? " (LAN-accessible; unauthenticated)" : "");
    printf("[startup] rtl_tcp RX compatibility: %s",
           rtl_tcp_enabled ? "enabled" : "disabled");
    if (rtl_tcp_enabled) printf(" on %s:%lu", rtl_bind_address, rtl_port);
    putchar('\n');
    fflush(stdout);
    iq_clients iq = {
        .fd = {-1, -1, -1, -1}, .rtl_fd = -1,
    };
    flex1500_rtl_tcp_parser rtl_parser = {0};
    int tx_client = -1;
    http_client_state http_client = {.fd = -1};
    flex1500_api_controller api;
    flex1500_api_controller_init(&api, rx_tuning_enabled, true,
                                 test_page_enabled);
    daemon_tx_context tx_context = {
        .receiver = receiver,
        .api = &api,
    };
    flex1500_tx_control tx_control;
    flex1500_tx_control_init(
        &tx_control, transmit_enabled,
        FLEX1500_TX_TIMEOUT_DEFAULT_SECONDS * UINT64_C(1000), &tx_context,
                             daemon_tx_owner_start, daemon_tx_owner_stop);
    if (transmit_enabled) {
        /* Initialization and TX preparation have established an unkeyed RX
         * state. The radio reports PTT edges rather than an initial release,
         * so waiting for a release here would discard the first real press. */
        flex1500_tx_control_arm_physical_ptt(&tx_control);
    }
    flex1500_tune_control tune_control;
    flex1500_tune_control_init(&tune_control, transmit_enabled, &tx_control);
    tx_context.tune_control = &tune_control;
    flex1500_network_tx network_tx;
    flex1500_network_tx_init(&network_tx, transmit_enabled, &tx_control);
    flex1500_station_owner station_owner;
    flex1500_station_owner_init(&station_owner);
    tx_context.network_tx = &network_tx;
    if (transmit_enabled) {
        api.tune_control = &tune_control;
        api.next_tune_lease = initial_tune_lease();
        api.next_tx_lease = initial_tune_lease();
        api.network_tx = &network_tx;
        api.station_owner = &station_owner;
        api.next_station_lease = initial_tune_lease();
    }
    api.radio_context = receiver;
    api.tune_rx = api_tune_rx;
    api.set_rx_gain = api_set_rx_gain;
    uint64_t recovery_attempts = 0;
    uint64_t recovery_successes = 0;
    bool logged_inputs_known = false;
    flex1500_physical_inputs logged_inputs = {0};
    bool microphone_ptt_known = false;
    bool observed_microphone_ptt = false;

    while (!server_stop_requested) {
        if (flex1500_station_owner_tick(&station_owner, monotonic_ms()) ==
            FLEX1500_STATION_OWNER_EXPIRED) {
            flex1500_tune_control_shutdown(&tune_control);
            if (network_tx.reserved)
                (void)flex1500_network_tx_release(&network_tx,
                                                   network_tx.lease);
            printf("[owner] station-control lease expired; controls released\n");
            fflush(stdout);
        }
        flex1500_tune_result tune_tick = flex1500_tune_control_tick(
            &tune_control, monotonic_ms());
        if (tune_tick == FLEX1500_TUNE_EXPIRED ||
            tune_tick == FLEX1500_TUNE_HARD_LIMIT) {
            printf("[tune] stopped by %s watchdog\n",
                   flex1500_tune_result_name(tune_tick));
            fflush(stdout);
        } else if (tune_tick == FLEX1500_TUNE_HARDWARE_ERROR) {
            fprintf(stderr, "[tune] watchdog cleanup failed: %s\n",
                    flex1500_usb_rx_last_error(receiver));
        }
        flex1500_tx_control_result tx_tick = flex1500_tx_control_tick(
            &tx_control, monotonic_ms());
        flex1500_tune_control_reconcile(
            &tune_control, tx_tick == FLEX1500_TX_CONTROL_MAX_KEY);
        if (tx_tick == FLEX1500_TX_CONTROL_MAX_KEY) {
            printf("[tx] stopped by configurable maximum-key timer\n");
            fflush(stdout);
        } else if (tx_tick == FLEX1500_TX_CONTROL_HARDWARE_ERROR) {
            flex1500_tune_control_record_cleanup_failure(&tune_control);
            fprintf(stderr, "[tx] maximum-key cleanup failed: %s\n",
                    flex1500_usb_rx_last_error(receiver));
        }
        bool network_was_reserved = network_tx.reserved;
        flex1500_network_tx_reconcile(&network_tx);
        if (network_was_reserved && !network_tx.reserved && tx_client >= 0) {
            close(tx_client);
            tx_client = -1;
            tx_context.network_prebuffer_bytes = 0;
            printf("[tx] HTTP session invalidated by owner transition\n");
        }
        flex1500_network_tx_result network_tick = flex1500_network_tx_tick(
            &network_tx, monotonic_ms());
        if (network_tick == FLEX1500_NETWORK_TX_EXPIRED ||
            network_tick == FLEX1500_NETWORK_TX_DATA_TIMEOUT) {
            ++tune_control.diagnostics.watchdog_stops;
            if (tx_client >= 0) close(tx_client);
            tx_client = -1;
            tx_context.network_prebuffer_bytes = 0;
            printf("[tx] HTTP session stopped by %s watchdog\n",
                   flex1500_network_tx_result_name(network_tick));
        } else if (network_tick == FLEX1500_NETWORK_TX_HARDWARE_ERROR) {
            flex1500_tune_control_record_cleanup_failure(&tune_control);
            fprintf(stderr, "[tx] HTTP watchdog cleanup failed: %s\n",
                    flex1500_usb_rx_last_error(receiver));
        }
        usb_result = flex1500_usb_rx_pump(receiver);
        if (usb_result != 0 || !flex1500_usb_rx_is_running(receiver)) {
            uint32_t restore_frequency = 0;
            int32_t restore_gain = 20;
            bool restore_frequency_known = flex1500_usb_rx_frequency(
                receiver, &restore_frequency);
            (void)flex1500_usb_rx_gain(receiver, &restore_gain);
            char failure[160];
            snprintf(failure, sizeof(failure), "%s",
                     flex1500_usb_rx_last_error(receiver));
            for (size_t i = 0; i < MAX_IQ_CLIENTS; ++i) {
                if (iq.fd[i] >= 0) close(iq.fd[i]);
                iq.fd[i] = -1;
            }
            flex1500_iq_publisher_disconnect(&publisher);
            if (tx_client >= 0) {
                close(tx_client);
                tx_client = -1;
            }
            (void)flex1500_network_tx_disconnect_stream(&network_tx);
            (void)flex1500_network_tx_release(&network_tx, network_tx.lease);
            tx_context.network_prebuffer_bytes = 0;
            daemon_tx_shutdown(&tx_control, &tune_control);
            flex1500_usb_rx_stop(receiver);
            fprintf(stderr, "[recovery] receive stopped: %s\n", failure);
            fprintf(stderr,
                    "[recovery] if 2192:1502 does not reappear, power-cycle "
                    "the FLEX-1500; a live USB reconnect may not re-enumerate\n");
            bool recovered = false;
            for (unsigned int attempt = 1;
                 attempt <= 60 && !server_stop_requested; ++attempt) {
                ++recovery_attempts;
                fprintf(stderr, "[recovery] attempt %u/60 in 1 second\n", attempt);
                poll(NULL, 0, 1000);
                if (flex1500_usb_rx_start(receiver) != 0) {
                    fprintf(stderr, "[recovery] reopen failed: %s\n",
                            flex1500_usb_rx_last_error(receiver));
                    continue;
                }
                if (flex1500_usb_rx_set_gain(receiver, restore_gain) != 0 ||
                    (restore_frequency_known && flex1500_usb_rx_tune(
                        receiver, restore_frequency) != 0)) {
                    fprintf(stderr, "[recovery] state restore failed: %s\n",
                            flex1500_usb_rx_last_error(receiver));
                    flex1500_usb_rx_stop(receiver);
                    continue;
                }
                flex1500_iq_ring_clear(&ring);
                ++recovery_successes;
                if (transmit_enabled &&
                    flex1500_usb_rx_enable_transmit_preparation(receiver) != 0) {
                    fprintf(stderr, "[recovery] transmit preparation failed: %s\n",
                            flex1500_usb_rx_last_error(receiver));
                    flex1500_usb_rx_stop(receiver);
                    continue;
                }
                flex1500_tx_diagnostics saved_tx_diagnostics =
                    tune_control.diagnostics;
                uint32_t saved_tx_timeout =
                    flex1500_tx_control_timeout_seconds(&tx_control);
                flex1500_tx_control_init(
                    &tx_control, transmit_enabled,
                    (uint64_t)saved_tx_timeout * UINT64_C(1000),
                    &tx_context,
                    daemon_tx_owner_start, daemon_tx_owner_stop);
                if (transmit_enabled) {
                    flex1500_tx_control_arm_physical_ptt(&tx_control);
                }
                flex1500_tune_control_init(&tune_control, transmit_enabled,
                                           &tx_control);
                flex1500_network_tx_init(&network_tx, transmit_enabled,
                                         &tx_control);
                tune_control.diagnostics = saved_tx_diagnostics;
                microphone_ptt_known = false;
                recovered = true;
                printf("[recovery] receive restored (gain=%d dB%s)\n",
                       restore_gain, restore_frequency_known ? ", frequency restored" : "");
                fflush(stdout);
                break;
            }
            if (!recovered) break;
            continue;
        }

        flex1500_physical_inputs inputs;
        if (flex1500_usb_rx_physical_inputs(receiver, &inputs) &&
            (!logged_inputs_known ||
             !flex1500_physical_inputs_equal(&inputs, &logged_inputs))) {
            bool microphone_edge = !microphone_ptt_known ||
                inputs.mic_ptt != observed_microphone_ptt;
            printf("[inputs] raw=0x%02x mic_ptt=%s flexwire_ptt=%s dash=%s dot=%s\n",
                   inputs.raw_status,
                   inputs.mic_ptt ? "pressed" : "released",
                   inputs.flexwire_ptt ? "pressed" : "released",
                   inputs.dash ? "pressed" : "released",
                   inputs.dot ? "pressed" : "released");
            fflush(stdout);
            logged_inputs = inputs;
            logged_inputs_known = true;

            if (transmit_enabled && microphone_edge) {
                uint32_t ptt_frequency = 0;
                bool frequency_known = flex1500_usb_rx_frequency(
                    receiver, &ptt_frequency);
                flex1500_tx_mode physical_mode;
                const char *physical_mode_name = NULL;
                bool frequency_allowed = frequency_known &&
                    resolve_physical_tx_mode(
                        &tx_context, ptt_frequency, &physical_mode,
                        &physical_mode_name);
                flex1500_tx_control_result ptt_result;
                if (inputs.mic_ptt && tx_control.physical_ptt_armed &&
                    (!frequency_known || !frequency_allowed)) {
                    ++tune_control.diagnostics.rejected_ownership_requests;
                    fprintf(stderr,
                            "[tx] physical PTT rejected: TX requires AM/USB/LSB and a known frequency inside the configured amateur voice allocations (mode=%s frequency=%s)\n",
                            physical_mode_name != NULL
                                ? physical_mode_name : api.rx_mode,
                            frequency_known ? "known" : "unset");
                } else {
                    ptt_result = flex1500_tx_control_physical_ptt(
                        &tx_control, inputs.mic_ptt, monotonic_ms());
                    flex1500_tune_control_reconcile(&tune_control, false);
                    if (ptt_result == FLEX1500_TX_CONTROL_BUSY ||
                        ptt_result == FLEX1500_TX_CONTROL_INHIBITED) {
                        ++tune_control.diagnostics.rejected_ownership_requests;
                    } else if (ptt_result ==
                               FLEX1500_TX_CONTROL_HARDWARE_ERROR) {
                        flex1500_tune_control_record_cleanup_failure(
                            &tune_control);
                        fprintf(stderr,
                                "[tx] physical PTT transition failed: %s\n",
                                flex1500_usb_rx_last_error(receiver));
                    } else if (inputs.mic_ptt &&
                               tx_control.owner ==
                                   FLEX1500_TX_OWNER_PHYSICAL_MIC) {
                        printf("[tx] physical microphone keyed at %u Hz %s, drive=%u%%, mic_gain=%u dB\n",
                               tx_context.frequency_hz, tx_context.mode,
                               tx_context.drive_percent,
                               tx_context.microphone_gain_db);
                    } else if (!inputs.mic_ptt) {
                        printf("[tx] physical microphone released; RX restored\n");
                    }
                    fflush(stdout);
                }
            }
            if (microphone_edge) {
                observed_microphone_ptt = inputs.mic_ptt;
                microphone_ptt_known = true;
            }
        }

        if (http_client.fd < 0) {
            int client = accept(listener, NULL, NULL);
            if (client >= 0) {
                if (fcntl(client, F_SETFL,
                          fcntl(client, F_GETFL) | O_NONBLOCK) < 0) {
                    close(client);
                } else {
                    http_client.fd = client;
                    http_client.length = 0;
                    http_client.request[0] = '\0';
                }
            }
        }

        if (rtl_listener >= 0 && iq.rtl_fd < 0) {
            int client = accept(rtl_listener, NULL, NULL);
            if (client >= 0) {
                uint8_t header[FLEX1500_RTL_TCP_HEADER_SIZE];
                flex1500_rtl_tcp_header(header);
                if (send_all(client, (const char *)header, sizeof(header)) != 0 ||
                    fcntl(client, F_SETFL,
                          fcntl(client, F_GETFL) | O_NONBLOCK) < 0) {
                    close(client);
                } else {
                    iq.rtl_fd = client;
                    flex1500_rtl_tcp_resampler_reset(
                        &iq.rtl_resampler, FLEX1500_RTL_TCP_INPUT_RATE);
                    iq.rtl_pending_length = 0;
                    iq.rtl_pending_offset = 0;
                    memset(&rtl_parser, 0, sizeof(rtl_parser));
                    printf("[rtl_tcp] RX client connected\n");
                    fflush(stdout);
                }
            }
        }

        if (iq.rtl_fd >= 0) {
            uint8_t commands[256];
            ssize_t received = recv(iq.rtl_fd, commands, sizeof(commands), 0);
            if (received > 0) {
                for (ssize_t i = 0; i < received; ++i) {
                    flex1500_rtl_tcp_command command;
                    if (!flex1500_rtl_tcp_parse_byte(
                            &rtl_parser, commands[i], &command)) continue;
                    if (command.id == FLEX1500_RTL_TCP_SET_FREQUENCY) {
                        uint32_t filter = 0;
                        if (!rx_tuning_enabled) {
                            fprintf(stderr,
                                    "[rtl_tcp] frequency %u rejected: RX tuning disabled\n",
                                    command.parameter);
                        } else if (tx_control.owner !=
                                   FLEX1500_TX_OWNER_NONE) {
                            fprintf(stderr,
                                    "[rtl_tcp] frequency %u rejected: transmitter active\n",
                                    command.parameter);
                        } else if (station_owner.held) {
                            fprintf(stderr,
                                    "[rtl_tcp] frequency %u rejected: station controls owned by another client\n",
                                    command.parameter);
                        } else if (api_tune_rx(receiver, command.parameter,
                                               &filter) == 0) {
                            printf("[rtl_tcp] tuned RX to %u Hz; RX filter %u selected\n",
                                   command.parameter, filter);
                            fflush(stdout);
                        } else {
                            fprintf(stderr,
                                    "[rtl_tcp] frequency %u rejected by radio policy\n",
                                    command.parameter);
                        }
                    } else if (command.id ==
                               FLEX1500_RTL_TCP_SET_SAMPLE_RATE) {
                        if (command.parameter >= 48000 &&
                            command.parameter <= 3072000) {
                            flex1500_rtl_tcp_resampler_reset(
                                &iq.rtl_resampler, command.parameter);
                            iq.rtl_pending_length = 0;
                            iq.rtl_pending_offset = 0;
                            iq.rtl_source_complete = false;
                            printf("[rtl_tcp] output rate set to %u sample/s using filtered compatibility resampling; RF bandwidth remains 48000 Hz\n",
                                   command.parameter);
                            fflush(stdout);
                        } else {
                            fprintf(stderr,
                                    "[rtl_tcp] sample rate %u rejected; supported range is 48000 through 3072000\n",
                                    command.parameter);
                        }
                    }
                }
            } else if (received == 0 ||
                       (received < 0 && errno != EAGAIN &&
                        errno != EWOULDBLOCK)) {
                close(iq.rtl_fd);
                iq.rtl_fd = -1;
                iq.rtl_pending_length = 0;
                iq.rtl_pending_offset = 0;
                printf("[rtl_tcp] RX client disconnected\n");
                fflush(stdout);
            }
        }

        if (http_client.fd >= 0) {
            ssize_t received = recv(
                http_client.fd, http_client.request + http_client.length,
                sizeof(http_client.request) - 1 - http_client.length, 0);
            if (received > 0) {
                http_client.length += (size_t)received;
                http_client.request[http_client.length] = '\0';
            }
            bool disconnected = received == 0 ||
                (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK);
            bool overflow = http_client.length == sizeof(http_client.request) - 1 &&
                !flex1500_http_request_complete(http_client.request,
                                                http_client.length);
            bool complete = flex1500_http_request_complete(
                http_client.request, http_client.length);
            if (complete) {
                api.request_now_ms = monotonic_ms();
                flex1500_service_status status = live_service_status(
                    receiver, &ring, &publisher, &tune_control,
                    recovery_attempts,
                    recovery_successes);
                flex1500_radio_info radio = live_radio_info(receiver,
                                                            transmit_enabled);
                char response[4096];
                size_t response_length = 0;
                uint32_t tuned_frequency = 0;
                uint32_t tuned_filter = 0;
                bool tune_was_active = tune_control.active;
                flex1500_api_action action = flex1500_api_dispatch(
                    &api, http_client.request, &status, &radio, response,
                    sizeof(response), &response_length, &tuned_frequency,
                    &tuned_filter);
                log_tx_api_result(http_client.request, response,
                                  response_length, &network_tx);
                if (!network_tx.reserved && tx_client >= 0) {
                    close(tx_client);
                    tx_client = -1;
                    tx_context.network_prebuffer_bytes = 0;
                    tx_context.network_tail_bytes = 0;
                }
                if (!tune_was_active && tune_control.active) {
                    printf("[tune] carrier started at %u Hz; nominal 5 W; lease watchdog active\n",
                           radio.frequency_hz);
                    fflush(stdout);
                } else if (tune_was_active && !tune_control.active) {
                    printf("[tune] carrier stopped by API; RX restored\n");
                    fflush(stdout);
                } else if (strncmp(http_client.request,
                                   "PUT /v1/radio/tune/", 19) == 0 &&
                           (response_length < 12 ||
                            (strncmp(response + 9, "200", 3) != 0 &&
                             strncmp(response + 9, "201", 3) != 0))) {
                    fprintf(stderr, "[tune] API request rejected -> %.3s\n",
                            response_length >= 12 ? response + 9 : "---");
                    fflush(stderr);
                }
                if (action == FLEX1500_API_TUNED_RX) {
                    printf("[radio] tuned RX to %u Hz; RX filter %u selected\n",
                           tuned_frequency, tuned_filter);
                    send_all(http_client.fd, response, response_length);
                    fflush(stdout);
                } else if (action == FLEX1500_API_RX_TUNE_FAILED) {
                    send_all(http_client.fd, response, response_length);
                    fprintf(stderr, "[error] RX tune failed: %s\n",
                            flex1500_usb_rx_last_error(receiver));
                } else if (action == FLEX1500_API_OPEN_IQ_STREAM) {
                static const char stream_header[] =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/octet-stream\r\n"
                    "Connection: close\r\n"
                    "Cache-Control: no-store\r\n\r\n";
                size_t slot = MAX_IQ_CLIENTS;
                for (size_t i = 0; i < MAX_IQ_CLIENTS; ++i)
                    if (iq.fd[i] < 0) { slot = i; break; }
                if (slot == MAX_IQ_CLIENTS) {
                    static const char full[] =
                        "HTTP/1.1 503 Service Unavailable\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: 28\r\nConnection: close\r\n\r\n"
                        "{\"error\":\"iq_clients_full\"}\n";
                    (void)send_all(http_client.fd, full, sizeof(full) - 1);
                } else if (send_all(http_client.fd, stream_header,
                                    sizeof(stream_header) - 1) == 0) {
                    iq.fd[slot] = http_client.fd;
                    iq.pending_offset[slot] = 0;
                    http_client.fd = -1;
                    printf("[stream] IQ client connected in slot %zu\n", slot);
                    fflush(stdout);
                }
                } else if (action == FLEX1500_API_OPEN_TX_STREAM) {
                    static const char tx_header[] =
                        "HTTP/1.1 200 Connection Established\r\n"
                        "Content-Type: application/octet-stream\r\n"
                        "Cache-Control: no-store\r\n\r\n";
                    if (tx_client >= 0) close(tx_client);
                    tx_context.network_prebuffer_bytes = 0;
                    if (send_all(http_client.fd, tx_header,
                                 sizeof(tx_header) - 1) == 0) {
                        tx_client = http_client.fd;
                        http_client.fd = -1;
                        printf("[tx] network %s connection established\n",
                               network_tx.profile.source ==
                                       FLEX1500_NETWORK_TX_IQ
                                   ? "raw-IQ" : "PCM-audio");
                    }
                } else if (action == FLEX1500_API_PUSH_TX_AUDIO) {
                    const char *body = strstr(http_client.request, "\r\n\r\n");
                    size_t bytes = body != NULL
                        ? http_client.length - (size_t)(body + 4 - http_client.request)
                        : 0;
                    body = body != NULL ? body + 4 : NULL;
                    if (body != NULL && bytes != 0) {
                        if (!network_tx.keyed) {
                            size_t room = sizeof(tx_context.network_prebuffer) -
                                tx_context.network_prebuffer_bytes;
                            if (bytes > room) bytes = room;
                            memcpy(tx_context.network_prebuffer +
                                   tx_context.network_prebuffer_bytes,
                                   body, bytes);
                            tx_context.network_prebuffer_bytes += bytes;
                        } else {
                            (void)flex1500_usb_rx_network_tx_push(
                                receiver, (const uint8_t *)body, bytes, false);
                        }
                    }
                    send_all(http_client.fd, response, response_length);
                } else if (action == FLEX1500_API_SERVE_TEST_PAGE) {
                    send_web_ui(http_client.fd);
                    printf("[http] served receive test page\n");
                    fflush(stdout);
                } else if (response_length > 0) {
                    send_all(http_client.fd, response, response_length);
                    if (strncmp(http_client.request,
                                "PUT /v1/radio/mode/", 19) == 0 &&
                        strstr(response, "200 OK") != NULL) {
                        printf("[dsp] receive mode changed to %s; bandwidth %u Hz (host demodulation)\n",
                               api.rx_mode, flex1500_api_rx_bandwidth(&api));
                        fflush(stdout);
                    }
                }
            }
            if (http_client.fd >= 0 &&
                (complete || disconnected || overflow)) {
                close(http_client.fd);
                http_client.fd = -1;
                http_client.length = 0;
            }
        }

        if (tx_client >= 0) {
            uint8_t data[8192 + 4];
            bool raw_iq = network_tx.profile.source == FLEX1500_NETWORK_TX_IQ;
            size_t frame_bytes = raw_iq ? 4 : 2;
            size_t available_frames = network_tx.keyed
                ? flex1500_usb_rx_network_tx_available(receiver, raw_iq)
                : (network_tx.buffered_frames <
                       FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES
                    ? FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES -
                          network_tx.buffered_frames
                    : 0);
            size_t receive_limit = available_frames * frame_bytes;
            receive_limit = receive_limit > tx_context.network_tail_bytes
                ? receive_limit - tx_context.network_tail_bytes : 0;
            if (receive_limit > sizeof(data) - tx_context.network_tail_bytes) {
                receive_limit = sizeof(data) - tx_context.network_tail_bytes;
            }
            memcpy(data, tx_context.network_tail,
                   tx_context.network_tail_bytes);
            ssize_t received = receive_limit != 0
                ? recv(tx_client, data + tx_context.network_tail_bytes,
                       receive_limit, 0)
                : -1;
            if (received > 0) {
                size_t total = (size_t)received + tx_context.network_tail_bytes;
                size_t bytes = total - total % frame_bytes;
                tx_context.network_tail_bytes = total - bytes;
                if (tx_context.network_tail_bytes != 0) {
                    memcpy(tx_context.network_tail, data + bytes,
                           tx_context.network_tail_bytes);
                }
                if (!network_tx.keyed) {
                    size_t room = sizeof(tx_context.network_prebuffer) -
                                  tx_context.network_prebuffer_bytes;
                    if (bytes > room) bytes = room;
                    memcpy(tx_context.network_prebuffer +
                           tx_context.network_prebuffer_bytes, data, bytes);
                    tx_context.network_prebuffer_bytes += bytes;
                } else {
                    (void)flex1500_usb_rx_network_tx_push(receiver, data,
                                                          bytes, raw_iq);
                }
                (void)flex1500_network_tx_record_data(
                    &network_tx, network_tx.lease, bytes / frame_bytes,
                    monotonic_ms());
            } else if (receive_limit != 0 && (received == 0 ||
                       (received < 0 && errno != EAGAIN &&
                        errno != EWOULDBLOCK))) {
                close(tx_client);
                tx_client = -1;
                (void)flex1500_network_tx_disconnect_stream(&network_tx);
                tx_context.network_prebuffer_bytes = 0;
                tx_context.network_tail_bytes = 0;
                printf("[tx] network %s connection closed; unkey requested\n",
                       network_tx.profile.source == FLEX1500_NETWORK_TX_IQ
                           ? "raw-IQ" : "PCM-audio");
            }
        }

        bool have_iq_client = false;
        for (size_t i = 0; i < MAX_IQ_CLIENTS; ++i)
            have_iq_client = have_iq_client || iq.fd[i] >= 0;
        have_iq_client = have_iq_client || iq.rtl_fd >= 0;
        if (have_iq_client) {
            for (;;) {
                flex1500_publish_result publish_result =
                    flex1500_iq_publisher_pump(&publisher, write_iq_clients,
                                               &iq);
                if (publish_result == FLEX1500_PUBLISH_EMPTY ||
                    publish_result == FLEX1500_PUBLISH_WOULD_BLOCK) break;
                if (publish_result == FLEX1500_PUBLISH_DISCONNECTED ||
                    publish_result == FLEX1500_PUBLISH_ERROR) {
                    flex1500_iq_publisher_disconnect(&publisher);
                    printf("[stream] IQ client disconnected\n");
                    fflush(stdout);
                    break;
                }
            }
        }
    }

    for (size_t i = 0; i < MAX_IQ_CLIENTS; ++i)
        if (iq.fd[i] >= 0) close(iq.fd[i]);
    if (iq.rtl_fd >= 0) close(iq.rtl_fd);
    if (tx_client >= 0) close(tx_client);
    if (http_client.fd >= 0) close(http_client.fd);
    if (!server_stop_requested) {
        fprintf(stderr, "Live RX stopped: %s\n",
                flex1500_usb_rx_last_error(receiver));
    }
    daemon_tx_shutdown(&tx_control, &tune_control);
    if (transmit_enabled) {
        int cleanup_result =
            flex1500_usb_rx_disable_transmit_preparation(receiver);
        if (cleanup_result == 0) {
            printf("[shutdown] TX preparation cleanup confirmed "
                   "(PA filter 0, amplifier disabled)\n");
        } else {
            flex1500_tune_control_record_cleanup_failure(&tune_control);
            fprintf(stderr,
                    "[shutdown] TX preparation cleanup failed: %s\n",
                    flex1500_usb_rx_last_error(receiver));
        }
    }
    const flex1500_tx_diagnostics *tx_diagnostics =
        flex1500_tune_control_diagnostics(&tune_control);
    printf("[tx] diagnostics: starts=%llu stops=%llu underruns=%llu "
           "clipped_frames=%llu limited_frames=%llu "
           "dropped_microphone_frames=%llu "
           "peak_queued_frames=%llu stop_requested_frames=%llu "
           "graceful_drained_frames=%llu graceful_discarded_frames=%llu "
           "graceful_drain_ms=%llu "
           "rejected_ownership=%llu watchdog_stops=%llu "
           "cleanup_failures=%llu\n",
           (unsigned long long)tx_diagnostics->starts,
           (unsigned long long)tx_diagnostics->stops,
           (unsigned long long)tx_diagnostics->underruns,
           (unsigned long long)tx_diagnostics->clipped_frames,
           (unsigned long long)tx_diagnostics->limited_frames,
           (unsigned long long)tx_diagnostics->dropped_microphone_frames,
           (unsigned long long)tx_diagnostics->peak_queued_frames,
           (unsigned long long)tx_diagnostics->stop_requested_frames,
           (unsigned long long)tx_diagnostics->graceful_drained_frames,
           (unsigned long long)tx_diagnostics->graceful_discarded_frames,
           (unsigned long long)tx_diagnostics->graceful_drain_ms,
           (unsigned long long)tx_diagnostics->rejected_ownership_requests,
           (unsigned long long)tx_diagnostics->watchdog_stops,
           (unsigned long long)tx_diagnostics->cleanup_failures);
    flex1500_usb_rx_destroy(receiver);
    flex1500_iq_ring_destroy(&ring);
    close(listener);
    if (rtl_listener >= 0) close(rtl_listener);
    printf("[shutdown] receive daemon stopped\n");
    fflush(stdout);
    return server_stop_requested ? EXIT_SUCCESS : EXIT_FAILURE;
}

static int serve_live_rx(const char *port_text, bool rx_tuning_enabled,
                         bool test_page_enabled, bool transmit_enabled)
{
    return serve_live_rx_at("0.0.0.0", port_text, rx_tuning_enabled,
                            test_page_enabled, transmit_enabled, false,
                            "0.0.0.0", "1234");
}

static int analyze_file(const char *path)
{
    FILE *input = fopen(path, "rb");
    uint8_t bytes[INPUT_BYTES];
    flex1500_iq_sample processed[INPUT_FRAMES];
    flex1500_iq_sample consumed[INPUT_FRAMES];
    flex1500_iq_stats stats;
    flex1500_dc_blocker blocker;
    flex1500_iq_ring ring;
    uint64_t consumed_frames = 0;
    double processed_sum_i = 0.0;
    double processed_sum_q = 0.0;

    if (input == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return EXIT_FAILURE;
    }
    if (!flex1500_iq_ring_init(&ring, RING_CAPACITY)) {
        fputs("Unable to allocate IQ ring buffer.\n", stderr);
        fclose(input);
        return EXIT_FAILURE;
    }

    flex1500_iq_stats_reset(&stats);
    flex1500_dc_blocker_init(&blocker, 0.001f);
    for (;;) {
        size_t bytes_read = fread(bytes, 1, sizeof(bytes), input);
        if (bytes_read == 0) break;
        size_t produced = flex1500_process_iq16le(
            bytes, bytes_read, processed, INPUT_FRAMES, &stats, &blocker);
        flex1500_iq_ring_push(&ring, processed, produced);
        size_t popped =
            flex1500_iq_ring_pop(&ring, consumed, INPUT_FRAMES);
        for (size_t index = 0; index < popped; ++index) {
            processed_sum_i += consumed[index].i;
            processed_sum_q += consumed[index].q;
        }
        consumed_frames += popped;
        if (bytes_read < sizeof(bytes)) break;
    }

    if (ferror(input)) {
        fprintf(stderr, "Failed while reading %s.\n", path);
        flex1500_iq_ring_destroy(&ring);
        fclose(input);
        return EXIT_FAILURE;
    }

    printf("{\n");
    printf("  \"service\": \"flex1500d\",\n");
    printf("  \"state\": \"offline_analysis\",\n");
    printf("  \"source\": \"%s\",\n", path);
    printf("  \"sample_rate\": 48000,\n");
    printf("  \"sample_format\": \"iq_s16le\",\n");
    printf("  \"frames\": %llu,\n", (unsigned long long)stats.frames);
    printf("  \"sentinel_frames\": %llu,\n",
           (unsigned long long)stats.sentinel_frames);
    printf("  \"raw_i_min\": %d,\n", stats.min_i);
    printf("  \"raw_i_max\": %d,\n", stats.max_i);
    printf("  \"raw_q_min\": %d,\n", stats.min_q);
    printf("  \"raw_q_max\": %d,\n", stats.max_q);
    printf("  \"raw_i_mean\": %.6f,\n", stats.mean_i);
    printf("  \"raw_q_mean\": %.6f,\n", stats.mean_q);
    printf("  \"dc_estimate_i\": %.6f,\n", blocker.estimate_i);
    printf("  \"dc_estimate_q\": %.6f,\n", blocker.estimate_q);
    printf("  \"processed_i_mean\": %.6f,\n",
           consumed_frames ? processed_sum_i / (double)consumed_frames : 0.0);
    printf("  \"processed_q_mean\": %.6f,\n",
           consumed_frames ? processed_sum_q / (double)consumed_frames : 0.0);
    printf("  \"ring_capacity_frames\": %zu,\n", ring.capacity);
    printf("  \"ring_dropped_frames\": %llu\n",
           (unsigned long long)ring.dropped);
    printf("}\n");

    flex1500_iq_ring_destroy(&ring);
    fclose(input);
    return EXIT_SUCCESS;
}

static flex1500_publish_result write_file(void *context, const uint8_t *bytes,
                                         size_t length, size_t *written)
{
    FILE *output = context;
    *written = fwrite(bytes, 1, length, output);
    if (*written > 0) return FLEX1500_PUBLISH_OK;
    return ferror(output) ? FLEX1500_PUBLISH_ERROR : FLEX1500_PUBLISH_WOULD_BLOCK;
}

static int drain_publisher(flex1500_iq_publisher *publisher, FILE *output)
{
    for (;;) {
        flex1500_publish_result result =
            flex1500_iq_publisher_pump(publisher, write_file, output);
        if (result == FLEX1500_PUBLISH_EMPTY) return 0;
        if (result != FLEX1500_PUBLISH_OK) return -1;
    }
}

static int frame_capture(const char *input_path, const char *output_path)
{
    FILE *input = fopen(input_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", input_path, strerror(errno));
        return EXIT_FAILURE;
    }
    FILE *output = fopen(output_path, "wbx");
    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", output_path, strerror(errno));
        fclose(input);
        return EXIT_FAILURE;
    }

    flex1500_iq_ring ring;
    flex1500_iq_publisher publisher;
    flex1500_iq_stats stats;
    flex1500_dc_blocker blocker;
    uint8_t bytes[INPUT_BYTES];
    flex1500_iq_sample processed[INPUT_FRAMES];
    int result = EXIT_FAILURE;

    if (!flex1500_iq_ring_init(&ring, RING_CAPACITY) ||
        !flex1500_iq_publisher_init(&publisher, &ring, 256)) {
        fputs("Unable to initialize offline publishing pipeline.\n", stderr);
        if (ring.samples != NULL) flex1500_iq_ring_destroy(&ring);
        goto done;
    }
    flex1500_iq_stats_reset(&stats);
    flex1500_dc_blocker_init(&blocker, 0.001f);

    for (;;) {
        size_t bytes_read = fread(bytes, 1, sizeof(bytes), input);
        if (bytes_read == 0) break;
        size_t produced = flex1500_process_iq16le(
            bytes, bytes_read, processed, INPUT_FRAMES, &stats, &blocker);
        if (flex1500_iq_ring_push(&ring, processed, produced) != produced ||
            drain_publisher(&publisher, output) != 0) {
            fputs("Failed while publishing framed IQ data.\n", stderr);
            goto destroy_ring;
        }
        if (bytes_read < sizeof(bytes)) break;
    }
    if (ferror(input) || fflush(output) != 0) {
        fputs("File I/O failed during offline capture framing.\n", stderr);
        goto destroy_ring;
    }

    printf("Framed %llu complex samples into %llu network frames: %s\n",
           (unsigned long long)publisher.stats.samples_sent,
           (unsigned long long)publisher.stats.frames_sent, output_path);
    result = EXIT_SUCCESS;

destroy_ring:
    flex1500_iq_ring_destroy(&ring);
done:
    if (fclose(output) != 0) result = EXIT_FAILURE;
    fclose(input);
    return result;
}

static void store_le16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
}

static void store_le32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8);
    output[2] = (uint8_t)(value >> 16);
    output[3] = (uint8_t)(value >> 24);
}

static bool write_wav_header(FILE *output, uint32_t sample_count)
{
    uint8_t header[44] = {0};
    memcpy(&header[0], "RIFF", 4);
    store_le32(&header[4], 36 + sample_count * 2);
    memcpy(&header[8], "WAVEfmt ", 8);
    store_le32(&header[16], 16);
    store_le16(&header[20], 1);
    store_le16(&header[22], 1);
    store_le32(&header[24], 48000);
    store_le32(&header[28], 48000 * 2);
    store_le16(&header[32], 2);
    store_le16(&header[34], 16);
    memcpy(&header[36], "data", 4);
    store_le32(&header[40], sample_count * 2);
    return fwrite(header, 1, sizeof(header), output) == sizeof(header);
}

static int demod_capture(const char *input_path, const char *mode_text,
                         const char *output_path, const char *offset_text)
{
    flex1500_demod_mode mode;
    if (!flex1500_parse_demod_mode(mode_text, &mode)) {
        fprintf(stderr, "Unsupported mode: %s (use am, fm, usb, or lsb)\n",
                mode_text);
        return EXIT_FAILURE;
    }
    float offset = 0.0f;
    if (offset_text != NULL) {
        char *end = NULL;
        offset = strtof(offset_text, &end);
        if (end == offset_text || *end != '\0' || offset <= -24000.0f ||
            offset >= 24000.0f) {
            fprintf(stderr, "Invalid tuning offset: %s\n", offset_text);
            return EXIT_FAILURE;
        }
    }

    FILE *input = fopen(input_path, "rb");
    if (input == NULL) {
        fprintf(stderr, "Cannot open %s: %s\n", input_path, strerror(errno));
        return EXIT_FAILURE;
    }
    FILE *output = fopen(output_path, "wbx");
    if (output == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", output_path, strerror(errno));
        fclose(input);
        return EXIT_FAILURE;
    }

    flex1500_dsp dsp;
    flex1500_dsp_config config = {
        .mode = mode,
        .sample_rate = 48000.0f,
        .tuning_offset_hz = offset,
        .agc_enabled = true,
    };
    flex1500_iq_stats stats;
    flex1500_dc_blocker blocker;
    uint8_t bytes[INPUT_BYTES];
    flex1500_iq_sample iq[INPUT_FRAMES];
    float audio[INPUT_FRAMES];
    uint8_t pcm[INPUT_FRAMES * 2];
    uint64_t samples_written = 0;
    int result = EXIT_FAILURE;

    if (!flex1500_dsp_init(&dsp, &config) || !write_wav_header(output, 0)) {
        fputs("Unable to initialize WAV demodulation.\n", stderr);
        goto done;
    }
    flex1500_iq_stats_reset(&stats);
    flex1500_dc_blocker_init(&blocker, 0.001f);

    for (;;) {
        size_t bytes_read = fread(bytes, 1, sizeof(bytes), input);
        if (bytes_read == 0) break;
        size_t frames = flex1500_process_iq16le(
            bytes, bytes_read, iq, INPUT_FRAMES, &stats, &blocker);
        size_t produced = flex1500_dsp_process(
            &dsp, iq, frames, audio, INPUT_FRAMES);
        for (size_t index = 0; index < produced; ++index) {
            float limited = fmaxf(-1.0f, fminf(1.0f, audio[index]));
            int16_t sample = (int16_t)lrintf(limited * 32767.0f);
            store_le16(&pcm[index * 2], (uint16_t)sample);
        }
        if (fwrite(pcm, 2, produced, output) != produced) goto done;
        samples_written += produced;
        if (samples_written > UINT32_MAX / 2 || bytes_read < sizeof(bytes)) break;
    }
    if (ferror(input) || samples_written > UINT32_MAX / 2 ||
        fseek(output, 0, SEEK_SET) != 0 ||
        !write_wav_header(output, (uint32_t)samples_written) ||
        fflush(output) != 0) {
        fputs("File I/O failed during WAV demodulation.\n", stderr);
        goto done;
    }
    printf("Demodulated %llu samples as %s at offset %.1f Hz: %s\n",
           (unsigned long long)samples_written,
           flex1500_demod_mode_name(mode), offset, output_path);
    result = EXIT_SUCCESS;

done:
    if (fclose(output) != 0) result = EXIT_FAILURE;
    fclose(input);
    return result;
}

static bool is_configured_option(const char *argument)
{
    return strcmp(argument, "--config") == 0 ||
           strcmp(argument, "--check-config") == 0 ||
           strcmp(argument, "--print-effective-config") == 0 ||
           strcmp(argument, "--daemon") == 0 ||
           strcmp(argument, "--no-daemon") == 0 ||
           strcmp(argument, "--radio-mode") == 0 ||
           strcmp(argument, "--http-bind") == 0 ||
           strcmp(argument, "--http-port") == 0 ||
           strcmp(argument, "--enable-test-page") == 0 ||
           strcmp(argument, "--disable-test-page") == 0 ||
           strcmp(argument, "--enable-rtl-tcp") == 0 ||
           strcmp(argument, "--disable-rtl-tcp") == 0 ||
           strcmp(argument, "--rtl-tcp-bind") == 0 ||
           strcmp(argument, "--rtl-tcp-port") == 0;
}

static bool configured_invocation(int argc, char **argv)
{
    if (argc == 1) return true;
    if (strcmp(argv[1], "--status") == 0 ||
        strcmp(argv[1], "--help") == 0 ||
        strcmp(argv[1], "--analyze") == 0 ||
        strcmp(argv[1], "--frame-capture") == 0 ||
        strcmp(argv[1], "--demod") == 0 ||
        strcmp(argv[1], "--serve-offline") == 0 ||
        strcmp(argv[1], "--serve-live-rx") == 0) return false;
    for (int index = 1; index < argc; ++index) {
        if (is_configured_option(argv[index])) return true;
    }
    return false;
}

static bool valid_port_text(const char *text, uint16_t *port)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value == 0 ||
        value > 65535) return false;
    *port = (uint16_t)value;
    return true;
}

static int run_configured(int argc, char **argv)
{
    const char *path = FLEX1500_DEFAULT_CONFIG_PATH;
    bool explicit_path = false;
    bool check_only = false;
    bool print_config = false;
    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--config") == 0) {
            if (++index >= argc) {
                fputs("--config requires a path\n", stderr);
                return EXIT_FAILURE;
            }
            path = argv[index];
            explicit_path = true;
        }
    }

    flex1500_config config;
    flex1500_config_defaults(&config);
    char error[512];
    if (!flex1500_config_load(&config, path, !explicit_path, error,
                              sizeof(error))) {
        fprintf(stderr, "Configuration error: %s\n", error);
        return EXIT_FAILURE;
    }

    for (int index = 1; index < argc; ++index) {
        const char *option = argv[index];
        if (strcmp(option, "--config") == 0) {
            ++index;
        } else if (strcmp(option, "--check-config") == 0) {
            check_only = true;
        } else if (strcmp(option, "--print-effective-config") == 0) {
            print_config = true;
        } else if (strcmp(option, "--daemon") == 0) {
            config.daemon_enabled = true;
        } else if (strcmp(option, "--no-daemon") == 0) {
            config.daemon_enabled = false;
        } else if (strcmp(option, "--enable-test-page") == 0) {
            config.test_page_enabled = true;
        } else if (strcmp(option, "--disable-test-page") == 0) {
            config.test_page_enabled = false;
        } else if (strcmp(option, "--enable-rtl-tcp") == 0) {
            config.rtl_tcp_enabled = true;
        } else if (strcmp(option, "--disable-rtl-tcp") == 0) {
            config.rtl_tcp_enabled = false;
        } else if (strcmp(option, "--radio-mode") == 0) {
            if (++index >= argc ||
                !flex1500_parse_radio_mode(argv[index], &config.radio_mode)) {
                fputs("--radio-mode requires offline, receive, rx-tuning, or transmit\n",
                      stderr);
                return EXIT_FAILURE;
            }
        } else if (strcmp(option, "--http-bind") == 0) {
            if (++index >= argc || strlen(argv[index]) == 0 ||
                strlen(argv[index]) >= sizeof(config.http_bind)) {
                fputs("--http-bind requires a numeric IPv4 address\n", stderr);
                return EXIT_FAILURE;
            }
            strcpy(config.http_bind, argv[index]);
        } else if (strcmp(option, "--http-port") == 0) {
            if (++index >= argc ||
                !valid_port_text(argv[index], &config.http_port)) {
                fputs("--http-port requires a value from 1 through 65535\n",
                      stderr);
                return EXIT_FAILURE;
            }
        } else if (strcmp(option, "--rtl-tcp-bind") == 0) {
            if (++index >= argc || strlen(argv[index]) == 0 ||
                strlen(argv[index]) >= sizeof(config.rtl_tcp_bind)) {
                fputs("--rtl-tcp-bind requires a numeric IPv4 address\n",
                      stderr);
                return EXIT_FAILURE;
            }
            strcpy(config.rtl_tcp_bind, argv[index]);
        } else if (strcmp(option, "--rtl-tcp-port") == 0) {
            if (++index >= argc ||
                !valid_port_text(argv[index], &config.rtl_tcp_port)) {
                fputs("--rtl-tcp-port requires a value from 1 through 65535\n",
                      stderr);
                return EXIT_FAILURE;
            }
        } else {
            fprintf(stderr, "Unknown configured-daemon option: %s\n", option);
            return EXIT_FAILURE;
        }
    }

    struct in_addr parsed_address;
    if (inet_pton(AF_INET, config.http_bind, &parsed_address) != 1) {
        fprintf(stderr, "Invalid IPv4 bind address: %s\n", config.http_bind);
        return EXIT_FAILURE;
    }
    if (inet_pton(AF_INET, config.rtl_tcp_bind, &parsed_address) != 1) {
        fprintf(stderr, "Invalid rtl_tcp IPv4 bind address: %s\n",
                config.rtl_tcp_bind);
        return EXIT_FAILURE;
    }
    if (print_config) flex1500_config_print(&config, path);
    if (check_only) {
        printf("Configuration valid: %s\n", path);
        return EXIT_SUCCESS;
    }
    if (print_config || !config.daemon_enabled) {
        if (!print_config) print_idle_status();
        return EXIT_SUCCESS;
    }

    char port[6];
    char rtl_port[6];
    snprintf(port, sizeof(port), "%u", (unsigned int)config.http_port);
    snprintf(rtl_port, sizeof(rtl_port), "%u",
             (unsigned int)config.rtl_tcp_port);
    printf("[config] loaded %s; effective radio mode=%s, test-page=%s\n",
           path, flex1500_radio_mode_name(config.radio_mode),
           config.test_page_enabled ? "enabled" : "disabled");
    fflush(stdout);
    if (config.radio_mode == FLEX1500_RADIO_DISABLED) {
        return serve_offline_at(config.http_bind, port,
                                config.test_page_enabled);
    }
    return serve_live_rx_at(
        config.http_bind, port,
        config.radio_mode == FLEX1500_RADIO_RX_TUNING ||
            config.radio_mode == FLEX1500_RADIO_TRANSMIT,
        config.test_page_enabled,
        config.radio_mode == FLEX1500_RADIO_TRANSMIT,
        config.rtl_tcp_enabled, config.rtl_tcp_bind, rtl_port);
}

int main(int argc, char **argv)
{
    if (configured_invocation(argc, argv)) return run_configured(argc, argv);
    if (argc == 2 && strcmp(argv[1], "--status") == 0) {
        print_idle_status();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc == 3 && strcmp(argv[1], "--analyze") == 0) {
        return analyze_file(argv[2]);
    }
    if (argc == 3 && strcmp(argv[1], "--serve-offline") == 0) {
        return serve_offline(argv[2], false);
    }
    if (argc == 4 && strcmp(argv[1], "--serve-offline") == 0 &&
        strcmp(argv[3], "--enable-test-page") == 0) {
        return serve_offline(argv[2], true);
    }
    if (argc == 4 && strcmp(argv[1], "--frame-capture") == 0) {
        return frame_capture(argv[2], argv[3]);
    }
    if ((argc == 5 || argc == 6) && strcmp(argv[1], "--demod") == 0) {
        return demod_capture(argv[2], argv[3], argv[4],
                             argc == 6 ? argv[5] : NULL);
    }
    if (argc == 4 && strcmp(argv[1], "--serve-live-rx") == 0 &&
        strcmp(argv[3], "--initialize-radio") == 0) {
        return serve_live_rx(argv[2], false, false, false);
    }
    if (argc == 4 && strcmp(argv[1], "--serve-live-rx") == 0 &&
        strcmp(argv[3], "--initialize-radio-and-enable-rx-tuning") == 0) {
        return serve_live_rx(argv[2], true, false, false);
    }
    if (argc == 5 && strcmp(argv[1], "--serve-live-rx") == 0 &&
        strcmp(argv[3], "--initialize-radio-and-enable-rx-tuning") == 0 &&
        strcmp(argv[4], "--enable-test-page") == 0) {
        return serve_live_rx(argv[2], true, true, false);
    }
    if ((argc == 4 || argc == 5) &&
        strcmp(argv[1], "--serve-live-rx") == 0 &&
        strcmp(argv[3], "--initialize-radio-and-enable-transmit") == 0 &&
        (argc == 4 || strcmp(argv[4], "--enable-test-page") == 0)) {
        return serve_live_rx(argv[2], true, argc == 5, true);
    }
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
