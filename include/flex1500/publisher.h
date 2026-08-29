// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_PUBLISHER_H
#define FLEX1500_PUBLISHER_H

#include "flex1500/iq.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLEX1500_PUBLISHER_MAX_SAMPLES = 256,
    FLEX1500_PUBLISHER_BUFFER_SIZE =
        20 + FLEX1500_PUBLISHER_MAX_SAMPLES * 8,
};

typedef enum flex1500_publish_result {
    FLEX1500_PUBLISH_OK,
    FLEX1500_PUBLISH_EMPTY,
    FLEX1500_PUBLISH_WOULD_BLOCK,
    FLEX1500_PUBLISH_DISCONNECTED,
    FLEX1500_PUBLISH_ERROR,
} flex1500_publish_result;

typedef flex1500_publish_result (*flex1500_publish_writer)(
    void *context, const uint8_t *bytes, size_t length, size_t *written);

typedef struct flex1500_publisher_stats {
    uint64_t frames_encoded;
    uint64_t frames_sent;
    uint64_t samples_sent;
    uint64_t would_block_events;
    uint64_t disconnects;
    uint64_t write_errors;
} flex1500_publisher_stats;

typedef struct flex1500_iq_publisher {
    flex1500_iq_ring *source;
    uint32_t next_sequence;
    uint32_t samples_per_frame;
    uint8_t pending[FLEX1500_PUBLISHER_BUFFER_SIZE];
    size_t pending_length;
    size_t pending_offset;
    uint32_t pending_samples;
    flex1500_publisher_stats stats;
} flex1500_iq_publisher;

bool flex1500_iq_publisher_init(flex1500_iq_publisher *publisher,
                                flex1500_iq_ring *source,
                                uint32_t samples_per_frame);

flex1500_publish_result flex1500_iq_publisher_pump(
    flex1500_iq_publisher *publisher, flex1500_publish_writer writer,
    void *writer_context);

/* Drop only the unsent network frame; source-ring samples are untouched. */
void flex1500_iq_publisher_disconnect(flex1500_iq_publisher *publisher);

#endif
