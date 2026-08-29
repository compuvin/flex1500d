// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_IQ_H
#define FLEX1500_IQ_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct flex1500_iq_sample {
    float i;
    float q;
} flex1500_iq_sample;

typedef struct flex1500_iq_stats {
    uint64_t frames;
    uint64_t sentinel_frames;
    int16_t min_i;
    int16_t max_i;
    int16_t min_q;
    int16_t max_q;
    double mean_i;
    double mean_q;
    bool have_samples;
} flex1500_iq_stats;

typedef struct flex1500_dc_blocker {
    float estimate_i;
    float estimate_q;
    float alpha;
    bool initialized;
} flex1500_dc_blocker;

typedef struct flex1500_iq_ring {
    flex1500_iq_sample *samples;
    size_t capacity;
    size_t read_index;
    size_t size;
    uint64_t dropped;
} flex1500_iq_ring;

void flex1500_iq_stats_reset(flex1500_iq_stats *stats);
void flex1500_dc_blocker_init(flex1500_dc_blocker *blocker, float alpha);

size_t flex1500_process_iq16le(const uint8_t *bytes, size_t byte_count,
                               flex1500_iq_sample *output,
                               size_t output_capacity,
                               flex1500_iq_stats *stats,
                               flex1500_dc_blocker *blocker);

bool flex1500_iq_ring_init(flex1500_iq_ring *ring, size_t capacity);
void flex1500_iq_ring_destroy(flex1500_iq_ring *ring);
size_t flex1500_iq_ring_push(flex1500_iq_ring *ring,
                             const flex1500_iq_sample *samples, size_t count);
size_t flex1500_iq_ring_pop(flex1500_iq_ring *ring,
                            flex1500_iq_sample *samples, size_t count);
/* Discard queued samples while preserving lifetime drop accounting. */
void flex1500_iq_ring_clear(flex1500_iq_ring *ring);

#endif
