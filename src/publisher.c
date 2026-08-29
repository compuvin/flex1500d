// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/publisher.h"

#include "flex1500/network.h"

#include <string.h>

static bool prepare_frame(flex1500_iq_publisher *publisher)
{
    if (publisher->pending_offset < publisher->pending_length) return true;
    if (publisher->source->size == 0) return false;

    flex1500_iq_sample samples[FLEX1500_PUBLISHER_MAX_SAMPLES];
    uint32_t count = publisher->samples_per_frame;
    if (count > publisher->source->size) count = (uint32_t)publisher->source->size;
    count = (uint32_t)flex1500_iq_ring_pop(publisher->source, samples, count);

    publisher->pending_length = flex1500_encode_iq_frame(
        publisher->next_sequence, samples, count, publisher->pending,
        sizeof(publisher->pending));
    if (publisher->pending_length == 0) return false;
    publisher->pending_offset = 0;
    publisher->pending_samples = count;
    ++publisher->next_sequence;
    ++publisher->stats.frames_encoded;
    return true;
}

bool flex1500_iq_publisher_init(flex1500_iq_publisher *publisher,
                                flex1500_iq_ring *source,
                                uint32_t samples_per_frame)
{
    if (publisher == NULL || source == NULL || source->samples == NULL ||
        samples_per_frame == 0 ||
        samples_per_frame > FLEX1500_PUBLISHER_MAX_SAMPLES) {
        return false;
    }
    memset(publisher, 0, sizeof(*publisher));
    publisher->source = source;
    publisher->samples_per_frame = samples_per_frame;
    return true;
}

flex1500_publish_result flex1500_iq_publisher_pump(
    flex1500_iq_publisher *publisher, flex1500_publish_writer writer,
    void *writer_context)
{
    if (publisher == NULL || writer == NULL) return FLEX1500_PUBLISH_ERROR;
    if (!prepare_frame(publisher)) return FLEX1500_PUBLISH_EMPTY;

    size_t remaining = publisher->pending_length - publisher->pending_offset;
    size_t written = 0;
    flex1500_publish_result result = writer(
        writer_context, &publisher->pending[publisher->pending_offset], remaining,
        &written);

    if (result == FLEX1500_PUBLISH_WOULD_BLOCK) {
        ++publisher->stats.would_block_events;
        return result;
    }
    if (result == FLEX1500_PUBLISH_DISCONNECTED) {
        ++publisher->stats.disconnects;
        flex1500_iq_publisher_disconnect(publisher);
        return result;
    }
    if (result != FLEX1500_PUBLISH_OK || written == 0 || written > remaining) {
        ++publisher->stats.write_errors;
        return FLEX1500_PUBLISH_ERROR;
    }

    publisher->pending_offset += written;
    if (publisher->pending_offset == publisher->pending_length) {
        ++publisher->stats.frames_sent;
        publisher->stats.samples_sent += publisher->pending_samples;
        publisher->pending_length = 0;
        publisher->pending_offset = 0;
        publisher->pending_samples = 0;
    }
    return FLEX1500_PUBLISH_OK;
}

void flex1500_iq_publisher_disconnect(flex1500_iq_publisher *publisher)
{
    if (publisher == NULL) return;
    publisher->pending_length = 0;
    publisher->pending_offset = 0;
    publisher->pending_samples = 0;
}
