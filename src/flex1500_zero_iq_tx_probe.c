// SPDX-License-Identifier: GPL-3.0-only

#define _POSIX_C_SOURCE 200809L

#include "flex1500/protocol.h"

#include <libusb.h>

#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    ISO_TRANSFER_COUNT = 8,
#ifdef FLEX1500_TWO_TONE_PROBE
    /* 50 ms contains integral cycles of both 700 Hz and 1900 Hz. */
    ISO_PACKETS_PER_TRANSFER = 50,
#else
    ISO_PACKETS_PER_TRANSFER = 64,
#endif
    ISO_BUFFER_SIZE = ISO_PACKETS_PER_TRANSFER * FLEX1500_SAMPLE_PACKET_SIZE,
    COMMAND_TIMEOUT_MS = 1000,
    EVENT_SLICE_US = 5000,
    PRE_ROLL_MS = 250,
    TRANSITION_MS = 200,
    TX_DURATION_MS = 3000,
};

#define TX_FREQUENCY_HZ UINT32_C(28475000)
#define TX_TUNING_WORD UINT32_C(0x25f77777)
#define RX_TUNING_WORD UINT32_C(0x25f60000)
#define PA_FILTER_INDEX UINT32_C(2)
#define DISPLAYED_DRIVE_PERCENT UINT32_C(50)
#define TWO_TONE_AMPLITUDE 6222.0
#define SAMPLE_RATE_HZ 48000.0

typedef struct stream_state {
    libusb_context *context;
    libusb_device_handle *handle;
    struct libusb_transfer *transfers[ISO_TRANSFER_COUNT];
    uint8_t *buffers[ISO_TRANSFER_COUNT];
    bool running;
    bool failed;
    int active;
    uint64_t packets_completed;
} stream_state;

static volatile sig_atomic_t interrupted = 0;

static void on_signal(int signal_number)
{
    (void)signal_number;
    interrupted = 1;
}

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static void print_packet(const uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    for (size_t offset = 0; offset < FLEX1500_COMMAND_PACKET_SIZE; ++offset) {
        printf(" %02x", packet[offset]);
    }
    putchar('\n');
}

static void build_step(unsigned int step,
                       uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    switch (step) {
    case 0: flex1500_build_initialize_request(0, packet); break;
    case 1: flex1500_build_pa_filter_request(1, PA_FILTER_INDEX, packet); break;
    case 2: flex1500_build_amp_tx1_request(2, true, packet); break;
    case 3: flex1500_build_tuning_word_request(3, RX_TUNING_WORD, packet); break;
    case 4: flex1500_build_transition_mute_request(4, true, packet); break;
    case 5: flex1500_build_tuning_word_request(5, TX_TUNING_WORD, packet); break;
    case 6: flex1500_build_tr_request(6, true, packet); break;
    case 7: flex1500_build_transition_mute_request(7, false, packet); break;
    case 8: flex1500_build_transition_mute_request(8, true, packet); break;
    case 9: flex1500_build_tr_request(9, false, packet); break;
    case 10: flex1500_build_tuning_word_request(10, RX_TUNING_WORD, packet); break;
    case 11: flex1500_build_transition_mute_request(11, false, packet); break;
    default: flex1500_build_pa_filter_request(12, 0, packet); break;
    }
}

static void print_plan(void)
{
    static const char *labels[] = {
        "INITIALIZE", "SET_PA_FILTER(2)", "SET_AMP_TX1(1)",
        "RX center 0x25f60000", "transition mute", "exact TX center",
        "SET_TR(1)", "transition unmute", "transition mute",
        "SET_TR(0)", "restore RX center", "transition unmute",
        "SET_PA_FILTER(0)"
    };
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];

#ifdef FLEX1500_TWO_TONE_PROBE
    puts("FLEX-1500 fixed 700/1900 Hz two-tone TX probe (NOT ARMED)");
#else
    puts("FLEX-1500 fixed zero-I/Q TX switching probe (NOT ARMED)");
#endif
    printf("Frequency: %u Hz; duration: %u ms; recorded drive metadata: %u%%\n",
           TX_FREQUENCY_HZ, TX_DURATION_MS, DISPLAYED_DRIVE_PERCENT);
    puts("Load requirement: suitable 50-ohm dummy load connected before run.");
#ifdef FLEX1500_TWO_TONE_PROBE
    puts("Waveform: equal -700 and -1900 Hz complex tones at 48 kHz;");
    puts("each tone amplitude is 6222 counts, matching PowerSDR at 50% drive.");
    puts("This waveform intentionally produces RF into the required dummy load.");
#else
    puts("Waveform: every endpoint-0x01 I/Q sample is exactly (0, 0).");
    puts("The 50% factor therefore remains metadata: 50% of zero is zero.");
#endif
    puts("Planned fixed commands:");
    for (unsigned int step = 0; step < 13; ++step) {
        build_step(step, packet);
        printf("  %2u. %-24s", step + 1, labels[step]);
        print_packet(packet);
    }
#ifdef FLEX1500_TWO_TONE_PROBE
    puts("Timing: queue eight 50-ms continuous two-tone transfers; wait 250 ms; key;");
#else
    puts("Timing: queue eight 64-ms zero-I/Q transfers; wait 250 ms; key;");
#endif
    puts("unmute after 200 ms; unkey exactly 3000 ms after SET_TR(1);");
    puts("restore RX center, unmute after 200 ms, then set PA filter 0.");
    puts("SET_TR(0) is attempted during cleanup after every keyed error/signal.");
#ifdef FLEX1500_TWO_TONE_PROBE
    puts("Excluded: variable samples/levels, antenna changes, PA bias, EEPROM,");
#else
    puts("Excluded: nonzero TX samples, antenna changes, PA bias, EEPROM,");
#endif
    puts("firmware writes, USB reset, and network control.");
    puts("No USB device was opened and no radio state was changed.");
}

static int send_packet(libusb_device_handle *handle, const char *label,
                       const uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    int transferred = 0;
    int result = libusb_interrupt_transfer(
        handle, FLEX1500_EP_COMMAND_OUT, (unsigned char *)packet,
        FLEX1500_COMMAND_PACKET_SIZE, &transferred, COMMAND_TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS || transferred != FLEX1500_COMMAND_PACKET_SIZE) {
        fprintf(stderr, "%s failed: %s, %d/20 bytes\n", label,
                libusb_error_name(result), transferred);
        return -1;
    }
    printf("%s transferred 20/20 bytes.\n", label);
    return 0;
}

static void transfer_complete(struct libusb_transfer *transfer)
{
    stream_state *state = transfer->user_data;
    --state->active;
    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        state->packets_completed += (uint64_t)transfer->num_iso_packets;
    } else if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        state->failed = true;
    }
    if (state->running && !state->failed) {
        int result = libusb_submit_transfer(transfer);
        if (result == LIBUSB_SUCCESS) {
            ++state->active;
        } else {
            fprintf(stderr, "zero-I/Q resubmit failed: %s\n",
                    libusb_error_name(result));
            state->failed = true;
        }
    }
}

#ifdef FLEX1500_TWO_TONE_PROBE
static void fill_two_tone(uint8_t *buffer)
{
    const double pi = 3.14159265358979323846;
    const size_t frames = ISO_BUFFER_SIZE / 4;
    for (size_t n = 0; n < frames; ++n) {
        double phase_700 = 2.0 * pi * 700.0 * (double)n / SAMPLE_RATE_HZ;
        double phase_1900 = 2.0 * pi * 1900.0 * (double)n / SAMPLE_RATE_HZ;
        int16_t sample_i = (int16_t)lrint(
            TWO_TONE_AMPLITUDE * (cos(phase_700) + cos(phase_1900)));
        int16_t sample_q = (int16_t)lrint(
            -TWO_TONE_AMPLITUDE * (sin(phase_700) + sin(phase_1900)));
        buffer[4 * n] = (uint8_t)sample_i;
        buffer[4 * n + 1] = (uint8_t)((uint16_t)sample_i >> 8);
        buffer[4 * n + 2] = (uint8_t)sample_q;
        buffer[4 * n + 3] = (uint8_t)((uint16_t)sample_q >> 8);
    }
}
#endif

static int start_stream(stream_state *state)
{
    state->running = true;
    for (int index = 0; index < ISO_TRANSFER_COUNT; ++index) {
        state->buffers[index] = calloc(1, ISO_BUFFER_SIZE);
        state->transfers[index] = libusb_alloc_transfer(ISO_PACKETS_PER_TRANSFER);
        if (state->buffers[index] == NULL || state->transfers[index] == NULL) {
            fputs("Unable to allocate zero-I/Q transfer.\n", stderr);
            state->failed = true;
            return -1;
        }
#ifdef FLEX1500_TWO_TONE_PROBE
        fill_two_tone(state->buffers[index]);
#endif
        libusb_fill_iso_transfer(
            state->transfers[index], state->handle, FLEX1500_EP_SAMPLE_OUT,
            state->buffers[index], ISO_BUFFER_SIZE, ISO_PACKETS_PER_TRANSFER,
            transfer_complete, state, 0);
        libusb_set_iso_packet_lengths(state->transfers[index],
                                      FLEX1500_SAMPLE_PACKET_SIZE);
        int result = libusb_submit_transfer(state->transfers[index]);
        if (result != LIBUSB_SUCCESS) {
            fprintf(stderr, "zero-I/Q submit failed: %s\n",
                    libusb_error_name(result));
            state->failed = true;
            return -1;
        }
        ++state->active;
    }
    return 0;
}

static void service_until(stream_state *state, uint64_t deadline_ms)
{
    while (!interrupted && !state->failed && monotonic_ms() < deadline_ms) {
        struct timeval timeout = {0, EVENT_SLICE_US};
        int result = libusb_handle_events_timeout_completed(
            state->context, &timeout, NULL);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            fprintf(stderr, "USB event handling failed: %s\n",
                    libusb_error_name(result));
            state->failed = true;
        }
    }
}

static void stop_stream(stream_state *state)
{
    state->running = false;
    for (int index = 0; index < ISO_TRANSFER_COUNT; ++index) {
        if (state->transfers[index] != NULL) {
            int result = libusb_cancel_transfer(state->transfers[index]);
            if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_NOT_FOUND) {
                fprintf(stderr, "zero-I/Q cancel failed: %s\n",
                        libusb_error_name(result));
            }
        }
    }
    while (state->active > 0) {
        struct timeval timeout = {0, EVENT_SLICE_US};
        libusb_handle_events_timeout_completed(state->context, &timeout, NULL);
    }
    for (int index = 0; index < ISO_TRANSFER_COUNT; ++index) {
        libusb_free_transfer(state->transfers[index]);
        free(state->buffers[index]);
    }
}

static int execute_probe(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    stream_state stream = {0};
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    bool claimed = false;
    bool keyed = false;
    bool stream_started = false;
    bool pa_filter_idle = false;
    int exit_code = EXIT_FAILURE;
    int result = libusb_init(&context);
    if (result != LIBUSB_SUCCESS) return EXIT_FAILURE;

    handle = libusb_open_device_with_vid_pid(
        context, FLEX1500_USB_VENDOR_ID, FLEX1500_USB_PRODUCT_ID);
    if (handle == NULL) {
        fputs("Unable to open FLEX-1500 2192:1502.\n", stderr);
        goto cleanup;
    }
    result = libusb_kernel_driver_active(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != 0) {
        fputs("Refusing TX probe: interface 3 is not confirmed driver-free.\n",
              stderr);
        goto cleanup;
    }
    result = libusb_claim_interface(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Cannot claim interface 3: %s\n",
                libusb_error_name(result));
        goto cleanup;
    }
    claimed = true;
    stream.context = context;
    stream.handle = handle;

#define SEND_STEP(number, label) do { \
    build_step((number), packet); \
    if (send_packet(handle, (label), packet) != 0) goto cleanup; \
} while (0)

    SEND_STEP(0, "INITIALIZE");
    SEND_STEP(1, "SET_PA_FILTER(2)");
    SEND_STEP(2, "SET_AMP_TX1(1)");
    SEND_STEP(3, "SET_RX1_FREQ_TW(receive center)");
    stream_started = true;
    if (start_stream(&stream) != 0) goto cleanup;
    service_until(&stream, monotonic_ms() + PRE_ROLL_MS);
    if (interrupted || stream.failed) goto cleanup;

    SEND_STEP(4, "transition mute");
    SEND_STEP(5, "SET_RX1_FREQ_TW(exact TX center)");
    SEND_STEP(6, "SET_TR(1)");
    keyed = true;
    uint64_t keyed_at = monotonic_ms();
    service_until(&stream, keyed_at + TRANSITION_MS);
    if (interrupted || stream.failed) goto cleanup;
    SEND_STEP(7, "transition unmute");
    service_until(&stream, keyed_at + TX_DURATION_MS);
    if (interrupted || stream.failed) goto cleanup;

    SEND_STEP(8, "transition mute before unkey");
    SEND_STEP(9, "SET_TR(0)");
    keyed = false;
    SEND_STEP(10, "restore receive center");
    service_until(&stream, monotonic_ms() + TRANSITION_MS);
    SEND_STEP(11, "transition unmute after unkey");
    SEND_STEP(12, "SET_PA_FILTER(0)");
    pa_filter_idle = true;
    exit_code = stream.failed ? EXIT_FAILURE : EXIT_SUCCESS;

cleanup:
    if (keyed && handle != NULL) {
        build_step(8, packet);
        (void)send_packet(handle, "EMERGENCY transition mute", packet);
        build_step(9, packet);
        (void)send_packet(handle, "EMERGENCY SET_TR(0)", packet);
        build_step(10, packet);
        (void)send_packet(handle, "EMERGENCY restore receive center", packet);
    }
    if (stream_started) stop_stream(&stream);
    if (claimed) {
        if (!pa_filter_idle) {
            build_step(12, packet);
            (void)send_packet(handle, "cleanup SET_PA_FILTER(0)", packet);
        }
        libusb_release_interface(handle, FLEX1500_STREAMING_INTERFACE);
    }
    if (handle != NULL) libusb_close(handle);
    libusb_exit(context);
#ifdef FLEX1500_TWO_TONE_PROBE
    printf("two-tone packets completed: %llu\n",
#else
    printf("zero-I/Q packets completed: %llu\n",
#endif
           (unsigned long long)stream.packets_completed);
    return exit_code;
#undef SEND_STEP
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        print_plan();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [--help]\n", argv[0]);
#ifdef FLEX1500_TWO_TONE_PROBE
        printf("       %s --execute-approved-two-tone-tx-28475000-50pct-3s\n",
               argv[0]);
#else
        printf("       %s --execute-approved-zero-iq-tx-28475000-50pct-3s\n",
               argv[0]);
#endif
        return EXIT_SUCCESS;
    }
#ifdef FLEX1500_TWO_TONE_PROBE
    if (argc == 2 && strcmp(
            argv[1], "--execute-approved-two-tone-tx-28475000-50pct-3s") == 0) {
#else
    if (argc == 2 && strcmp(
            argv[1], "--execute-approved-zero-iq-tx-28475000-50pct-3s") == 0) {
#endif
        signal(SIGINT, on_signal);
        signal(SIGTERM, on_signal);
        return execute_probe();
    }
    fputs("Unknown or incomplete arguments; refusing TX probe.\n", stderr);
    return EXIT_FAILURE;
}
