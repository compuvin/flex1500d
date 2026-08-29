// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ISO_PACKET_COUNT = 64,
    ISO_PACKET_SIZE = FLEX1500_SAMPLE_PACKET_SIZE,
    ISO_BUFFER_SIZE = ISO_PACKET_COUNT * ISO_PACKET_SIZE,
    TRANSFER_TIMEOUT_MS = 1000,
};

typedef struct completion_state {
    int completed;
} completion_state;

typedef struct sample_stats {
    unsigned int packets_ok;
    unsigned int packets_error;
    unsigned int bytes_received;
    unsigned int sample_frames;
    unsigned int nonzero_bytes;
    int16_t min_i;
    int16_t max_i;
    int16_t min_q;
    int16_t max_q;
    bool have_samples;
} sample_stats;

static void transfer_complete(struct libusb_transfer *transfer)
{
    completion_state *state = transfer->user_data;
    state->completed = 1;
}

static void print_plan(void)
{
    puts("FLEX-1500 sample-IN probe (NOT ARMED)");
    puts("Planned USB operations:");
    puts("  1. Initialize libusb and open only device 2192:1502.");
    puts("  2. Refuse if interface 3 has an active kernel driver.");
    puts("  3. Claim interface 3 without detaching a driver.");
    puts("  4. Submit one isochronous IN transfer to endpoint 0x82.");
    puts("  5. Request 64 packets of 192 bytes (64 ms at 1,000 packets/s).");
    puts("  6. Report packet status and signed-16-bit interleaved I/Q stats.");
    puts("  7. Release the interface and close the device.");
    puts("Excluded: endpoint 0x01, endpoint 0x04, endpoint 0x83, control");
    puts("transfers, reset, configuration changes, and alternate settings.");
    puts("No USB device was opened.");
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--help]\n", program);
    printf("       %s --execute-approved-sample-in-probe\n", program);
    puts("");
    puts("With no arguments, prints the offline plan and exits.");
    puts("The execution flag must only be used after explicit permission from KB1JDX.");
}

static bool is_armed(int argc, char **argv)
{
    if (argc == 1) {
        return false;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
    }
    if (argc == 2 &&
        strcmp(argv[1], "--execute-approved-sample-in-probe") == 0) {
        return true;
    }

    fprintf(stderr, "Unknown or incomplete arguments.\n");
    print_usage(argv[0]);
    exit(EXIT_FAILURE);
}

static void update_stats(sample_stats *stats, const unsigned char *data,
                         unsigned int length)
{
    for (unsigned int index = 0; index < length; ++index) {
        if (data[index] != 0) {
            ++stats->nonzero_bytes;
        }
    }

    for (unsigned int index = 0; index + 3 < length; index += 4) {
        int16_t sample_i;
        int16_t sample_q;
        flex1500_decode_iq_frame(&data[index], &sample_i, &sample_q);

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
        ++stats->sample_frames;
    }
}

static void inspect_transfer(struct libusb_transfer *transfer,
                             sample_stats *stats)
{
    bool printed_first_payload = false;

    for (int packet_index = 0; packet_index < transfer->num_iso_packets;
         ++packet_index) {
        struct libusb_iso_packet_descriptor *packet =
            &transfer->iso_packet_desc[packet_index];

        if (packet->status != LIBUSB_TRANSFER_COMPLETED) {
            ++stats->packets_error;
            continue;
        }

        ++stats->packets_ok;
        stats->bytes_received += packet->actual_length;
        if (packet->actual_length == 0) {
            continue;
        }

        unsigned char *data =
            libusb_get_iso_packet_buffer_simple(transfer, packet_index);
        update_stats(stats, data, packet->actual_length);

        if (!printed_first_payload) {
            unsigned int display_length =
                packet->actual_length < 64 ? packet->actual_length : 64;
            printf("first payload (%u of %u bytes):", display_length,
                   packet->actual_length);
            for (unsigned int index = 0; index < display_length; ++index) {
                printf(" %02x", data[index]);
            }
            putchar('\n');
            printed_first_payload = true;
        }
    }
}

static int wait_for_transfer(libusb_context *context, completion_state *state,
                             struct libusb_transfer *transfer)
{
    while (!state->completed) {
        int result = libusb_handle_events_completed(context, &state->completed);
        if (result == LIBUSB_SUCCESS || result == LIBUSB_ERROR_INTERRUPTED) {
            continue;
        }

        fprintf(stderr, "libusb event handling failed: %s\n",
                libusb_error_name(result));
        result = libusb_cancel_transfer(transfer);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_NOT_FOUND) {
            fprintf(stderr, "transfer cancellation failed: %s\n",
                    libusb_error_name(result));
        }
        while (!state->completed) {
            libusb_handle_events_completed(context, &state->completed);
        }
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static int run_probe(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    struct libusb_transfer *transfer = NULL;
    unsigned char *buffer = NULL;
    completion_state completion = {0};
    sample_stats stats = {0};
    bool claimed = false;
    int exit_code = EXIT_FAILURE;
    int result = libusb_init(&context);

    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "libusb initialization failed: %s\n",
                libusb_error_name(result));
        return EXIT_FAILURE;
    }

    handle = libusb_open_device_with_vid_pid(
        context, FLEX1500_USB_VENDOR_ID, FLEX1500_USB_PRODUCT_ID);
    if (handle == NULL) {
        fputs("Unable to open FLEX-1500 2192:1502.\n", stderr);
        goto cleanup;
    }

    result = libusb_kernel_driver_active(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != 0) {
        if (result == 1) {
            fputs("Refusing probe: interface 3 has an active kernel driver.\n",
                  stderr);
        } else {
            fprintf(stderr, "Cannot verify kernel-driver state: %s\n",
                    libusb_error_name(result));
        }
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
        fputs("Internal safety policy rejected endpoint 0x82.\n", stderr);
        goto cleanup;
    }

    transfer = libusb_alloc_transfer(ISO_PACKET_COUNT);
    buffer = calloc(1, ISO_BUFFER_SIZE);
    if (transfer == NULL || buffer == NULL) {
        fputs("Unable to allocate isochronous transfer buffers.\n", stderr);
        goto cleanup;
    }

    libusb_fill_iso_transfer(transfer, handle, FLEX1500_EP_SAMPLE_IN, buffer,
                             ISO_BUFFER_SIZE, ISO_PACKET_COUNT,
                             transfer_complete, &completion,
                             TRANSFER_TIMEOUT_MS);
    libusb_set_iso_packet_lengths(transfer, ISO_PACKET_SIZE);

    result = libusb_submit_transfer(transfer);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Cannot submit sample-IN transfer: %s\n",
                libusb_error_name(result));
        goto cleanup;
    }

    if (wait_for_transfer(context, &completion, transfer) != EXIT_SUCCESS) {
        goto cleanup;
    }

    printf("transfer status: %d\n", transfer->status);
    inspect_transfer(transfer, &stats);
    printf("packets completed: %u\n", stats.packets_ok);
    printf("packets with errors or timeout: %u\n", stats.packets_error);
    printf("bytes received: %u\n", stats.bytes_received);
    printf("complex sample frames: %u\n", stats.sample_frames);
    printf("nonzero bytes: %u\n", stats.nonzero_bytes);
    if (stats.have_samples) {
        printf("I range: %d to %d\n", stats.min_i, stats.max_i);
        printf("Q range: %d to %d\n", stats.min_q, stats.max_q);
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (transfer != NULL) libusb_free_transfer(transfer);
    free(buffer);
    if (claimed) {
        result =
            libusb_release_interface(handle, FLEX1500_STREAMING_INTERFACE);
        if (result != LIBUSB_SUCCESS) {
            fprintf(stderr, "Warning: release interface failed: %s\n",
                    libusb_error_name(result));
        }
    }
    if (handle != NULL) libusb_close(handle);
    libusb_exit(context);
    return exit_code;
}

int main(int argc, char **argv)
{
    if (!is_armed(argc, argv)) {
        print_plan();
        return EXIT_SUCCESS;
    }
    return run_probe();
}
