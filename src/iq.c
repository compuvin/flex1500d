// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/iq.h"
#include "flex1500/protocol.h"

#include <stdlib.h>
#include <string.h>

void flex1500_iq_stats_reset(flex1500_iq_stats *stats)
{
    memset(stats, 0, sizeof(*stats));
}

void flex1500_dc_blocker_init(flex1500_dc_blocker *blocker, float alpha)
{
    blocker->estimate_i = 0.0f;
    blocker->estimate_q = 0.0f;
    blocker->alpha = alpha;
    blocker->initialized = false;
}

static void update_stats(flex1500_iq_stats *stats, int16_t sample_i,
                         int16_t sample_q)
{
    ++stats->frames;
    if (sample_i == -1 && sample_q == -1) ++stats->sentinel_frames;

    if (!stats->have_samples) {
        stats->min_i = stats->max_i = sample_i;
        stats->min_q = stats->max_q = sample_q;
        stats->mean_i = sample_i;
        stats->mean_q = sample_q;
        stats->have_samples = true;
        return;
    }

    if (sample_i < stats->min_i) stats->min_i = sample_i;
    if (sample_i > stats->max_i) stats->max_i = sample_i;
    if (sample_q < stats->min_q) stats->min_q = sample_q;
    if (sample_q > stats->max_q) stats->max_q = sample_q;

    stats->mean_i += ((double)sample_i - stats->mean_i) / (double)stats->frames;
    stats->mean_q += ((double)sample_q - stats->mean_q) / (double)stats->frames;
}

size_t flex1500_process_iq16le(const uint8_t *bytes, size_t byte_count,
                               flex1500_iq_sample *output,
                               size_t output_capacity,
                               flex1500_iq_stats *stats,
                               flex1500_dc_blocker *blocker)
{
    size_t frame_count = byte_count / 4;
    if (frame_count > output_capacity) frame_count = output_capacity;

    for (size_t frame = 0; frame < frame_count; ++frame) {
        int16_t sample_i;
        int16_t sample_q;
        flex1500_decode_iq_frame(&bytes[frame * 4], &sample_i, &sample_q);
        update_stats(stats, sample_i, sample_q);

        if (!blocker->initialized) {
            blocker->estimate_i = sample_i;
            blocker->estimate_q = sample_q;
            blocker->initialized = true;
        } else {
            blocker->estimate_i +=
                blocker->alpha * ((float)sample_i - blocker->estimate_i);
            blocker->estimate_q +=
                blocker->alpha * ((float)sample_q - blocker->estimate_q);
        }

        output[frame].i = (float)sample_i - blocker->estimate_i;
        output[frame].q = (float)sample_q - blocker->estimate_q;
    }
    return frame_count;
}

bool flex1500_iq_ring_init(flex1500_iq_ring *ring, size_t capacity)
{
    memset(ring, 0, sizeof(*ring));
    if (capacity == 0) return false;
    ring->samples = calloc(capacity, sizeof(*ring->samples));
    if (ring->samples == NULL) return false;
    ring->capacity = capacity;
    return true;
}

void flex1500_iq_ring_destroy(flex1500_iq_ring *ring)
{
    free(ring->samples);
    memset(ring, 0, sizeof(*ring));
}

size_t flex1500_iq_ring_push(flex1500_iq_ring *ring,
                             const flex1500_iq_sample *samples, size_t count)
{
    size_t accepted = count;
    size_t available = ring->capacity - ring->size;
    if (accepted > available) {
        ring->dropped += accepted - available;
        accepted = available;
    }

    for (size_t index = 0; index < accepted; ++index) {
        size_t write_index =
            (ring->read_index + ring->size + index) % ring->capacity;
        ring->samples[write_index] = samples[index];
    }
    ring->size += accepted;
    return accepted;
}

size_t flex1500_iq_ring_pop(flex1500_iq_ring *ring,
                            flex1500_iq_sample *samples, size_t count)
{
    if (count > ring->size) count = ring->size;
    for (size_t index = 0; index < count; ++index) {
        samples[index] = ring->samples[(ring->read_index + index) % ring->capacity];
    }
    ring->read_index = (ring->read_index + count) % ring->capacity;
    ring->size -= count;
    return count;
}

void flex1500_iq_ring_clear(flex1500_iq_ring *ring)
{
    if (ring == NULL) return;
    ring->read_index = 0;
    ring->size = 0;
}
