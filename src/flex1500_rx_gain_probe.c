// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { COMMAND_INDEX = 16, TRANSFER_TIMEOUT_MS = 1000 };

static int send_gain(flex1500_rx_gain gain)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    int transferred = 0;
    int result = libusb_init(&context);
    int exit_code = EXIT_FAILURE;
    bool claimed = false;

    if (result != LIBUSB_SUCCESS) goto cleanup;
    handle = libusb_open_device_with_vid_pid(
        context, FLEX1500_USB_VENDOR_ID, FLEX1500_USB_PRODUCT_ID);
    if (handle == NULL) {
        fputs("Unable to open FLEX-1500 2192:1502.\n", stderr);
        goto cleanup;
    }
    result = libusb_kernel_driver_active(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != 0) {
        fputs("Refusing gain change: interface 3 is not driver-free.\n", stderr);
        goto cleanup;
    }
    result = libusb_claim_interface(handle, FLEX1500_STREAMING_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        fprintf(stderr, "Cannot claim interface 3: %s\n",
                libusb_error_name(result));
        goto cleanup;
    }
    claimed = true;
    if (!flex1500_build_rx_gain_request(COMMAND_INDEX, gain, packet)) {
        fputs("Gain request rejected by protocol policy.\n", stderr);
        goto cleanup;
    }
    result = libusb_interrupt_transfer(
        handle, FLEX1500_EP_COMMAND_OUT, packet, sizeof(packet), &transferred,
        TRANSFER_TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS || transferred != (int)sizeof(packet)) {
        fprintf(stderr, "Gain transfer failed: %s, %d/20 bytes\n",
                libusb_error_name(result), transferred);
        goto cleanup;
    }
    printf("Set FLEX-1500 receive gain to %s dB.\n",
           gain == FLEX1500_RX_GAIN_0_DB ? "0" : "+20");
    exit_code = EXIT_SUCCESS;

cleanup:
    if (claimed) libusb_release_interface(handle, FLEX1500_STREAMING_INTERFACE);
    if (handle != NULL) libusb_close(handle);
    if (context != NULL) libusb_exit(context);
    return exit_code;
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        puts("FLEX-1500 RX gain probe is offline by default.");
        puts("Approved test options: --execute-approved-gain-0 or "
             "--execute-approved-gain-plus-20");
        puts("No USB device was opened and no radio state was changed.");
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--execute-approved-gain-0") == 0) {
        return send_gain(FLEX1500_RX_GAIN_0_DB);
    }
    if (argc == 2 &&
        strcmp(argv[1], "--execute-approved-gain-plus-20") == 0) {
        return send_gain(FLEX1500_RX_GAIN_PLUS_20_DB);
    }
    fputs("Unknown or unapproved gain operation.\n", stderr);
    return EXIT_FAILURE;
}
