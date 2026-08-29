// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    TRANSFER_COUNT = 8,
    PACKETS_PER_TRANSFER = 128,
    TOTAL_PACKETS = TRANSFER_COUNT * PACKETS_PER_TRANSFER,
    DISCARD_PACKETS = 32,
    TRANSFER_BYTES = PACKETS_PER_TRANSFER * FLEX1500_SAMPLE_PACKET_SIZE,
    TRANSFER_TIMEOUT_MS = 2500,
};

static const char *const OUTPUT_PATH = "captures/rx-settled.iq16le";

typedef struct capture_state capture_state;

typedef struct transfer_state {
    capture_state *capture;
    bool completed;
} transfer_state;

struct capture_state {
    unsigned int completed_count;
};

typedef struct iq_stats {
    uint64_t frames;
    uint64_t non_sentinel_frames;
    int64_t sum_i;
    int64_t sum_q;
    int16_t min_i;
    int16_t max_i;
    int16_t min_q;
    int16_t max_q;
    bool have_samples;
} iq_stats;

static void transfer_complete(struct libusb_transfer *transfer)
{
    transfer_state *state = transfer->user_data;
    state->completed = true;
    ++state->capture->completed_count;
}

static void print_plan(void)
{
    puts("FLEX-1500 settled RX capture (NOT ARMED)");
    puts("Planned USB operations:");
    puts("  1. Open only device 2192:1502 and claim interface 3.");
    puts("  2. Queue eight receive-only transfers on endpoint 0x82.");
    puts("  3. Each transfer contains 128 x 192-byte packets.");
    puts("  4. Capture 1,024 packets (about 1.024 seconds).");
    puts("  5. Discard the first 32 packets and save the remaining I/Q.");
    puts("  6. Report packet, sentinel, range, and DC-mean statistics.");
    printf("Output: %s (execution refuses to overwrite it)\n", OUTPUT_PATH);
    puts("Excluded: every OUT endpoint, command, control transfer, reset,");
    puts("configuration change, alternate setting, and driver detach.");
    puts("No USB device was opened and no output file was created.");
}

static bool is_armed(int argc, char **argv)
{
    if (argc == 1) return false;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [--help]\n", argv[0]);
        printf("       %s --execute-approved-settled-rx-capture\n", argv[0]);
        exit(EXIT_SUCCESS);
    }
    if (argc == 2 &&
        strcmp(argv[1], "--execute-approved-settled-rx-capture") == 0) {
        return true;
    }
    fputs("Unknown or incomplete arguments.\n", stderr);
    exit(EXIT_FAILURE);
}

static void update_stats(iq_stats *stats, const uint8_t *data,
                         unsigned int length)
{
    for (unsigned int offset = 0; offset + 3 < length; offset += 4) {
        int16_t sample_i;
        int16_t sample_q;
        flex1500_decode_iq_frame(&data[offset], &sample_i, &sample_q);
        if (!stats->have_samples) {
            stats->min_i = stats->max_i = sample_i;
            stats->min_q = stats->max_q = sample_q;
            stats->have_samples = true;
        } else {
            if (sample_i < stats->min_i) stats->min_i = sample_i;
            if (sample_i > stats->max_i) stats->max_i = sample_i;
            if (sample_q < stats->min_q) stats->min_q = sample_q;
            if (sample_q > stats->max_q) stats->max_q = sample_q;
        }
        stats->sum_i += sample_i;
        stats->sum_q += sample_q;
        ++stats->frames;
        if (sample_i != -1 || sample_q != -1) ++stats->non_sentinel_frames;
    }
}

static void cancel_pending(libusb_context *context,
                           struct libusb_transfer **transfers,
                           transfer_state *states, unsigned int submitted)
{
    for (unsigned int index = 0; index < submitted; ++index) {
        if (!states[index].completed) libusb_cancel_transfer(transfers[index]);
    }
    for (;;) {
        bool all_done = true;
        for (unsigned int index = 0; index < submitted; ++index) {
            if (!states[index].completed) all_done = false;
        }
        if (all_done) break;
        int result = libusb_handle_events(context);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) break;
    }
}

static int run_capture(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    struct libusb_transfer *transfers[TRANSFER_COUNT] = {0};
    transfer_state transfer_states[TRANSFER_COUNT] = {0};
    uint8_t *buffers[TRANSFER_COUNT] = {0};
    capture_state capture = {0};
    FILE *output = NULL;
    iq_stats stats = {0};
    unsigned int submitted = 0;
    unsigned int packet_number = 0;
    unsigned int packets_ok = 0;
    unsigned int packets_error = 0;
    uint64_t bytes_written = 0;
    bool claimed = false;
    bool printed_payload = false;
    int exit_code = EXIT_FAILURE;
    int result;

    output = fopen(OUTPUT_PATH, "wbx");
    if (output == NULL) {
        fprintf(stderr, "Cannot create %s (it may already exist).\n", OUTPUT_PATH);
        return EXIT_FAILURE;
    }

    result = libusb_init(&context);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "libusb initialization failed: %s\n",
                libusb_error_name(result));
        goto cleanup;
    }
    handle = libusb_open_device_with_vid_pid(
        context, FLEX1500_USB_VENDOR_ID, FLEX1500_USB_PRODUCT_ID);
    if (handle == NULL) {
        fputs("Unable to open FLEX-1500 2192:1502.\n", stderr);
        goto cleanup;
    }
    result = libusb_kernel_driver_active(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != 0) {
        fputs("Refusing capture: cannot confirm interface 3 is driver-free.\n",
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

    if (!flex1500_rx_probe_transfer_allowed(FLEX1500_TRANSFER_ISOCHRONOUS,
                                             FLEX1500_EP_SAMPLE_IN)) {
        fputs("Safety policy rejected endpoint 0x82.\n", stderr);
        goto cleanup;
    }

    for (unsigned int index = 0; index < TRANSFER_COUNT; ++index) {
        transfers[index] = libusb_alloc_transfer(PACKETS_PER_TRANSFER);
        buffers[index] = calloc(1, TRANSFER_BYTES);
        if (transfers[index] == NULL || buffers[index] == NULL) {
            fputs("Unable to allocate capture transfer.\n", stderr);
            goto cleanup;
        }
        transfer_states[index].capture = &capture;
        libusb_fill_iso_transfer(
            transfers[index], handle, FLEX1500_EP_SAMPLE_IN, buffers[index],
            TRANSFER_BYTES, PACKETS_PER_TRANSFER, transfer_complete,
            &transfer_states[index], TRANSFER_TIMEOUT_MS);
        libusb_set_iso_packet_lengths(transfers[index],
                                      FLEX1500_SAMPLE_PACKET_SIZE);
        result = libusb_submit_transfer(transfers[index]);
        if (result != LIBUSB_SUCCESS) {
            fprintf(stderr, "Cannot submit transfer %u: %s\n", index,
                    libusb_error_name(result));
            goto cleanup;
        }
        ++submitted;
    }

    while (capture.completed_count < TRANSFER_COUNT) {
        result = libusb_handle_events(context);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            fprintf(stderr, "libusb event handling failed: %s\n",
                    libusb_error_name(result));
            goto cleanup;
        }
    }

    for (unsigned int transfer_index = 0; transfer_index < TRANSFER_COUNT;
         ++transfer_index) {
        struct libusb_transfer *transfer = transfers[transfer_index];
        for (int iso_index = 0; iso_index < transfer->num_iso_packets;
             ++iso_index, ++packet_number) {
            struct libusb_iso_packet_descriptor *packet =
                &transfer->iso_packet_desc[iso_index];
            if (packet->status != LIBUSB_TRANSFER_COMPLETED) {
                ++packets_error;
                continue;
            }
            ++packets_ok;
            if (packet_number < DISCARD_PACKETS || packet->actual_length == 0) {
                continue;
            }
            uint8_t *data =
                libusb_get_iso_packet_buffer_simple(transfer, iso_index);
            if (fwrite(data, 1, packet->actual_length, output) !=
                packet->actual_length) {
                fputs("Failed while writing capture file.\n", stderr);
                goto cleanup;
            }
            bytes_written += packet->actual_length;
            update_stats(&stats, data, packet->actual_length);
            if (!printed_payload) {
                unsigned int shown =
                    packet->actual_length < 64 ? packet->actual_length : 64;
                printf("First retained payload:");
                for (unsigned int byte = 0; byte < shown; ++byte) {
                    printf(" %02x", data[byte]);
                }
                putchar('\n');
                printed_payload = true;
            }
        }
    }

    printf("packets completed: %u\n", packets_ok);
    printf("packets with errors or timeout: %u\n", packets_error);
    printf("packets discarded: %u\n", DISCARD_PACKETS);
    printf("bytes written: %llu\n", (unsigned long long)bytes_written);
    printf("complex sample frames: %llu\n", (unsigned long long)stats.frames);
    printf("non-sentinel frames: %llu\n",
           (unsigned long long)stats.non_sentinel_frames);
    if (stats.have_samples) {
        printf("I range: %d to %d\n", stats.min_i, stats.max_i);
        printf("Q range: %d to %d\n", stats.min_q, stats.max_q);
        printf("I mean: %.6f\n", (double)stats.sum_i / (double)stats.frames);
        printf("Q mean: %.6f\n", (double)stats.sum_q / (double)stats.frames);
    }
    printf("output: %s\n", OUTPUT_PATH);
    exit_code = EXIT_SUCCESS;

cleanup:
    if (context != NULL && submitted > 0 &&
        capture.completed_count < submitted) {
        cancel_pending(context, transfers, transfer_states, submitted);
    }
    for (unsigned int index = 0; index < TRANSFER_COUNT; ++index) {
        if (transfers[index] != NULL) libusb_free_transfer(transfers[index]);
        free(buffers[index]);
    }
    if (claimed) libusb_release_interface(handle, FLEX1500_STREAMING_INTERFACE);
    if (handle != NULL) libusb_close(handle);
    if (context != NULL) libusb_exit(context);
    if (output != NULL) fclose(output);
    return exit_code;
}

int main(int argc, char **argv)
{
    if (!is_armed(argc, argv)) {
        print_plan();
        return EXIT_SUCCESS;
    }
    return run_capture();
}
