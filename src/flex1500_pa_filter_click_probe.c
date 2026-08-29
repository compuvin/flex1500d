// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { FILTER_5_INDEX = 14, FILTER_0_INDEX = 15, TIMEOUT_MS = 1000 };

static void print_packet(const uint8_t *packet)
{
    for (size_t index = 0; index < FLEX1500_COMMAND_PACKET_SIZE; ++index) {
        printf(" %02x", packet[index]);
    }
    putchar('\n');
}

static void print_plan(void)
{
    uint8_t filter_5[FLEX1500_COMMAND_PACKET_SIZE];
    uint8_t filter_0[FLEX1500_COMMAND_PACKET_SIZE];
    flex1500_build_pa_filter_request(FILTER_5_INDEX, 5, filter_5);
    flex1500_build_pa_filter_request(FILTER_0_INDEX, 0, filter_0);
    puts("FLEX-1500 PA filter click probe (NOT ARMED)");
    puts("  1. Open only 2192:1502 and claim interface 3.");
    puts("  2. Send opcode 1260 SET_PA_FILTER with filter=5.");
    printf("     Exact packet:"); print_packet(filter_5);
    puts("  3. Wait three seconds with no USB operation.");
    puts("  4. Send opcode 1260 SET_PA_FILTER with filter=0 (idle/bypass).");
    printf("     Exact packet:"); print_packet(filter_0);
    puts("  5. Release interface 3 and close USB.");
    puts("Excluded: PTT, MOX, PA bias, TX enable, sample streaming, frequency,");
    puts("RX filter, INITIALIZE, gain, antenna, firmware, EEPROM, and reset.");
    puts("No USB device was opened and no radio state was changed.");
}

static int send_filter(libusb_device_handle *handle, uint8_t index,
                       uint32_t filter)
{
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    int transferred = 0;
    flex1500_build_pa_filter_request(index, filter, packet);
    int result = libusb_interrupt_transfer(
        handle, FLEX1500_EP_COMMAND_OUT, packet, sizeof(packet), &transferred,
        TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS || transferred != (int)sizeof(packet)) {
        fprintf(stderr, "SET_PA_FILTER(%u) failed: %s, %d/20 bytes\n", filter,
                libusb_error_name(result), transferred);
        return -1;
    }
    printf("SET_PA_FILTER(%u) transferred 20/20 bytes.\n", filter);
    return 0;
}

static int execute_probe(void)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    bool claimed = false;
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
    if (send_filter(handle, FILTER_5_INDEX, 5) != 0) goto cleanup;
    sleep(3);
    if (send_filter(handle, FILTER_0_INDEX, 0) != 0) goto cleanup;
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
        printf("       %s --execute-approved-pa-filter-click-test\n", argv[0]);
        return EXIT_SUCCESS;
    }
    if (argc == 2 &&
        strcmp(argv[1], "--execute-approved-pa-filter-click-test") == 0) {
        return execute_probe();
    }
    fputs("Unknown or incomplete arguments.\n", stderr);
    return EXIT_FAILURE;
}
