// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_USB_RX_H
#define FLEX1500_USB_RX_H

#include "flex1500/iq.h"
#include "flex1500/protocol.h"
#include "flex1500/tx_audio_stream.h"

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
    uint64_t status_packets;
    uint64_t status_changes;
    uint64_t status_errors;
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
int flex1500_usb_rx_set_gain(flex1500_usb_rx *receiver, int32_t gain_db);

/* Prepare the validated TX hardware path; PA selection follows RX tuning. */
int flex1500_usb_rx_enable_transmit_preparation(flex1500_usb_rx *receiver);
int flex1500_usb_rx_disable_transmit_preparation(flex1500_usb_rx *receiver);
bool flex1500_usb_rx_transmit_prepared(const flex1500_usb_rx *receiver);
bool flex1500_usb_rx_pa_filter(const flex1500_usb_rx *receiver,
                               uint32_t *filter);

/* Transmit-enabled daemon Tune backend; emits the capture-matched 5 W tone. */
int flex1500_usb_rx_tune_carrier_start(flex1500_usb_rx *receiver);
int flex1500_usb_rx_tune_carrier_stop(flex1500_usb_rx *receiver);
bool flex1500_usb_rx_tune_carrier_active(const flex1500_usb_rx *receiver);

/* Live physical-microphone stream backend; not yet connected to PTT. */
int flex1500_usb_rx_microphone_tx_start(flex1500_usb_rx *receiver,
                                       flex1500_tx_sideband sideband,
                                       unsigned int drive_percent,
                                       float microphone_gain);
int flex1500_usb_rx_microphone_tx_stop(flex1500_usb_rx *receiver);
const flex1500_tx_audio_stats *flex1500_usb_rx_microphone_tx_stats(
    const flex1500_usb_rx *receiver);

typedef enum flex1500_tx_fault_stage {
    FLEX1500_TX_FAULT_NONE = 0,
    FLEX1500_TX_FAULT_AFTER_STREAM,
    FLEX1500_TX_FAULT_AFTER_KEY,
} flex1500_tx_fault_stage;

/* Standalone research-probe hook; never exposed by the daemon or API. */
void flex1500_usb_rx_set_tx_fault_stage(flex1500_usb_rx *receiver,
                                        flex1500_tx_fault_stage stage);

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
bool flex1500_usb_rx_gain(const flex1500_usb_rx *receiver, int32_t *gain_db);
bool flex1500_usb_rx_physical_inputs(
    const flex1500_usb_rx *receiver, flex1500_physical_inputs *inputs);

#endif
