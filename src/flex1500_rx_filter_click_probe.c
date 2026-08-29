// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    FILTER_5_INDEX = 12,
    FILTER_2_INDEX = 13,
    TRANSFER_TIMEOUT_MS = 1000,
};

static void print_packet(const uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE])
{
    for (size_t index = 0; index < FLEX1500_COMMAND_PACKET_SIZE; ++index) {
        printf(" %02x", packet[index]);
    }
    putchar('\n');
}

static void print_plan(void)
{
    uint8_t filter_5[FLEX1500_COMMAND_PACKET_SIZE];
    uint8_t filter_2[FLEX1500_COMMAND_PACKET_SIZE];
    flex1500_build_rx_filter_request(FILTER_5_INDEX, 5, filter_5);
    flex1500_build_rx_filter_request(FILTER_2_INDEX, 2, filter_2);

    puts("FLEX-1500 RX filter click probe (NOT ARMED)");
    puts("Planned USB operations:");
    puts("  1. Open only 2192:1502 and claim interface 3.");
    puts("  2. Send opcode 1257 SET_RX1_FILTER with filter=5.");
    printf("     Exact packet:");
    print_packet(filter_5);
    puts("  3. Wait three seconds; no USB operation occurs during the wait.");
    puts("  4. Send opcode 1257 SET_RX1_FILTER with filter=2.");
    printf("     Exact packet:");
    print_packet(filter_2);
    puts("  5. Release interface 3 and close USB.");
    puts("Filter 2 is the last observed RX-filter state at PowerSDR exit.");
    puts("If the radio was reset since then, this is a known end state rather");
    puts("than a guaranteed restoration of an unknown current state.");
    puts("Excluded: PA filter, frequency, INITIALIZE, gain, routing, PTT, TX,");
    puts("sample endpoints, firmware, EEPROM, reset, and configuration changes.");
    puts("No USB device was opened and no radio state was changed.");
}

static int send_packet(libusb_device_handle *handle, const uint8_t *packet,
                       const char *label)
{
    int transferred = 0;
    int result = libusb_interrupt_transfer(
        handle, FLEX1500_EP_COMMAND_OUT, (unsigned char *)packet,
        FLEX1500_COMMAND_PACKET_SIZE, &transferred, TRANSFER_TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS ||
        transferred != FLEX1500_COMMAND_PACKET_SIZE) {
        fprintf(stderr, "%s failed: %s, %d/20 bytes\n", label,
                libusb_error_name(result), transferred);
        return -1;
    }
    printf("%s transferred 20/20 bytes.\n", label);
    return 0;
}

static int execute_probe(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    uint8_t filter_5[FLEX1500_COMMAND_PACKET_SIZE];
    uint8_t filter_2[FLEX1500_COMMAND_PACKET_SIZE];
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
        fputs("Refusing probe: interface 3 is not confirmed driver-free.\n",
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

    if (!flex1500_build_rx_filter_request(FILTER_5_INDEX, 5, filter_5) ||
        !flex1500_build_rx_filter_request(FILTER_2_INDEX, 2, filter_2)) {
        fputs("Internal filter policy rejected a fixed packet.\n", stderr);
        goto cleanup;
    }
    if (send_packet(handle, filter_5, "SET_RX1_FILTER(5)") != 0) goto cleanup;
    sleep(3);
    if (send_packet(handle, filter_2, "SET_RX1_FILTER(2)") != 0) goto cleanup;
    exit_code = EXIT_SUCCESS;

cleanup:
    if (claimed) libusb_release_interface(handle, FLEX1500_STREAMING_INTERFACE);
    if (handle != NULL) libusb_close(handle);
    libusb_exit(context);
    return exit_code;
}

int main(int argc, char **argv)
{
    if (argc == 1) {
        print_plan();
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        printf("Usage: %s [--help]\n", argv[0]);
        printf("       %s --execute-approved-rx-filter-click-test\n", argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc == 2 &&
        strcmp(argv[1], "--execute-approved-rx-filter-click-test") == 0) {
        return execute_probe();
    }
    fputs("Unknown or incomplete arguments.\n", stderr);
    return EXIT_FAILURE;
}
