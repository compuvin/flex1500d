// SPDX-License-Identifier: GPL-3.0-only

#define _POSIX_C_SOURCE 200809L

#include "flex1500/usb_rx.h"

#include "flex1500/protocol.h"
#include "flex1500/tx_lifecycle.h"
#include "flex1500/usb_io.h"

#include <libusb.h>

#include <math.h>
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
    TUNE_TRANSFER_COUNT = 8,
    TUNE_PACKETS_PER_TRANSFER = 50,
    TUNE_TRANSFER_BYTES =
        TUNE_PACKETS_PER_TRANSFER * FLEX1500_SAMPLE_PACKET_SIZE,
    TUNE_TRANSITION_MS = 200,
    TUNE_PRE_ROLL_MS = 250,
};

#define TUNE_TONE_HZ 600.0
#define TUNE_SAMPLE_RATE_HZ 48000.0
#define TUNE_MAGNITUDE 24890.0

typedef struct rx_slot {
    struct flex1500_usb_rx *receiver;
    struct libusb_transfer *transfer;
    uint8_t *buffer;
    bool submitted;
} rx_slot;

typedef struct tune_slot {
    struct flex1500_usb_rx *receiver;
    struct libusb_transfer *transfer;
    uint8_t *buffer;
    bool submitted;
    size_t audio_frames;
} tune_slot;

struct flex1500_usb_rx {
    libusb_context *context;
    libusb_device_handle *handle;
    flex1500_iq_ring *destination;
    rx_slot slots[RX_TRANSFER_COUNT];
    tune_slot tune_slots[TUNE_TRANSFER_COUNT];
    struct libusb_transfer *status_transfer;
    uint8_t status_buffer[FLEX1500_STATUS_PACKET_SIZE];
    bool status_submitted;
    bool physical_inputs_known;
    flex1500_physical_inputs physical_inputs;
    flex1500_iq_stats iq_stats;
    flex1500_dc_blocker dc_blocker;
    flex1500_usb_rx_counters counters;
    unsigned int active_transfers;
    unsigned int active_tune_transfers;
    bool interface_claimed;
    bool running;
    bool stopping;
    bool tune_streaming;
    bool tune_prepared;
    bool tune_active;
    bool tune_keyed;
    bool tune_key_attempted;
    bool tune_transition_attempted;
    bool tune_frequency_attempted;
    bool microphone_streaming;
    bool microphone_capture_enabled;
    bool tx_accepting_audio;
    flex1500_tx_audio_stream microphone_stream;
    flex1500_tx_fault_stage tx_fault_stage;
    bool transmit_prepared;
    bool amp_enable_attempted;
    bool pa_filter_known;
    uint32_t pa_filter;
    bool frequency_known;
    bool filter_known;
    uint32_t frequency_hz;
    uint32_t rx_filter;
    bool gain_known;
    int32_t gain_db;
    uint8_t next_command_index;
    uint64_t started_ms;
    bool sentinel_seen;
    unsigned int consecutive_transfer_failures;
    char last_error[160];
    flex1500_usb_io io;
};

static int production_send_command(void *context, const uint8_t *packet,
                                   size_t bytes, size_t *transferred);
static int production_start_tx_stream(void *context, bool generated_audio);
static int production_service_tx_stream(void *context, uint64_t duration_ms);
static void production_stop_tx_stream(void *context);

static const flex1500_usb_io_ops PRODUCTION_USB_IO_OPS = {
    production_send_command,
    production_start_tx_stream,
    production_service_tx_stream,
    production_stop_tx_stream,
};

static bool tx_stream_present(const flex1500_usb_rx *receiver)
{
    if (receiver->tune_streaming || receiver->active_tune_transfers > 0) {
        return true;
    }
    for (unsigned int index = 0; index < TUNE_TRANSFER_COUNT; ++index) {
        const tune_slot *slot = &receiver->tune_slots[index];
        if (slot->transfer != NULL || slot->buffer != NULL || slot->submitted) {
            return true;
        }
    }
    return false;
}

static uint32_t tx_partial_state(const flex1500_usb_rx *receiver)
{
    uint32_t state = FLEX1500_TX_PARTIAL_NONE;
    if (receiver->pa_filter_known) state |= FLEX1500_TX_PARTIAL_PA_SELECTED;
    if (receiver->amp_enable_attempted || receiver->transmit_prepared) {
        state |= FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED;
    }
    if (tx_stream_present(receiver)) {
        state |= FLEX1500_TX_PARTIAL_STREAM_STARTED;
    }
    if (receiver->tune_transition_attempted) {
        state |= FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED;
    }
    if (receiver->tune_frequency_attempted) {
        state |= FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED;
    }
    if (receiver->tune_key_attempted || receiver->tune_keyed ||
        receiver->tune_active) {
        state |= FLEX1500_TX_PARTIAL_KEY_ATTEMPTED;
    }
    return state;
}

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
    if (receiver->microphone_streaming) {
        if (receiver->microphone_capture_enabled &&
            receiver->tx_accepting_audio) {
            (void)flex1500_tx_audio_stream_push_iq16le(
                &receiver->microphone_stream, data, length);
        }
        /* Endpoint 0x82 carries microphone input in TX, not receive I/Q. */
        return;
    }
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
        receiver->consecutive_transfer_failures = 0;
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
        if (++receiver->consecutive_transfer_failures >= RX_TRANSFER_COUNT) {
            set_error(receiver, "RX stream unhealthy after repeated USB transfer failures");
            receiver->running = false;
        }
    }

    if (!receiver->stopping && receiver->running) {
        submit_slot(slot, true);
    }
}

static int submit_status(flex1500_usb_rx *receiver)
{
    int result = libusb_submit_transfer(receiver->status_transfer);
    if (result != LIBUSB_SUCCESS) {
        ++receiver->counters.status_errors;
        set_libusb_error(receiver, "submit endpoint-0x83 status transfer",
                         result);
        receiver->running = false;
        return result;
    }
    receiver->status_submitted = true;
    ++receiver->active_transfers;
    return LIBUSB_SUCCESS;
}

static void status_complete(struct libusb_transfer *transfer)
{
    flex1500_usb_rx *receiver = transfer->user_data;
    receiver->status_submitted = false;
    if (receiver->active_transfers > 0) --receiver->active_transfers;

    if (transfer->status == LIBUSB_TRANSFER_COMPLETED) {
        flex1500_physical_inputs inputs;
        ++receiver->counters.status_packets;
        if (flex1500_decode_physical_inputs(
                transfer->buffer, (size_t)transfer->actual_length, &inputs)) {
            if (!receiver->physical_inputs_known ||
                !flex1500_physical_inputs_equal(
                    &receiver->physical_inputs, &inputs)) {
                ++receiver->counters.status_changes;
            }
            receiver->physical_inputs = inputs;
            receiver->physical_inputs_known = true;
        } else {
            ++receiver->counters.status_errors;
        }
    } else if (transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        ++receiver->counters.status_errors;
        if (transfer->status == LIBUSB_TRANSFER_NO_DEVICE) {
            set_error(receiver, "endpoint-0x83 status listener lost device");
            receiver->running = false;
        }
    }

    if (!receiver->stopping && receiver->running) {
        (void)submit_status(receiver);
    }
}

static void tune_complete(struct libusb_transfer *transfer)
{
    tune_slot *slot = transfer->user_data;
    flex1500_usb_rx *receiver = slot->receiver;
    slot->submitted = false;
    slot->audio_frames = 0;
    if (receiver->active_tune_transfers > 0) {
        --receiver->active_tune_transfers;
    }

    if (transfer->status != LIBUSB_TRANSFER_COMPLETED &&
        transfer->status != LIBUSB_TRANSFER_CANCELLED) {
        set_error(receiver, "Tune endpoint-0x01 stream failed");
        receiver->tune_streaming = false;
        receiver->running = false;
    }
    if (receiver->tune_streaming && receiver->running) {
        if (receiver->microphone_streaming) {
            size_t before = flex1500_tx_audio_stream_queued(
                &receiver->microphone_stream);
            (void)flex1500_tx_audio_stream_render_iq16le(
                &receiver->microphone_stream, transfer->buffer,
                (size_t)transfer->length / 4);
            size_t after = flex1500_tx_audio_stream_queued(
                &receiver->microphone_stream);
            slot->audio_frames = before - after;
        }
        int result = libusb_submit_transfer(transfer);
        if (result == LIBUSB_SUCCESS) {
            slot->submitted = true;
            ++receiver->active_tune_transfers;
        } else {
            set_libusb_error(receiver, "resubmit Tune endpoint-0x01", result);
            receiver->tune_streaming = false;
            receiver->running = false;
        }
    }
}

flex1500_usb_rx *flex1500_usb_rx_create(flex1500_iq_ring *destination)
{
    if (destination == NULL || destination->samples == NULL) return NULL;
    flex1500_usb_rx *receiver = calloc(1, sizeof(*receiver));
    if (receiver == NULL) return NULL;
    receiver->destination = destination;
    if (!flex1500_usb_io_init(&receiver->io, &PRODUCTION_USB_IO_OPS,
                              receiver)) {
        free(receiver);
        return NULL;
    }
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
    for (unsigned int index = 0; index < TUNE_TRANSFER_COUNT; ++index) {
        if (receiver->tune_slots[index].transfer != NULL) {
            libusb_free_transfer(receiver->tune_slots[index].transfer);
        }
        free(receiver->tune_slots[index].buffer);
        memset(&receiver->tune_slots[index], 0,
               sizeof(receiver->tune_slots[index]));
    }
    if (receiver->status_transfer != NULL) {
        libusb_free_transfer(receiver->status_transfer);
        receiver->status_transfer = NULL;
    }
}

void flex1500_usb_rx_stop(flex1500_usb_rx *receiver)
{
    if (receiver == NULL) return;
    uint32_t cleanup = flex1500_tx_cleanup_plan(tx_partial_state(receiver));
    if (receiver->tune_prepared ||
        (cleanup & (FLEX1500_TX_CLEANUP_UNKEY |
                    FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                    FLEX1500_TX_CLEANUP_STOP_STREAM)) != 0) {
        (void)flex1500_usb_rx_tune_carrier_stop(receiver);
    }
    if (receiver->transmit_prepared || receiver->amp_enable_attempted ||
        receiver->pa_filter_known) {
        (void)flex1500_usb_rx_disable_transmit_preparation(receiver);
    }
    receiver->stopping = true;
    receiver->running = false;

    for (unsigned int index = 0; index < RX_TRANSFER_COUNT; ++index) {
        if (receiver->slots[index].submitted) {
            libusb_cancel_transfer(receiver->slots[index].transfer);
        }
    }
    if (receiver->status_submitted) {
        libusb_cancel_transfer(receiver->status_transfer);
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
    receiver->active_tune_transfers = 0;
    receiver->stopping = false;
    receiver->frequency_known = false;
    receiver->filter_known = false;
    receiver->sentinel_seen = false;
    receiver->physical_inputs_known = false;
    receiver->transmit_prepared = false;
    receiver->amp_enable_attempted = false;
    receiver->pa_filter_known = false;
    receiver->tune_key_attempted = false;
    receiver->tune_transition_attempted = false;
    receiver->tune_frequency_attempted = false;
    receiver->consecutive_transfer_failures = 0;
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
    size_t transferred = 0;
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
    receiver->physical_inputs_known = false;
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
    result = flex1500_usb_io_send_command(
        &receiver->io, command, sizeof(command), &transferred);
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
    receiver->status_transfer = libusb_alloc_transfer(0);
    if (receiver->status_transfer == NULL) {
        set_error(receiver, "allocate endpoint-0x83 status transfer failed");
        flex1500_usb_rx_stop(receiver);
        return LIBUSB_ERROR_NO_MEM;
    }
    libusb_fill_interrupt_transfer(
        receiver->status_transfer, receiver->handle, FLEX1500_EP_STATUS_IN,
        receiver->status_buffer, sizeof(receiver->status_buffer),
        status_complete, receiver, 0);
    result = submit_status(receiver);
    if (result != LIBUSB_SUCCESS) {
        flex1500_usb_rx_stop(receiver);
        return result;
    }
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
    size_t transferred = 0;
    int result = flex1500_usb_io_send_command(
        &receiver->io, packet, FLEX1500_COMMAND_PACKET_SIZE, &transferred);
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

static int production_send_command(void *context, const uint8_t *packet,
                                   size_t bytes, size_t *transferred)
{
    flex1500_usb_rx *receiver = context;
    int actual = 0;
    int result = libusb_interrupt_transfer(
        receiver->handle, FLEX1500_EP_COMMAND_OUT, (unsigned char *)packet,
        (int)bytes, &actual, RX_TRANSFER_TIMEOUT_MS);
    if (actual > 0) *transferred = (size_t)actual;
    return result;
}

static void fill_tune_tone(uint8_t *buffer)
{
    const double pi = 3.14159265358979323846;
    const size_t frames = TUNE_TRANSFER_BYTES / 4;
    for (size_t frame = 0; frame < frames; ++frame) {
        double phase = 2.0 * pi * TUNE_TONE_HZ * (double)frame /
                       TUNE_SAMPLE_RATE_HZ;
        int16_t sample_i = (int16_t)lrint(TUNE_MAGNITUDE * cos(phase));
        int16_t sample_q = (int16_t)lrint(-TUNE_MAGNITUDE * sin(phase));
        buffer[frame * 4] = (uint8_t)sample_i;
        buffer[frame * 4 + 1] = (uint8_t)((uint16_t)sample_i >> 8);
        buffer[frame * 4 + 2] = (uint8_t)sample_q;
        buffer[frame * 4 + 3] = (uint8_t)((uint16_t)sample_q >> 8);
    }
}

static int service_usb_for(flex1500_usb_rx *receiver, uint64_t duration_ms)
{
    uint64_t deadline = monotonic_ms() + duration_ms;
    while (monotonic_ms() < deadline) {
        struct timeval timeout = {0, 5000};
        int result = libusb_handle_events_timeout_completed(
            receiver->context, &timeout, NULL);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            set_libusb_error(receiver, "service Tune USB events", result);
            return result;
        }
        if (!receiver->tune_streaming || !receiver->running) {
            return LIBUSB_ERROR_IO;
        }
    }
    return LIBUSB_SUCCESS;
}

static void release_tune_stream(flex1500_usb_rx *receiver)
{
    receiver->tune_streaming = false;
    for (unsigned int index = 0; index < TUNE_TRANSFER_COUNT; ++index) {
        tune_slot *slot = &receiver->tune_slots[index];
        if (slot->submitted) (void)libusb_cancel_transfer(slot->transfer);
    }
    while (receiver->context != NULL && receiver->active_tune_transfers > 0) {
        struct timeval timeout = {0, 5000};
        int result = libusb_handle_events_timeout_completed(
            receiver->context, &timeout, NULL);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            break;
        }
    }
    for (unsigned int index = 0; index < TUNE_TRANSFER_COUNT; ++index) {
        tune_slot *slot = &receiver->tune_slots[index];
        if (slot->transfer != NULL) libusb_free_transfer(slot->transfer);
        free(slot->buffer);
        memset(slot, 0, sizeof(*slot));
    }
    receiver->active_tune_transfers = 0;
    receiver->microphone_streaming = false;
    receiver->microphone_capture_enabled = false;
    receiver->tx_accepting_audio = false;
}

static int start_tune_stream(flex1500_usb_rx *receiver, bool microphone)
{
    receiver->tune_streaming = true;
    receiver->microphone_streaming = microphone;
    receiver->tx_accepting_audio = microphone;
    for (unsigned int index = 0; index < TUNE_TRANSFER_COUNT; ++index) {
        tune_slot *slot = &receiver->tune_slots[index];
        slot->receiver = receiver;
        slot->buffer = malloc(TUNE_TRANSFER_BYTES);
        slot->transfer = libusb_alloc_transfer(TUNE_PACKETS_PER_TRANSFER);
        if (slot->buffer == NULL || slot->transfer == NULL) {
            set_error(receiver, "allocate Tune endpoint-0x01 transfer failed");
            release_tune_stream(receiver);
            return LIBUSB_ERROR_NO_MEM;
        }
        if (microphone) {
            memset(slot->buffer, 0, TUNE_TRANSFER_BYTES);
        } else {
            fill_tune_tone(slot->buffer);
        }
        libusb_fill_iso_transfer(
            slot->transfer, receiver->handle, FLEX1500_EP_SAMPLE_OUT,
            slot->buffer, TUNE_TRANSFER_BYTES, TUNE_PACKETS_PER_TRANSFER,
            tune_complete, slot, 0);
        libusb_set_iso_packet_lengths(slot->transfer,
                                      FLEX1500_SAMPLE_PACKET_SIZE);
        int result = libusb_submit_transfer(slot->transfer);
        if (result != LIBUSB_SUCCESS) {
            set_libusb_error(receiver, "submit Tune endpoint-0x01", result);
            release_tune_stream(receiver);
            return result;
        }
        slot->submitted = true;
        ++receiver->active_tune_transfers;
    }
    return LIBUSB_SUCCESS;
}

static int production_start_tx_stream(void *context, bool generated_audio)
{
    return start_tune_stream(context, generated_audio);
}

static int production_service_tx_stream(void *context, uint64_t duration_ms)
{
    return service_usb_for(context, duration_ms);
}

static void production_stop_tx_stream(void *context)
{
    release_tune_stream(context);
}

static int send_tune_command(flex1500_usb_rx *receiver,
                             const uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE],
                             const char *operation)
{
    return send_rx_command(receiver, packet, operation);
}

typedef struct usb_cleanup_context {
    flex1500_usb_rx *receiver;
    bool preserve_pa_preparation;
} usb_cleanup_context;

static int execute_usb_cleanup_action(flex1500_tx_cleanup_action action,
                                      void *opaque)
{
    usb_cleanup_context *context = opaque;
    flex1500_usb_rx *receiver = context->receiver;
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];

    switch (action) {
    case FLEX1500_TX_CLEANUP_TRANSITION_MUTE:
        flex1500_build_transition_mute_request(receiver->next_command_index++,
                                               true, packet);
        return send_tune_command(receiver, packet,
                                 "TX cleanup transition mute");
    case FLEX1500_TX_CLEANUP_UNKEY:
        flex1500_build_tr_request(receiver->next_command_index++, false,
                                  packet);
        return send_tune_command(receiver, packet, "TX cleanup SET_TR(0)");
    case FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY: {
        if (!receiver->frequency_known) return LIBUSB_SUCCESS;
        flex1500_build_rx_tune_request(receiver->next_command_index++,
                                       receiver->frequency_hz, packet, NULL);
        int result = send_tune_command(receiver, packet,
                                       "TX cleanup restore RX frequency");
        if (result == LIBUSB_SUCCESS && receiver->tune_streaming &&
            receiver->running) {
            result = flex1500_usb_io_service_tx_stream(
                &receiver->io, TUNE_TRANSITION_MS);
        }
        return result;
    }
    case FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE:
        flex1500_build_transition_mute_request(receiver->next_command_index++,
                                               false, packet);
        return send_tune_command(receiver, packet,
                                 "TX cleanup transition unmute");
    case FLEX1500_TX_CLEANUP_RESET_PA_FILTER: {
        uint32_t filter = 0;
        if (context->preserve_pa_preparation && receiver->frequency_known) {
            (void)flex1500_pa_filter_for_frequency(receiver->frequency_hz,
                                                   &filter);
        }
        flex1500_build_pa_filter_request(receiver->next_command_index++, filter,
                                         packet);
        int result = send_tune_command(receiver, packet,
                                       "TX cleanup PA filter");
        receiver->pa_filter = filter;
        receiver->pa_filter_known = context->preserve_pa_preparation;
        return result;
    }
    case FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER:
        flex1500_build_amp_tx1_request(receiver->next_command_index++, false,
                                       packet);
        return send_tune_command(receiver, packet,
                                 "TX cleanup SET_AMP_TX1(0)");
    case FLEX1500_TX_CLEANUP_STOP_STREAM:
        flex1500_usb_io_stop_tx_stream(&receiver->io);
        return LIBUSB_SUCCESS;
    case FLEX1500_TX_CLEANUP_NONE:
        return LIBUSB_SUCCESS;
    }
    return LIBUSB_ERROR_INVALID_PARAM;
}

int flex1500_usb_rx_enable_transmit_preparation(flex1500_usb_rx *receiver)
{
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    if (receiver == NULL || !receiver->running || receiver->handle == NULL ||
        receiver->transmit_prepared) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    if (receiver->frequency_known) {
        uint32_t filter;
        if (!flex1500_pa_filter_for_frequency(receiver->frequency_hz,
                                               &filter) ||
            !flex1500_build_pa_filter_request(
                receiver->next_command_index++, filter, packet)) {
            return LIBUSB_ERROR_INVALID_PARAM;
        }
        int result = send_rx_command(receiver, packet,
                                     "prepare TX SET_PA_FILTER");
        if (result != LIBUSB_SUCCESS) return result;
        receiver->pa_filter = filter;
        receiver->pa_filter_known = true;
    }
    flex1500_build_amp_tx1_request(receiver->next_command_index++, true,
                                   packet);
    receiver->amp_enable_attempted = true;
    int result = send_rx_command(receiver, packet, "prepare TX SET_AMP_TX1(1)");
    if (result != LIBUSB_SUCCESS) {
        if (receiver->pa_filter_known) {
            flex1500_build_pa_filter_request(receiver->next_command_index++, 0,
                                             packet);
            (void)send_rx_command(receiver, packet,
                                  "prepare TX cleanup SET_PA_FILTER(0)");
            receiver->pa_filter_known = false;
        }
        flex1500_build_amp_tx1_request(receiver->next_command_index++, false,
                                       packet);
        (void)send_rx_command(receiver, packet,
                              "prepare TX cleanup SET_AMP_TX1(0)");
        receiver->amp_enable_attempted = false;
        return result;
    }
    receiver->transmit_prepared = true;
    return LIBUSB_SUCCESS;
}

int flex1500_usb_rx_disable_transmit_preparation(flex1500_usb_rx *receiver)
{
    if (receiver == NULL || receiver->handle == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    uint32_t cleanup = flex1500_tx_cleanup_plan(tx_partial_state(receiver));
    if ((cleanup & (FLEX1500_TX_CLEANUP_UNKEY |
                    FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                    FLEX1500_TX_CLEANUP_STOP_STREAM)) != 0) {
        (void)flex1500_usb_rx_tune_carrier_stop(receiver);
        cleanup = flex1500_tx_cleanup_plan(tx_partial_state(receiver));
    }
    if ((cleanup & (FLEX1500_TX_CLEANUP_RESET_PA_FILTER |
                    FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER)) == 0) {
        return LIBUSB_SUCCESS;
    }
    usb_cleanup_context context = {
        .receiver = receiver,
        .preserve_pa_preparation = false,
    };
    int first_error = flex1500_tx_execute_cleanup(
        cleanup & (FLEX1500_TX_CLEANUP_RESET_PA_FILTER |
                   FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER),
        execute_usb_cleanup_action, &context, NULL);
    receiver->transmit_prepared = false;
    receiver->amp_enable_attempted = false;
    receiver->pa_filter_known = false;
    return first_error;
}

bool flex1500_usb_rx_transmit_prepared(const flex1500_usb_rx *receiver)
{
    return receiver != NULL && receiver->transmit_prepared;
}

bool flex1500_usb_rx_pa_filter(const flex1500_usb_rx *receiver,
                               uint32_t *filter)
{
    if (receiver == NULL || filter == NULL || !receiver->pa_filter_known) {
        return false;
    }
    *filter = receiver->pa_filter;
    return true;
}

int flex1500_usb_rx_tune_carrier_start(flex1500_usb_rx *receiver)
{
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t pa_filter;
    uint32_t tune_word;
    if (receiver == NULL || !receiver->running || receiver->handle == NULL ||
        !receiver->transmit_prepared || !receiver->frequency_known ||
        receiver->tune_active ||
        !flex1500_pa_filter_for_frequency(receiver->frequency_hz, &pa_filter) ||
        !flex1500_usb_tune_frequency_to_tuning_word(
            receiver->frequency_hz, (uint32_t)TUNE_TONE_HZ, &tune_word)) {
        if (receiver != NULL) set_error(receiver, "Tune start policy rejected");
        return LIBUSB_ERROR_INVALID_PARAM;
    }

    receiver->tune_prepared = true;
    flex1500_build_pa_filter_request(receiver->next_command_index++, pa_filter,
                                     packet);
    if (send_tune_command(receiver, packet, "Tune SET_PA_FILTER") != 0) goto fail;
    receiver->pa_filter = pa_filter;
    receiver->pa_filter_known = true;
    flex1500_build_amp_tx1_request(receiver->next_command_index++, true, packet);
    if (send_tune_command(receiver, packet, "Tune SET_AMP_TX1(1)") != 0) goto fail;
    if (flex1500_usb_io_start_tx_stream(&receiver->io, false) != 0 ||
        flex1500_usb_io_service_tx_stream(
            &receiver->io, TUNE_PRE_ROLL_MS) != 0) goto fail;
    if (receiver->tx_fault_stage == FLEX1500_TX_FAULT_AFTER_STREAM) {
        set_error(receiver, "injected failure after Tune stream start");
        goto fail;
    }
    flex1500_build_transition_mute_request(receiver->next_command_index++, true,
                                           packet);
    receiver->tune_transition_attempted = true;
    if (send_tune_command(receiver, packet, "Tune transition mute") != 0) goto fail;
    flex1500_build_tuning_word_request(receiver->next_command_index++, tune_word,
                                       packet);
    receiver->tune_frequency_attempted = true;
    if (send_tune_command(receiver, packet, "Tune carrier frequency") != 0) goto fail;
    flex1500_build_tr_request(receiver->next_command_index++, true, packet);
    receiver->tune_key_attempted = true;
    if (send_tune_command(receiver, packet, "Tune SET_TR(1)") != 0) goto fail;
    receiver->tune_keyed = true;
    if (receiver->tx_fault_stage == FLEX1500_TX_FAULT_AFTER_KEY) {
        set_error(receiver, "injected ambiguous failure after SET_TR(1)");
        goto fail;
    }
    if (flex1500_usb_io_service_tx_stream(
            &receiver->io, TUNE_TRANSITION_MS) != 0) goto fail;
    flex1500_build_transition_mute_request(receiver->next_command_index++, false,
                                           packet);
    if (send_tune_command(receiver, packet, "Tune transition unmute") != 0) goto fail;
    receiver->tune_transition_attempted = false;
    receiver->tune_active = true;
    return LIBUSB_SUCCESS;

fail:
    (void)flex1500_usb_rx_tune_carrier_stop(receiver);
    return LIBUSB_ERROR_IO;
}

static int audio_tx_start(flex1500_usb_rx *receiver,
                          flex1500_tx_sideband sideband,
                          unsigned int drive_percent, float microphone_gain,
                          bool compressor_enabled, bool capture_physical,
                          bool raw_iq, const uint8_t *prebuffer, size_t bytes)
{
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t pa_filter;
    uint32_t tx_word;
    if (receiver == NULL || !receiver->running || receiver->handle == NULL ||
        !receiver->transmit_prepared || !receiver->frequency_known ||
        receiver->tune_active || receiver->tune_streaming ||
        !flex1500_pa_filter_for_frequency(receiver->frequency_hz, &pa_filter) ||
        !flex1500_usb_tune_frequency_to_tuning_word(receiver->frequency_hz, 0,
                                                    &tx_word) ||
        !flex1500_tx_audio_stream_init(&receiver->microphone_stream, sideband,
                                       drive_percent, microphone_gain)) {
        if (receiver != NULL) {
            set_error(receiver, "microphone TX start policy rejected");
        }
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    flex1500_tx_audio_stream_set_compressor(&receiver->microphone_stream,
                                            compressor_enabled);
    flex1500_tx_audio_stream_set_raw_iq(&receiver->microphone_stream, raw_iq);
    if (prebuffer != NULL && bytes != 0) {
        size_t accepted = raw_iq
            ? flex1500_tx_audio_stream_push_iq16le(
                  &receiver->microphone_stream, prebuffer, bytes)
            : flex1500_tx_audio_stream_push_pcm16le(
                  &receiver->microphone_stream, prebuffer, bytes);
        if (accepted == 0) {
            set_error(receiver, "network TX prebuffer rejected");
            return LIBUSB_ERROR_INVALID_PARAM;
        }
    }

    receiver->tune_prepared = true;
    flex1500_build_pa_filter_request(receiver->next_command_index++, pa_filter,
                                     packet);
    if (send_tune_command(receiver, packet,
                          "microphone TX SET_PA_FILTER") != 0) goto fail;
    receiver->pa_filter = pa_filter;
    receiver->pa_filter_known = true;
    flex1500_build_amp_tx1_request(receiver->next_command_index++, true, packet);
    if (send_tune_command(receiver, packet,
                          "microphone TX SET_AMP_TX1(1)") != 0) goto fail;
    if (flex1500_usb_io_start_tx_stream(&receiver->io, true) != 0 ||
        flex1500_usb_io_service_tx_stream(
            &receiver->io, TUNE_PRE_ROLL_MS) != 0) goto fail;
    flex1500_build_transition_mute_request(receiver->next_command_index++, true,
                                           packet);
    receiver->tune_transition_attempted = true;
    if (send_tune_command(receiver, packet,
                          "microphone TX transition mute") != 0) goto fail;
    flex1500_build_tuning_word_request(receiver->next_command_index++, tx_word,
                                       packet);
    receiver->tune_frequency_attempted = true;
    if (send_tune_command(receiver, packet,
                          "microphone TX exact frequency") != 0) goto fail;
    flex1500_build_tr_request(receiver->next_command_index++, true, packet);
    receiver->tune_key_attempted = true;
    if (send_tune_command(receiver, packet,
                          "microphone TX SET_TR(1)") != 0) goto fail;
    receiver->tune_keyed = true;
    receiver->microphone_capture_enabled = capture_physical;
    if (flex1500_usb_io_service_tx_stream(
            &receiver->io, TUNE_TRANSITION_MS) != 0) goto fail;
    flex1500_build_transition_mute_request(receiver->next_command_index++, false,
                                           packet);
    if (send_tune_command(receiver, packet,
                          "microphone TX transition unmute") != 0) goto fail;
    receiver->tune_transition_attempted = false;
    receiver->tune_active = true;
    return LIBUSB_SUCCESS;

fail:
    (void)flex1500_usb_rx_tune_carrier_stop(receiver);
    return LIBUSB_ERROR_IO;
}

int flex1500_usb_rx_microphone_tx_start(flex1500_usb_rx *receiver,
                                       flex1500_tx_sideband sideband,
                                       unsigned int drive_percent,
                                       float microphone_gain,
                                       bool compressor_enabled)
{
    return audio_tx_start(receiver, sideband, drive_percent, microphone_gain,
                          compressor_enabled, true, false, NULL, 0);
}

int flex1500_usb_rx_network_tx_start(flex1500_usb_rx *receiver,
                                     flex1500_tx_sideband sideband,
                                     unsigned int drive_percent,
                                     bool raw_iq, const uint8_t *prebuffer,
                                     size_t bytes)
{
    return audio_tx_start(receiver, sideband, drive_percent, 1.0f, false,
                          false, raw_iq, prebuffer, bytes);
}

size_t flex1500_usb_rx_network_tx_push(flex1500_usb_rx *receiver,
                                      const uint8_t *data, size_t bytes,
                                      bool raw_iq)
{
    if (receiver == NULL || !receiver->microphone_streaming ||
        !receiver->tx_accepting_audio ||
        receiver->microphone_capture_enabled ||
        receiver->microphone_stream.raw_iq != raw_iq) return 0;
    return raw_iq
        ? flex1500_tx_audio_stream_push_iq16le(&receiver->microphone_stream,
                                               data, bytes)
        : flex1500_tx_audio_stream_push_pcm16le(&receiver->microphone_stream,
                                                data, bytes);
}

size_t flex1500_usb_rx_network_tx_available(
    const flex1500_usb_rx *receiver, bool raw_iq)
{
    if (receiver == NULL || !receiver->microphone_streaming ||
        !receiver->tx_accepting_audio ||
        receiver->microphone_capture_enabled ||
        receiver->microphone_stream.raw_iq != raw_iq) return 0;
    return flex1500_tx_audio_stream_available(&receiver->microphone_stream);
}

int flex1500_usb_rx_microphone_tx_stop(flex1500_usb_rx *receiver)
{
    return flex1500_usb_rx_tune_carrier_stop(receiver);
}

size_t flex1500_usb_rx_tx_pending_frames(const flex1500_usb_rx *receiver)
{
    if (receiver == NULL || !receiver->microphone_streaming) return 0;
    size_t pending = flex1500_tx_audio_stream_queued(
        &receiver->microphone_stream);
    for (unsigned int index = 0; index < TUNE_TRANSFER_COUNT; ++index) {
        if (receiver->tune_slots[index].submitted) {
            pending += receiver->tune_slots[index].audio_frames;
        }
    }
    return pending;
}

int flex1500_usb_rx_microphone_tx_stop_graceful(
    flex1500_usb_rx *receiver, uint64_t maximum_drain_ms)
{
    if (receiver == NULL || receiver->handle == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    receiver->tx_accepting_audio = false;
    size_t requested = flex1500_usb_rx_tx_pending_frames(receiver);
    receiver->microphone_stream.stats.stop_requested_frames += requested;
    uint64_t started = monotonic_ms();
    while (flex1500_usb_rx_tx_pending_frames(receiver) != 0 &&
           monotonic_ms() - started < maximum_drain_ms) {
        struct timeval timeout = {0, 5000};
        int result = libusb_handle_events_timeout_completed(
            receiver->context, &timeout, NULL);
        if (result != LIBUSB_SUCCESS && result != LIBUSB_ERROR_INTERRUPTED) {
            set_libusb_error(receiver, "graceful TX drain", result);
            break;
        }
        if (!receiver->tune_streaming || !receiver->running) break;
    }
    size_t discarded = flex1500_usb_rx_tx_pending_frames(receiver);
    receiver->microphone_stream.stats.graceful_drained_frames +=
        requested - discarded;
    receiver->microphone_stream.stats.graceful_discarded_frames += discarded;
    receiver->microphone_stream.stats.graceful_drain_ms +=
        monotonic_ms() - started;
    return flex1500_usb_rx_tune_carrier_stop(receiver);
}

const flex1500_tx_audio_stats *flex1500_usb_rx_microphone_tx_stats(
    const flex1500_usb_rx *receiver)
{
    return receiver != NULL
        ? flex1500_tx_audio_stream_stats(&receiver->microphone_stream) : NULL;
}

int flex1500_usb_rx_tune_carrier_stop(flex1500_usb_rx *receiver)
{
    int first_error = LIBUSB_SUCCESS;
    if (receiver == NULL || receiver->handle == NULL) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    if (!receiver->tune_prepared && !receiver->tune_active &&
        !receiver->tune_keyed && !receiver->tune_key_attempted &&
        !tx_stream_present(receiver) && !receiver->tune_transition_attempted &&
        !receiver->tune_frequency_attempted) {
        return LIBUSB_SUCCESS;
    }

    uint32_t cleanup = flex1500_tx_cleanup_plan(tx_partial_state(receiver));

    usb_cleanup_context context = {
        .receiver = receiver,
        .preserve_pa_preparation = receiver->transmit_prepared,
    };
    first_error = flex1500_tx_execute_cleanup(
        cleanup, execute_usb_cleanup_action, &context, NULL);
    receiver->tune_keyed = false;
    receiver->tune_key_attempted = false;
    receiver->tune_active = false;
    receiver->tune_prepared = false;
    receiver->tune_transition_attempted = false;
    receiver->tune_frequency_attempted = false;
    if (tx_stream_present(receiver)) {
        flex1500_usb_io_stop_tx_stream(&receiver->io);
    }
    flex1500_iq_ring_clear(receiver->destination);
    return first_error;
}

bool flex1500_usb_rx_tune_carrier_active(const flex1500_usb_rx *receiver)
{
    return receiver != NULL && receiver->tune_active;
}

void flex1500_usb_rx_set_tx_fault_stage(flex1500_usb_rx *receiver,
                                        flex1500_tx_fault_stage stage)
{
    if (receiver != NULL) receiver->tx_fault_stage = stage;
}

int flex1500_usb_rx_tune(flex1500_usb_rx *receiver, uint32_t frequency_hz)
{
    uint8_t tune[FLEX1500_COMMAND_PACKET_SIZE];
    uint8_t filter_packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint8_t pa_filter_packet[FLEX1500_COMMAND_PACKET_SIZE];
    uint32_t filter;
    uint32_t pa_filter = 0;
    if (receiver == NULL || !receiver->running || receiver->handle == NULL ||
        receiver->tune_active || receiver->tune_keyed) {
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
    uint8_t command_count = receiver->transmit_prepared ? 3 : 2;
    if (receiver->transmit_prepared &&
        (!flex1500_pa_filter_for_frequency(frequency_hz, &pa_filter) ||
         !flex1500_build_pa_filter_request(
             (uint8_t)(receiver->next_command_index + 2), pa_filter,
             pa_filter_packet))) {
        set_error(receiver, "TX PA filter mapping failed during RX tune");
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    receiver->next_command_index =
        (uint8_t)(receiver->next_command_index + command_count);
    int result = send_rx_command(receiver, tune, "send SET_RX1_FREQ_TW");
    if (result != LIBUSB_SUCCESS) return result;
    receiver->frequency_hz = frequency_hz;
    receiver->frequency_known = true;
    receiver->filter_known = false;
    result = send_rx_command(receiver, filter_packet, "send SET_RX1_FILTER");
    if (result != LIBUSB_SUCCESS) return result;
    receiver->rx_filter = filter;
    receiver->filter_known = true;
    if (receiver->transmit_prepared) {
        result = send_rx_command(receiver, pa_filter_packet,
                                 "send mapped SET_PA_FILTER");
        if (result != LIBUSB_SUCCESS) {
            receiver->pa_filter_known = false;
            return result;
        }
        receiver->pa_filter = pa_filter;
        receiver->pa_filter_known = true;
    }
    flex1500_iq_ring_clear(receiver->destination);
    ++receiver->counters.rx_tune_operations;
    return LIBUSB_SUCCESS;
}

int flex1500_usb_rx_set_gain(flex1500_usb_rx *receiver, int32_t gain_db)
{
    uint8_t packet[FLEX1500_COMMAND_PACKET_SIZE];
    if (receiver == NULL || !receiver->running || receiver->handle == NULL ||
        receiver->tune_active || receiver->tune_keyed ||
        gain_db < -10 || gain_db > 30 || gain_db % 10 != 0) {
        return LIBUSB_ERROR_INVALID_PARAM;
    }
    flex1500_rx_gain gain = (flex1500_rx_gain)((gain_db + 10) / 10);
    if (!flex1500_build_rx_gain_request(receiver->next_command_index++, gain,
                                        packet)) return LIBUSB_ERROR_INVALID_PARAM;
    int result = send_rx_command(receiver, packet, "send SET_TRX_PREAMP");
    if (result == LIBUSB_SUCCESS) {
        receiver->gain_db = gain_db;
        receiver->gain_known = true;
    }
    return result;
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

bool flex1500_usb_rx_gain(const flex1500_usb_rx *receiver, int32_t *gain_db)
{
    if (receiver == NULL || gain_db == NULL || !receiver->gain_known) return false;
    *gain_db = receiver->gain_db;
    return true;
}

bool flex1500_usb_rx_physical_inputs(
    const flex1500_usb_rx *receiver, flex1500_physical_inputs *inputs)
{
    if (receiver == NULL || inputs == NULL ||
        !receiver->physical_inputs_known) return false;
    *inputs = receiver->physical_inputs;
    return true;
}
