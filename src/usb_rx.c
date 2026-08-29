// SPDX-License-Identifier: GPL-3.0-only

#define _POSIX_C_SOURCE 200809L

#include "flex1500/usb_rx.h"

#include "flex1500/protocol.h"

#include <libusb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

enum {
    RX_TRANSFER_COUNT = 8,
    RX_PACKETS_PER_TRANSFER = 64,
    RX_TRANSFER_BYTES =
        RX_PACKETS_PER_TRANSFER * FLEX1500_SAMPLE_PACKET_SIZE,
    RX_TRANSFER_TIMEOUT_MS = 1000,
    RX_FRAMES_PER_PACKET = FLEX1500_SAMPLE_PACKET_SIZE / 4,
    INITIALIZE_INDEX = 3,
};

typedef struct rx_slot {
    struct flex1500_usb_rx *receiver;
    struct libusb_transfer *transfer;
    uint8_t *buffer;
    bool submitted;
} rx_slot;

struct flex1500_usb_rx {
    libusb_context *context;
    libusb_device_handle *handle;
    flex1500_iq_ring *destination;
    rx_slot slots[RX_TRANSFER_COUNT];
    flex1500_iq_stats iq_stats;
    flex1500_dc_blocker dc_blocker;
    flex1500_usb_rx_counters counters;
    unsigned int active_transfers;
    bool interface_claimed;
    bool running;
    bool stopping;
    bool frequency_known;
    bool filter_known;
    uint32_t frequency_hz;
    uint32_t rx_filter;
    uint8_t next_command_index;
    uint64_t started_ms;
    bool sentinel_seen;
    char last_error[160];
};

static uint64_t monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * UINT64_C(1000) +
           (uint64_t)now.tv_nsec / UINT64_C(1000000);
}

static uint64_t elapsed_ms(const flex1500_usb_rx *receiver)
{
    uint64_t now = monotonic_ms();
    return now >= receiver->started_ms ? now - receiver->started_ms : 0;
}

static void mark_error_event(flex1500_usb_rx *receiver)
{
    uint64_t when = elapsed_ms(receiver);
    if (receiver->counters.error_events == 0) {
        receiver->counters.first_error_ms = when;
    }
    receiver->counters.last_error_ms = when;
    ++receiver->counters.error_events;
}

static void classify_transfer_status(flex1500_usb_rx *receiver, int status,
                                     bool packet)
{
    uint64_t *timeouts = packet ? &receiver->counters.packet_timeouts :
        &receiver->counters.transfer_timeouts;
    uint64_t *stalls = packet ? &receiver->counters.packet_stalls :
        &receiver->counters.transfer_stalls;
    uint64_t *no_device = packet ? &receiver->counters.packet_no_device :
        &receiver->counters.transfer_no_device;
    uint64_t *overflows = packet ? &receiver->counters.packet_overflows :
        &receiver->counters.transfer_overflows;
    uint64_t *other = packet ? &receiver->counters.packet_other_errors :
        &receiver->counters.transfer_other_errors;
    switch (status) {
    case LIBUSB_TRANSFER_TIMED_OUT: ++*timeouts; break;
    case LIBUSB_TRANSFER_STALL: ++*stalls; break;
    case LIBUSB_TRANSFER_NO_DEVICE: ++*no_device; break;
    case LIBUSB_TRANSFER_OVERFLOW: ++*overflows; break;
    default: ++*other; break;
    }
    mark_error_event(receiver);
}

static void set_error(flex1500_usb_rx *receiver, const char *message)
{
    snprintf(receiver->last_error, sizeof(receiver->last_error), "%s", message);
}

static void set_libusb_error(flex1500_usb_rx *receiver, const char *operation,
                             int error)
{
    snprintf(receiver->last_error, sizeof(receiver->last_error), "%s: %s",
             operation, libusb_error_name(error));
}

static void process_packet(flex1500_usb_rx *receiver, const uint8_t *data,
                           unsigned int length)
{
    uint64_t base_frame = receiver->iq_stats.frames;
    for (unsigned int offset = 0; offset + 3 < length; offset += 4) {
        int16_t sample_i;
        int16_t sample_q;
        flex1500_decode_iq_frame(&data[offset], &sample_i, &sample_q);
        if (sample_i == -1 && sample_q == -1) {
            uint64_t frame = base_frame + offset / 4;
            uint64_t when = elapsed_ms(receiver);
            if (!receiver->sentinel_seen) {
                receiver->counters.first_sentinel_frame = frame;
                receiver->counters.first_sentinel_ms = when;
                receiver->sentinel_seen = true;
            }
            receiver->counters.last_sentinel_frame = frame;
            receiver->counters.last_sentinel_ms = when;
        }
    }
    flex1500_iq_sample samples[RX_FRAMES_PER_PACKET];
    size_t frames = flex1500_process_iq16le(
        data, length, samples, RX_FRAMES_PER_PACKET, &receiver->iq_stats,
        &receiver->dc_blocker);
    flex1500_iq_ring_push(receiver->destination, samples, frames);
}

static int submit_slot(rx_slot *slot, bool resubmit)
{
    flex1500_usb_rx *receiver = slot->receiver;
    libusb_set_iso_packet_lengths(slot->transfer,
                                  FLEX1500_SAMPLE_PACKET_SIZE);
    int result = libusb_submit_transfer(slot->transfer);
    if (result != LIBUSB_SUCCESS) {
        ++receiver->counters.transfer_submit_errors;
        set_libusb_error(receiver, "submit RX transfer", result);
        receiver->running = false;
        return result;
    }
    slot->submitted = true;
    ++receiver->active_transfers;
    if (resubmit) ++receiver->counters.transfer_resubmits;
    return LIBUSB_SUCCESS;
}

static void rx_complete(struct libusb_transfer *transfer)
{
    rx_slot *slot = transfer->user_data;
    flex1500_usb_rx *receiver = slot->receiver;
    slot->submitted = false;
    if (receiver->active_transfers > 0) --receiver->active_transfers;

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        for (int index = 0; index < transfer->num_iso_packets; ++index) {
            struct libusb_iso_packet_descriptor *packet =
                &transfer->iso_packet_desc[index];
            if (packet->status != LIBUSB_TRANSFER_COMPLETED) {
                ++receiver->counters.usb_packet_errors;
                ++receiver->counters.packet_status_errors;
                classify_transfer_status(receiver, packet->status, true);
                continue;
            }
            ++receiver->counters.usb_packets;
            receiver->counters.bytes += packet->actual_length;
            if (packet->actual_length == 0) {
                ++receiver->counters.zero_length_packets;
                ++receiver->counters.short_packets;
                receiver->counters.missing_bytes += FLEX1500_SAMPLE_PACKET_SIZE;
                mark_error_event(receiver);
            } else if (packet->actual_length < FLEX1500_SAMPLE_PACKET_SIZE) {
                ++receiver->counters.short_packets;
                receiver->counters.missing_bytes +=
                    FLEX1500_SAMPLE_PACKET_SIZE - packet->actual_length;
                mark_error_event(receiver);
            } else if (packet->actual_length > FLEX1500_SAMPLE_PACKET_SIZE) {
                ++receiver->counters.oversized_packets;
                mark_error_event(receiver);
            }
            receiver->counters.trailing_bytes += packet->actual_length % 4;
            if (packet->actual_length > 0) {
                uint8_t *data =
                    libusb_get_iso_packet_buffer_simple(transfer, index);
                process_packet(receiver, data, packet->actual_length);
            }
        }
    } else if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        receiver->counters.usb_packet_errors += RX_PACKETS_PER_TRANSFER;
        ++receiver->counters.transfer_status_errors;
        classify_transfer_status(receiver, transfer->status, false);
    }

    if (!receiver->stopping && receiver->running) {
        submit_slot(slot, true);
    }
}

flex1500_usb_rx *flex1500_usb_rx_create(flex1500_iq_ring *destination)
{
    if (destination == NULL || destination->samples == NULL) return NULL;
    flex1500_usb_rx *receiver = calloc(1, sizeof(*receiver));
    if (receiver == NULL) return NULL;
    receiver->destination = destination;
    flex1500_iq_stats_reset(&receiver->iq_stats);
    flex1500_dc_blocker_init(&receiver->dc_blocker, 0.001f);
    return receiver;
}

static void free_slots(flex1500_usb_rx *receiver)
{
    for (unsigned int index = 0; index < RX_TRANSFER_COUNT; ++index) {
        if (receiver->slots[index].transfer != NULL) {
            libusb_free_transfer(receiver->slots[index].transfer);
        }
        free(receiver->slots[index].buffer);
        memset(&receiver->slots[index], 0, sizeof(receiver->slots[index]));
    }
}

void flex1500_usb_rx_stop(flex1500_usb_rx *receiver)
{
    if (receiver == NULL) return;
    receiver->stopping = true;
    receiver->running = false;

    for (unsigned int index = 0; index < RX_TRANSFER_COUNT; ++index) {
        if (receiver->slots[index].submitted) {
            libusb_cancel_transfer(receiver->slots[index].transfer);
        }
    }
    while (receiver->context != NULL && receiver->active_transfers > 0) {
        int result = libusb_handle_events(receiver->context);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) break;
    }

    free_slots(receiver);
    if (receiver->interface_claimed) {
        libusb_release_interface(receiver->handle, FLEX1500_STREAMING_INTERFACE);
        receiver->interface_claimed = false;
    }
    if (receiver->handle != NULL) {
        libusb_close(receiver->handle);
        receiver->handle = NULL;
    }
    if (receiver->context != NULL) {
        libusb_exit(receiver->context);
        receiver->context = NULL;
    }
    receiver->active_transfers = 0;
    receiver->stopping = false;
    receiver->frequency_known = false;
    receiver->filter_known = false;
    receiver->sentinel_seen = false;
    receiver->next_command_index = INITIALIZE_INDEX + 1;
}

void flex1500_usb_rx_destroy(flex1500_usb_rx *receiver)
{
    if (receiver == NULL) return;
    flex1500_usb_rx_stop(receiver);
    free(receiver);
}

int flex1500_usb_rx_start(flex1500_usb_rx *receiver)
{
    uint8_t command[FLEX1500_COMMAND_PACKET_SIZE];
    int transferred = 0;
    int result;

    if (receiver == NULL || receiver->running || receiver->context != NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    memset(&receiver->counters, 0, sizeof(receiver->counters));
    flex1500_iq_stats_reset(&receiver->iq_stats);
    flex1500_dc_blocker_init(&receiver->dc_blocker, 0.001f);
    receiver->started_ms = monotonic_ms();
    receiver->frequency_known = false;
    receiver->filter_known = false;
    receiver->sentinel_seen = false;
    receiver->next_command_index = INITIALIZE_INDEX + 1;
    receiver->last_error[0] = '\0';
    receiver->stopping = false;
    result = libusb_init(&receiver->context);
    if (result != LIBUSB_SUCCESS) {
        set_libusb_error(receiver, "initialize libusb", result);
        flex1500_usb_rx_stop(receiver);
        return result;
    }
    receiver->handle = libusb_open_device_with_vid_pid(
        receiver->context, FLEX1500_USB_VENDOR_ID, FLEX1500_USB_PRODUCT_ID);
    if (receiver->handle == NULL) {
        set_error(receiver, "open FLEX-1500 2192:1502 failed");
        flex1500_usb_rx_stop(receiver);
        return LIBUSB_ERROR_NO_DEVICE;
    }
    result = libusb_kernel_driver_active(receiver->handle,
                                         FLEX1500_STREAMING_INTERFACE);
    if (result != 0) {
        set_error(receiver, "interface 3 is not confirmed driver-free");
        flex1500_usb_rx_stop(receiver);
        return result == 1 ? LIBUSB_ERROR_BUSY : result;
    }
    result = libusb_claim_interface(receiver->handle,
                                    FLEX1500_STREAMING_INTERFACE);
    if (result != LIBUSB_SUCCESS) {
        set_libusb_error(receiver, "claim interface 3", result);
        flex1500_usb_rx_stop(receiver);
        return result;
    }
    receiver->interface_claimed = true;

    flex1500_build_initialize_request(INITIALIZE_INDEX, command);
    result = libusb_interrupt_transfer(
        receiver->handle, FLEX1500_EP_COMMAND_OUT, command, sizeof(command),
        &transferred, RX_TRANSFER_TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS ||
        transferred != FLEX1500_COMMAND_PACKET_SIZE) {
        if (result != LIBUSB_SUCCESS) {
            set_libusb_error(receiver, "send fixed INITIALIZE", result);
        } else {
            set_error(receiver, "fixed INITIALIZE transfer was short");
        }
        flex1500_usb_rx_stop(receiver);
        return result != LIBUSB_SUCCESS ? result : LIBUSB_ERROR_IO;
    }

    receiver->running = true;
    for (unsigned int index = 0; index < RX_TRANSFER_COUNT; ++index) {
        rx_slot *slot = &receiver->slots[index];
        slot->receiver = receiver;
        slot->transfer = libusb_alloc_transfer(RX_PACKETS_PER_TRANSFER);
        slot->buffer = calloc(1, RX_TRANSFER_BYTES);
        if (slot->transfer == NULL || slot->buffer == NULL) {
            set_error(receiver, "allocate RX transfer failed");
            flex1500_usb_rx_stop(receiver);
            return LIBUSB_ERROR_NO_MEM;
        }
        libusb_fill_iso_transfer(
            slot->transfer, receiver->handle, FLEX1500_EP_SAMPLE_IN,
            slot->buffer, RX_TRANSFER_BYTES, RX_PACKETS_PER_TRANSFER,
            rx_complete, slot, RX_TRANSFER_TIMEOUT_MS);
        result = submit_slot(slot, false);
        if (result != LIBUSB_SUCCESS) {
            flex1500_usb_rx_stop(receiver);
            return result;
        }
    }
    return LIBUSB_SUCCESS;
}

int flex1500_usb_rx_pump(flex1500_usb_rx *receiver)
{
    if (receiver == NULL || !receiver->running || receiver->context == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    int result = libusb_handle_events(receiver->context);
    if (result == LIBUSB_ERROR_INTERRUPTED) return LIBUSB_SUCCESS;
    if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
        set_libusb_error(receiver, "handle RX events", result);
        receiver->running = false;
    }
    return result;
}

static int send_rx_command(flex1500_usb_rx *receiver, const uint8_t *packet,
                           const char *operation)
{
    int transferred = 0;
    int result = libusb_interrupt_transfer(
        receiver->handle, FLEX1500_EP_COMMAND_OUT, (unsigned char *)packet,
        FLEX1500_COMMAND_PACKET_SIZE, &transferred, RX_TRANSFER_TIMEOUT_MS);
    if (result != LIBUSB_SUCCESS || transferred != FLEX1500_COMMAND_PACKET_SIZE) {
        ++receiver->counters.command_errors;
        if (result != LIBUSB_SUCCESS) {
            set_libusb_error(receiver, operation, result);
            return result;
        }
        set_error(receiver, "RX command transfer was short");
        return LIBUSB_ERROR_IO;
    }
    return LIBUSB_SUCCESS;
}

int flex1500_usb_rx_tune(flex1500_usb_rx *receiver, uint32_t frequency_hz)
{
    uint8_t tune[FLEX1500_COMMAND_PACKET_SIZE];
    uint8_t filter_packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t filter;
    if (receiver == NULL || !receiver->running || receiver->handle == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    if (!flex1500_rx_filter_for_frequency(frequency_hz, &filter) ||
        !flex1500_build_rx_tune_request(receiver->next_command_index,
                                        frequency_hz, tune, NULL) ||
        !flex1500_build_rx_filter_request(
            (uint8_t)(receiver->next_command_index + 1), filter,
            filter_packet)) {
        set_error(receiver, "RX tune rejected by frequency/filter policy");
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    receiver->next_command_index = (uint8_t)(receiver->next_command_index + 2);
    int result = send_rx_command(receiver, tune, "send SET_RX1_FREQ_TW");
    if (result != LIBUSB_SUCCESS) return result;
    receiver->frequency_hz = frequency_hz;
    receiver->frequency_known = true;
    receiver->filter_known = false;
    result = send_rx_command(receiver, filter_packet, "send SET_RX1_FILTER");
    if (result != LIBUSB_SUCCESS) return result;
    receiver->rx_filter = filter;
    receiver->filter_known = true;
    flex1500_iq_ring_clear(receiver->destination);
    ++receiver->counters.rx_tune_operations;
    return LIBUSB_SUCCESS;
}

bool flex1500_usb_rx_is_running(const flex1500_usb_rx *receiver)
{
    return receiver != NULL && receiver->running;
}

const flex1500_usb_rx_counters *
flex1500_usb_rx_get_counters(const flex1500_usb_rx *receiver)
{
    return receiver != NULL ? &receiver->counters : NULL;
}

const flex1500_iq_stats *
flex1500_usb_rx_get_iq_stats(const flex1500_usb_rx *receiver)
{
    return receiver != NULL ? &receiver->iq_stats : NULL;
}

const char *flex1500_usb_rx_last_error(const flex1500_usb_rx *receiver)
{
    return receiver != NULL ? receiver->last_error : "receiver is null";
}

bool flex1500_usb_rx_frequency(const flex1500_usb_rx *receiver,
                               uint32_t *frequency_hz)
{
    if (receiver == NULL || frequency_hz == NULL ||
        !receiver->frequency_known) return false;
    *frequency_hz = receiver->frequency_hz;
    return true;
}

bool flex1500_usb_rx_filter(const flex1500_usb_rx *receiver,
                            uint32_t *filter)
{
    if (receiver == NULL || filter == NULL || !receiver->filter_known) {
        return false;
    }
    *filter = receiver->rx_filter;
    return true;
}
