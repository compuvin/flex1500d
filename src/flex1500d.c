// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/api.h"
#include "flex1500/dsp.h"
#include "flex1500/iq.h"
#include "flex1500/network.h"
#include "flex1500/publisher.h"
#include "flex1500/protocol.h"
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
#include <netinet/in.h>
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
    char request[2048];
    size_t length;
} http_client_state;

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
    puts("Default, status, analysis, demod, framing, and offline-server modes");
    puts("do not open the radio. --frame-capture is offline file conversion.");
    puts("--demod is offline and writes 48 kHz mono PCM16 WAV audio.");
    puts("--serve-offline binds only 127.0.0.1.");
    puts("Live RX always sends opcode-1219 INITIALIZE. The separately armed");
    puts("tuning mode may also send RX-frequency and RX-filter commands.");
    puts("Never run either without KB1JDX's explicit permission. Both bind");
    puts("only 127.0.0.1, and neither mode contains a TX/PTT path.");
}

static void print_idle_status(void)
{
    flex1500_service_status status = {
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

static int open_loopback_listener(const char *port_text, unsigned long *port)
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
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };
    if (bind(listener, (struct sockaddr *)&address, sizeof(address)) != 0 ||
        listen(listener, 8) != 0) {
        close(listener);
        return -1;
    }
    return listener;
}

static int serve_offline(const char *port_text, bool test_page_enabled)
{
    unsigned long parsed;
    int listener = open_loopback_listener(port_text, &parsed);
    if (listener < 0) {
        perror("open loopback listener");
        return EXIT_FAILURE;
    }

    server_stop_requested = 0;
    signal(SIGINT, request_server_stop);
    signal(SIGTERM, request_server_stop);
    printf("flex1500d offline API listening on http://127.0.0.1:%lu\n", parsed);
    fflush(stdout);

    flex1500_service_status status = {
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

static flex1500_publish_result write_socket(void *context,
                                            const uint8_t *bytes,
                                            size_t length, size_t *written)
{
    int socket_fd = *(int *)context;
    ssize_t result = send(socket_fd, bytes, length, MSG_NOSIGNAL);
    if (result > 0) {
        *written = (size_t)result;
        return FLEX1500_PUBLISH_OK;
    }
    *written = 0;
    if (result < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        return FLEX1500_PUBLISH_WOULD_BLOCK;
    }
    if (result == 0 || errno == EPIPE || errno == ECONNRESET) {
        return FLEX1500_PUBLISH_DISCONNECTED;
    }
    return FLEX1500_PUBLISH_ERROR;
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

static flex1500_service_status live_service_status(
    const flex1500_usb_rx *receiver, const flex1500_iq_ring *ring,
    const flex1500_iq_publisher *publisher, uint64_t recovery_attempts,
    uint64_t recovery_successes)
{
    const flex1500_usb_rx_counters *usb =
        flex1500_usb_rx_get_counters(receiver);
    const flex1500_iq_stats *iq = flex1500_usb_rx_get_iq_stats(receiver);
    return (flex1500_service_status){
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
        .usb_first_error_ms = usb->first_error_ms,
        .usb_last_error_ms = usb->last_error_ms,
        .first_sentinel_frame = usb->first_sentinel_frame,
        .last_sentinel_frame = usb->last_sentinel_frame,
        .first_sentinel_ms = usb->first_sentinel_ms,
        .last_sentinel_ms = usb->last_sentinel_ms,
        .rx_recovery_attempts = recovery_attempts,
        .rx_recovery_successes = recovery_successes,
    };
}

static flex1500_radio_info live_radio_info(const flex1500_usb_rx *receiver)
{
    flex1500_radio_info radio = RADIO_INFO;
    radio.frequency_known = flex1500_usb_rx_frequency(
        receiver, &radio.frequency_hz);
    radio.rx_filter_known = flex1500_usb_rx_filter(receiver, &radio.rx_filter);
    radio.rx_gain_known = flex1500_usb_rx_gain(receiver, &radio.rx_gain_db);
    return radio;
}

static int serve_live_rx(const char *port_text, bool rx_tuning_enabled,
                         bool test_page_enabled)
{
    unsigned long port;
    int listener = open_loopback_listener(port_text, &port);
    if (listener < 0) {
        perror("open loopback listener");
        return EXIT_FAILURE;
    }
    if (fcntl(listener, F_SETFL, fcntl(listener, F_GETFL) | O_NONBLOCK) < 0) {
        perror("set listener nonblocking");
        close(listener);
        return EXIT_FAILURE;
    }

    flex1500_iq_ring ring;
    if (!flex1500_iq_ring_init(&ring, RING_CAPACITY)) {
        close(listener);
        return EXIT_FAILURE;
    }
    flex1500_usb_rx *receiver = flex1500_usb_rx_create(&ring);
    flex1500_iq_publisher publisher;
    if (receiver == NULL ||
        !flex1500_iq_publisher_init(&publisher, &ring, 256)) {
        flex1500_usb_rx_destroy(receiver);
        flex1500_iq_ring_destroy(&ring);
        close(listener);
        return EXIT_FAILURE;
    }

    int usb_result = flex1500_usb_rx_start(receiver);
    if (usb_result != 0) {
        fprintf(stderr, "Live RX start failed: %s\n",
                flex1500_usb_rx_last_error(receiver));
        flex1500_usb_rx_destroy(receiver);
        flex1500_iq_ring_destroy(&ring);
        close(listener);
        return EXIT_FAILURE;
    }
    usb_result = flex1500_usb_rx_set_gain(receiver, 20);
    if (usb_result != 0) {
        fprintf(stderr, "Live RX gain initialization failed: %s\n",
                flex1500_usb_rx_last_error(receiver));
        flex1500_usb_rx_destroy(receiver);
        flex1500_iq_ring_destroy(&ring);
        close(listener);
        return EXIT_FAILURE;
    }

    server_stop_requested = 0;
    signal(SIGINT, request_server_stop);
    signal(SIGTERM, request_server_stop);
    printf("[startup] FLEX-1500 initialized; receive-only USB stream active\n");
    printf("[startup] receive gain set to +20 dB\n");
    printf("[startup] API listening on http://127.0.0.1:%lu (tuning=%s, test-page=%s)\n",
           port, rx_tuning_enabled ? "enabled" : "disabled",
           test_page_enabled ? "enabled" : "disabled");
    fflush(stdout);
    int iq_client = -1;
    http_client_state http_client = {.fd = -1};
    flex1500_api_controller api;
    flex1500_api_controller_init(&api, rx_tuning_enabled, true,
                                 test_page_enabled);
    api.radio_context = receiver;
    api.tune_rx = api_tune_rx;
    api.set_rx_gain = api_set_rx_gain;
    uint64_t recovery_attempts = 0;
    uint64_t recovery_successes = 0;

    while (!server_stop_requested) {
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
            if (iq_client >= 0) {
                close(iq_client);
                iq_client = -1;
                flex1500_iq_publisher_disconnect(&publisher);
            }
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
                recovered = true;
                printf("[recovery] receive restored (gain=%d dB%s)\n",
                       restore_gain, restore_frequency_known ? ", frequency restored" : "");
                fflush(stdout);
                break;
            }
            if (!recovered) break;
            continue;
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
                flex1500_service_status status = live_service_status(
                    receiver, &ring, &publisher, recovery_attempts,
                    recovery_successes);
                flex1500_radio_info radio = live_radio_info(receiver);
                char response[4096];
                size_t response_length = 0;
                uint32_t tuned_frequency = 0;
                uint32_t tuned_filter = 0;
                flex1500_api_action action = flex1500_api_dispatch(
                    &api, http_client.request, &status, &radio, response,
                    sizeof(response), &response_length, &tuned_frequency,
                    &tuned_filter);
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
                if (iq_client >= 0) {
                    close(iq_client);
                    iq_client = -1;
                    flex1500_iq_publisher_disconnect(&publisher);
                    printf("[stream] previous IQ client replaced\n");
                }
                if (send_all(http_client.fd, stream_header,
                             sizeof(stream_header) - 1) == 0) {
                    iq_client = http_client.fd;
                    http_client.fd = -1;
                    printf("[stream] IQ client connected\n");
                    fflush(stdout);
                }
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

        if (iq_client >= 0) {
            for (;;) {
                flex1500_publish_result publish_result =
                    flex1500_iq_publisher_pump(&publisher, write_socket,
                                               &iq_client);
                if (publish_result == FLEX1500_PUBLISH_EMPTY ||
                    publish_result == FLEX1500_PUBLISH_WOULD_BLOCK) break;
                if (publish_result == FLEX1500_PUBLISH_DISCONNECTED ||
                    publish_result == FLEX1500_PUBLISH_ERROR) {
                    close(iq_client);
                    iq_client = -1;
                    flex1500_iq_publisher_disconnect(&publisher);
                    printf("[stream] IQ client disconnected\n");
                    fflush(stdout);
                    break;
                }
            }
        }
    }

    if (iq_client >= 0) close(iq_client);
    if (http_client.fd >= 0) close(http_client.fd);
    if (!server_stop_requested) {
        fprintf(stderr, "Live RX stopped: %s\n",
                flex1500_usb_rx_last_error(receiver));
    }
    flex1500_usb_rx_destroy(receiver);
    flex1500_iq_ring_destroy(&ring);
    close(listener);
    printf("[shutdown] receive daemon stopped\n");
    fflush(stdout);
    return server_stop_requested ? EXIT_SUCCESS : EXIT_FAILURE;
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

int main(int argc, char **argv)
{
    if (argc == 1 || (argc == 2 && strcmp(argv[1], "--status") == 0)) {
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
        return serve_live_rx(argv[2], false, false);
    }
    if (argc == 4 && strcmp(argv[1], "--serve-live-rx") == 0 &&
        strcmp(argv[3], "--initialize-radio-and-enable-rx-tuning") == 0) {
        return serve_live_rx(argv[2], true, false);
    }
    if (argc == 5 && strcmp(argv[1], "--serve-live-rx") == 0 &&
        strcmp(argv[3], "--initialize-radio-and-enable-rx-tuning") == 0 &&
        strcmp(argv[4], "--enable-test-page") == 0) {
        return serve_live_rx(argv[2], true, true);
    }
    print_usage(argv[0]);
    return EXIT_FAILURE;
}
