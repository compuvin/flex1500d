// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    DEFAULT_ATTEMPTS = 10,
    MAX_ATTEMPTS = 1000,
    INTERRUPT_TIMEOUT_MS = 250,
};

typedef struct probe_options {
    bool armed;
    unsigned int attempts;
} probe_options;

static void print_plan(void)
{
    puts("FLEX-1500 status probe (NOT ARMED)");
    puts("Planned USB operations:");
    puts("  1. Initialize libusb.");
    puts("  2. Open only device 2192:1502.");
    puts("  3. Refuse to continue if interface 3 has a kernel driver.");
    puts("  4. Claim interface 3 without detaching any driver.");
    puts("  5. Attempt interrupt IN reads from endpoint 0x83 only.");
    puts("  6. Release the interface and close the device.");
    puts("Excluded: control transfers, OUT transfers, device reset,");
    puts("configuration changes, alternate settings, and endpoint 0x82.");
    puts("No USB device was opened.");
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--help]\n", program);
    printf("       %s --execute-approved-status-probe [--attempts N]\n", program);
    puts("");
    puts("With no arguments, prints the offline plan and exits.");
    puts("The execution flag must only be used after explicit permission from KB1JDX.");
}

static bool parse_uint(const char *text, unsigned int *value)
{
    char *end = NULL;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0 ||
        parsed > MAX_ATTEMPTS || parsed > UINT_MAX) {
        return false;
    }

    *value = (unsigned int)parsed;
    return true;
}

static bool parse_options(int argc, char **argv, probe_options *options)
{
    options->armed = false;
    options->attempts = DEFAULT_ATTEMPTS;

    for (int index = 1; index < argc; ++index) {
        if (strcmp(argv[index], "--help") == 0) {
            print_usage(argv[0]);
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[index], "--execute-approved-status-probe") == 0) {
            options->armed = true;
            continue;
        }
        if (strcmp(argv[index], "--attempts") == 0 && index + 1 < argc) {
            ++index;
            if (!parse_uint(argv[index], &options->attempts)) {
                fprintf(stderr, "Invalid attempt count: %s\n", argv[index]);
                return false;
            }
            continue;
        }

        fprintf(stderr, "Unknown or incomplete option: %s\n", argv[index]);
        return false;
    }

    return true;
}

static void print_packet(unsigned int sequence, const unsigned char *data,
                         int length)
{
    printf("packet %u length %d:", sequence, length);
    for (int index = 0; index < length; ++index) {
        printf(" %02x", data[index]);
    }
    putchar('\n');
}

static int run_probe(const probe_options *options)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    unsigned int received = 0;
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
        libusb_exit(context);
        return EXIT_FAILURE;
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
        libusb_close(handle);
        libusb_exit(context);
        return EXIT_FAILURE;
    }

    result = libusb_claim_interface(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Cannot claim interface 3: %s\n",
                libusb_error_name(result));
        libusb_close(handle);
        libusb_exit(context);
        return EXIT_FAILURE;
    }

    for (unsigned int attempt = 1; attempt <= options->attempts; ++attempt) {
        unsigned char packet[FLEX1500_STATUS_PACKET_SIZE] = {0};
        int transferred = 0;

        if (!flex1500_rx_probe_transfer_allowed(
                FLEX1500_TRANSFER_INTERRUPT, FLEX1500_EP_STATUS_IN)) {
            fputs("Internal safety policy rejected endpoint 0x83.\n", stderr);
            result = LIBUSB_ERROR_OTHER;
            break;
        }

        result = libusb_interrupt_transfer(
            handle, FLEX1500_EP_STATUS_IN, packet, sizeof(packet), &transferred,
            INTERRUPT_TIMEOUT_MS);
        if (result == LIBUSB_ERROR_TIMEOUT) {
            printf("attempt %u: timeout (no packet)\n", attempt);
            continue;
        }
        if (result != LIBUSB_SUCCESS) {
            fprintf(stderr, "attempt %u failed: %s\n", attempt,
                    libusb_error_name(result));
            break;
        }

        ++received;
        print_packet(received, packet, transferred);
    }

    {
        int release_result =
            libusb_release_interface(handle, FLEX1500_STREAMING_INTERFACE);
        if (release_result != LIBUSB_SUCCESS) {
            fprintf(stderr, "Warning: release interface failed: %s\n",
                    libusb_error_name(release_result));
        }
    }
    libusb_close(handle);
    libusb_exit(context);

    printf("received %u status packet(s)\n", received);
    return result == LIBUSB_SUCCESS || result == LIBUSB_ERROR_TIMEOUT
               ? EXIT_SUCCESS
               : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    probe_options options;

    if (!parse_options(argc, argv, &options)) {
        print_usage(argv[0]);
        return EXIT_FAILURE;
    }

    if (!options.armed) {
        print_plan();
        return EXIT_SUCCESS;
    }

    return run_probe(&options);
}
