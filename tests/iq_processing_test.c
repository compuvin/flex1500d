// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/iq.h"

#include "test_assert.h"
#include <stdint.h>

int main(void)
{
    const uint8_t input[] = {
        100, 0, 20, 0,
        101, 0, 19, 0,
        99, 0, 21, 0,
        100, 0, 20, 0,
    };
    flex1500_iq_sample output[4];
    flex1500_iq_sample popped[3];
    flex1500_iq_stats stats;
    flex1500_dc_blocker blocker;
    flex1500_iq_ring ring;

    flex1500_iq_stats_reset(&stats);
    flex1500_dc_blocker_init(&blocker, 0.25f);
    CHECK(flex1500_process_iq16le(input, sizeof(input), output, 4, &stats,
                                   &blocker) == 4);
    CHECK(stats.frames == 4);
    CHECK(stats.sentinel_frames == 0);
    CHECK(stats.min_i == 99 && stats.max_i == 101);
    CHECK(stats.min_q == 19 && stats.max_q == 21);
    CHECK(stats.mean_i == 100.0);
    CHECK(stats.mean_q == 20.0);
    CHECK(output[0].i == 0.0f && output[0].q == 0.0f);

    CHECK(flex1500_iq_ring_init(&ring, 3));
    CHECK(flex1500_iq_ring_push(&ring, output, 4) == 3);
    CHECK(ring.dropped == 1);

    CHECK(flex1500_iq_ring_pop(&ring, popped, 2) == 2);
    CHECK(flex1500_iq_ring_push(&ring, &output[3], 1) == 1);
    CHECK(flex1500_iq_ring_pop(&ring, popped, 3) == 2);
    CHECK(flex1500_iq_ring_push(&ring, output, 2) == 2);
    uint64_t dropped_before_clear = ring.dropped;
    flex1500_iq_ring_clear(&ring);
    CHECK(ring.size == 0);
    CHECK(ring.read_index == 0);
    CHECK(ring.dropped == dropped_before_clear);
    flex1500_iq_ring_destroy(&ring);
    return 0;
}
