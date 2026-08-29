// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_USB_RX_H
#define FLEX1500_USB_RX_H

#include "flex1500/iq.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct flex1500_usb_rx flex1500_usb_rx;

typedef struct flex1500_usb_rx_counters {
    uint64_t usb_packets;
    uint64_t usb_packet_errors;
    uint64_t bytes;
    uint64_t transfer_resubmits;
    uint64_t transfer_submit_errors;
    uint64_t rx_tune_operations;
    uint64_t command_errors;
    uint64_t transfer_status_errors;
    uint64_t transfer_timeouts;
    uint64_t transfer_stalls;
    uint64_t transfer_no_device;
    uint64_t transfer_overflows;
    uint64_t transfer_other_errors;
    uint64_t packet_status_errors;
    uint64_t packet_timeouts;
    uint64_t packet_stalls;
    uint64_t packet_no_device;
    uint64_t packet_overflows;
    uint64_t packet_other_errors;
    uint64_t short_packets;
    uint64_t zero_length_packets;
    uint64_t oversized_packets;
    uint64_t missing_bytes;
    uint64_t trailing_bytes;
    uint64_t error_events;
    uint64_t first_error_ms;
    uint64_t last_error_ms;
    uint64_t first_sentinel_frame;
    uint64_t last_sentinel_frame;
    uint64_t first_sentinel_ms;
    uint64_t last_sentinel_ms;
} flex1500_usb_rx_counters;

/* Allocation and inspection are offline and do not initialize libusb. */
flex1500_usb_rx *flex1500_usb_rx_create(flex1500_iq_ring *destination);
void flex1500_usb_rx_destroy(flex1500_usb_rx *receiver);

/*
 * Permission-gated live boundary. This opens 2192:1502, sends the one fixed
 * opcode-1219 INITIALIZE packet, and queues endpoint-0x82 IN transfers.
 */
int flex1500_usb_rx_start(flex1500_usb_rx *receiver);

/* Process USB completions. Valid only after a successful start. */
int flex1500_usb_rx_pump(flex1500_usb_rx *receiver);

/* Sends only SET_RX1_FREQ_TW followed by the mapped SET_RX1_FILTER. */
int flex1500_usb_rx_tune(flex1500_usb_rx *receiver, uint32_t frequency_hz);

/* Cancel host-side IN transfers, release interface 3, and close USB. */
void flex1500_usb_rx_stop(flex1500_usb_rx *receiver);

bool flex1500_usb_rx_is_running(const flex1500_usb_rx *receiver);
const flex1500_usb_rx_counters *
flex1500_usb_rx_get_counters(const flex1500_usb_rx *receiver);
const flex1500_iq_stats *
flex1500_usb_rx_get_iq_stats(const flex1500_usb_rx *receiver);
const char *flex1500_usb_rx_last_error(const flex1500_usb_rx *receiver);
bool flex1500_usb_rx_frequency(const flex1500_usb_rx *receiver,
                               uint32_t *frequency_hz);
bool flex1500_usb_rx_filter(const flex1500_usb_rx *receiver,
                            uint32_t *filter);

#endif
