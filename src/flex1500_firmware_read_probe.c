// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    REQUEST_INDEX = 1,
    TRANSFER_TIMEOUT_MS = 1000,
};

typedef struct completion_state {
    bool completed;
} completion_state;

static void transfer_complete(struct libusb_transfer *transfer)
{
    completion_state *state = transfer->user_data;
    state->completed = true;
}

static void print_bytes(const char *label, const uint8_t *bytes, size_t length)
{
    printf("%s:", label);
    for (size_t index = 0; index < length; ++index) {
        printf(" %02x", bytes[index]);
    }
    putchar('\n');
}

static void print_plan(void)
{
    uint8_t request[FLEX1500_COMMAND_PACKET_SIZE];
    bool built = flex1500_build_firmware_read_request(
        REQUEST_INDEX, FLEX1500_OP_GET_FIRMWARE_REV, 0, 0, request);

    puts("FLEX-1500 firmware-read probe (NOT ARMED)");
    puts("Planned USB operations:");
    puts("  1. Initialize libusb and open only device 2192:1502.");
    puts("  2. Refuse if interface 3 has an active kernel driver.");
    puts("  3. Claim interface 3 without detaching a driver.");
    puts("  4. Submit one interrupt-IN response buffer on endpoint 0x83.");
    puts("  5. Send one 20-byte interrupt-OUT request on endpoint 0x04:");
    puts("       opcode 1200 GET_FIRMWARE_REV, param1=0, param2=0.");
    puts("  6. Accept only a type-1 response with matching request index 1.");
    puts("  7. Release the interface and close the device.");
    puts("Excluded: every other opcode, endpoint 0x01, endpoint 0x82,");
    puts("control transfers, reset, configuration changes, and alt settings.");
    if (built) print_bytes("Exact request", request, sizeof(request));
    puts("No USB device was opened.");
}

static void print_usage(const char *program)
{
    printf("Usage: %s [--help]\n", program);
    printf("       %s --execute-approved-firmware-read-probe\n", program);
    puts("");
    puts("With no arguments, prints the offline plan and exits.");
    puts("The execution flag must only be used after explicit permission from KB1JDX.");
}

static bool is_armed(int argc, char **argv)
{
    if (argc == 1) return false;
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        exit(EXIT_SUCCESS);
    }
    if (argc == 2 &&
        strcmp(argv[1], "--execute-approved-firmware-read-probe") == 0) {
        return true;
    }

    fputs("Unknown or incomplete arguments.\n", stderr);
    print_usage(argv[0]);
    exit(EXIT_FAILURE);
}

static int handle_until_complete(libusb_context *context,
                                 completion_state *in_state,
                                 completion_state *out_state)
{
    while (!in_state->completed || !out_state->completed) {
        int result = libusb_handle_events(context);
        if (result == LIBUSB_SUCCESS || result == LIBUSB_ERROR_INTERRUPTED) {
            continue;
        }
        fprintf(stderr, "libusb event handling failed: %s\n",
                libusb_error_name(result));
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}

static void cancel_pending(libusb_context *context,
                           struct libusb_transfer *transfer,
                           completion_state *state)
{
    if (transfer == NULL || state->completed) return;

    int result = libusb_cancel_transfer(transfer);
    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_NOT_FOUND) {
        fprintf(stderr, "transfer cancellation failed: %s\n",
                libusb_error_name(result));
        return;
    }
    while (!state->completed) {
        result = libusb_handle_events(context);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            break;
        }
    }
}

static int run_probe(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    struct libusb_transfer *in_transfer = NULL;
    struct libusb_transfer *out_transfer = NULL;
    completion_state in_state = {false};
    completion_state out_state = {false};
    uint8_t in_packet[FLEX1500_STATUS_PACKET_SIZE] = {0};
    uint8_t out_packet[FLEX1500_COMMAND_PACKET_SIZE] = {0};
    bool claimed = false;
    bool in_submitted = false;
    bool out_submitted = false;
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

    if (!flex1500_rx_probe_transfer_allowed(FLEX1500_TRANSFER_INTERRUPT,
                                             FLEX1500_EP_STATUS_IN)) {
        fputs("Safety policy rejected response endpoint 0x83.\n", stderr);
        goto cleanup;
    }
    if (!flex1500_build_firmware_read_request(
            REQUEST_INDEX, FLEX1500_OP_GET_FIRMWARE_REV, 0, 0, out_packet)) {
        fputs("Safety policy rejected firmware-read request.\n", stderr);
        goto cleanup;
    }

    in_transfer = libusb_alloc_transfer(0);
    out_transfer = libusb_alloc_transfer(0);
    if (in_transfer == NULL || out_transfer == NULL) {
        fputs("Unable to allocate interrupt transfers.\n", stderr);
        goto cleanup;
    }

    libusb_fill_interrupt_transfer(
        in_transfer, handle, FLEX1500_EP_STATUS_IN, in_packet,
        sizeof(in_packet), transfer_complete, &in_state, TRANSFER_TIMEOUT_MS);
    libusb_fill_interrupt_transfer(
        out_transfer, handle, FLEX1500_EP_COMMAND_OUT, out_packet,
        sizeof(out_packet), transfer_complete, &out_state, TRANSFER_TIMEOUT_MS);

    result = libusb_submit_transfer(in_transfer);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Cannot submit response-IN transfer: %s\n",
                libusb_error_name(result));
        goto cleanup;
    }
    in_submitted = true;

    result = libusb_submit_transfer(out_transfer);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Cannot submit firmware-read request: %s\n",
                libusb_error_name(result));
        goto cleanup;
    }
    out_submitted = true;

    if (handle_until_complete(context, &in_state, &out_state) != EXIT_SUCCESS) {
        goto cleanup;
    }

    print_bytes("Request sent", out_packet, sizeof(out_packet));
    printf("request transfer status: %d, bytes: %d\n", out_transfer->status,
           out_transfer->actual_length);
    printf("response transfer status: %d, bytes: %d\n", in_transfer->status,
           in_transfer->actual_length);

    if (out_transfer->status != LIBUSB_TRANSFER_COMPLETED ||
        out_transfer->actual_length != FLEX1500_COMMAND_PACKET_SIZE) {
        fputs("Firmware-read request did not complete in full.\n", stderr);
        goto cleanup;
    }
    if (in_transfer->status != LIBUSB_TRANSFER_COMPLETED) {
        fputs("Firmware-read response did not complete.\n", stderr);
        goto cleanup;
    }

    print_bytes("Response received", in_packet, in_transfer->actual_length);
    {
        uint32_t firmware_revision = 0;
        if (!flex1500_decode_u32_response(
                in_packet, (size_t)in_transfer->actual_length, REQUEST_INDEX,
                &firmware_revision)) {
            fputs("Response type or request index did not match.\n", stderr);
            goto cleanup;
        }
        printf("firmware raw: 0x%08x\n", firmware_revision);
        printf("firmware version: %u.%u.%u.%u\n",
               (firmware_revision >> 24) & 0xff,
               (firmware_revision >> 16) & 0xff,
               (firmware_revision >> 8) & 0xff, firmware_revision & 0xff);
    }
    exit_code = EXIT_SUCCESS;

cleanup:
    if (in_submitted) cancel_pending(context, in_transfer, &in_state);
    if (out_submitted) cancel_pending(context, out_transfer, &out_state);
    if (in_transfer != NULL) libusb_free_transfer(in_transfer);
    if (out_transfer != NULL) libusb_free_transfer(out_transfer);
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
