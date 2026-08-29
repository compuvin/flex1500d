// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <libusb.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { TUNE_INDEX = 11, TRANSFER_TIMEOUT_MS = 1000 };

static void usage(const char *program)
{
    printf("Usage: %s [--help]\n", program);
    printf("       %s --plan FREQUENCY_HZ\n", program);
    printf("       %s --execute-approved-tune FREQUENCY_HZ\n", program);
}

static bool parse_frequency(const char *text, uint32_t *frequency)
{
    char *end = NULL;
    errno = 0;
    unsigned long value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || value > UINT32_MAX ||
        value < FLEX1500_MIN_RX_FREQUENCY_HZ ||
        value > FLEX1500_MAX_RX_FREQUENCY_HZ) {
        return false;
    }
    *frequency = (uint32_t)value;
    return true;
}

static void print_plan(uint32_t frequency)
{
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t tuning_word;
    flex1500_build_rx_tune_request(TUNE_INDEX, frequency, packet,
                                   &tuning_word);
    printf("FLEX-1500 RX frequency tune probe (NOT ARMED)\n");
    printf("Requested center frequency: %u Hz\n", frequency);
    printf("384 MHz reference tuning word: %u / 0x%08x\n", tuning_word,
           tuning_word);
    printf("Exact 20-byte endpoint-0x04 packet:");
    for (size_t index = 0; index < sizeof(packet); ++index) {
        printf(" %02x", packet[index]);
    }
    putchar('\n');
    puts("Operation: open 2192:1502, claim interface 3, send this packet,");
    puts("then release and close. Opcode 1347 SET_RX1_FREQ_TW, param2=0.");
    puts("Excluded: INITIALIZE, filters/relays, gain, PTT, TX, endpoint 0x01,");
    puts("firmware, EEPROM, reset, configuration, and alternate settings.");
    puts("No USB device was opened and no radio state was changed.");
}

static int execute_tune(uint32_t frequency)
{
    libusb_context *context = NULL;
    libusb_device_handle *handle = NULL;
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t tuning_word;
    bool claimed = false;
    int result = libusb_init(&context);
    int transferred = 0;
    int exit_code = EXIT_FAILURE;

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
        fputs("Refusing tune: interface 3 is not confirmed driver-free.\n",
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

    if (!flex1500_build_rx_tune_request(TUNE_INDEX, frequency, packet,
                                        &tuning_word)) {
        fputs("Tune request rejected by frequency policy.\n", stderr);
        goto cleanup;
    }
    result = libusb_interrupt_transfer(
        handle, FLEX1500_EP_COMMAND_OUT, packet, sizeof(packet), &transferred,
        TRANSFER_TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS || transferred != (int)sizeof(packet)) {
        fprintf(stderr, "Tune transfer failed: %s, %d/20 bytes\n",
                libusb_error_name(result), transferred);
        goto cleanup;
    }
    printf("Sent one RX tune packet: %u Hz, tuning word 0x%08x.\n",
           frequency, tuning_word);
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
        puts("FLEX-1500 RX tune probe is offline by default.");
        usage(argv[0]);
        puts("No USB device was opened and no radio state was changed.");
        return EXIT_SUCCESS;
    }
    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(argv[0]);
        return EXIT_SUCCESS;
    }
    uint32_t frequency;
    if (argc != 3 || !parse_frequency(argv[2], &frequency)) {
        fputs("Frequency must be an integer from 100000 through 54000000 Hz.\n",
              stderr);
        usage(argv[0]);
        return EXIT_FAILURE;
    }
    if (strcmp(argv[1], "--plan") == 0) {
        print_plan(frequency);
        return EXIT_SUCCESS;
    }
    if (strcmp(argv[1], "--execute-approved-tune") == 0) {
        return execute_tune(frequency);
    }
    fputs("Unknown or incomplete arguments.\n", stderr);
    usage(argv[0]);
    return EXIT_FAILURE;
}
