// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/publisher.h"

#include "test_assert.h"
#include <string.h>

typedef struct test_writer {
    uint8_t bytes[4096];
    size_t length;
    size_t max_write;
    flex1500_publish_result forced_result;
} test_writer;

static flex1500_publish_result write_test_bytes(
    void *context, const uint8_t *bytes, size_t length, size_t *written)
{
    test_writer *writer = context;
    if (writer->forced_result != FLEX1500_PUBLISH_OK) {
        *written = 0;
        return writer->forced_result;
    }
    size_t count = length;
    if (count > writer->max_write) count = writer->max_write;
    memcpy(&writer->bytes[writer->length], bytes, count);
    writer->length += count;
    *written = count;
    return FLEX1500_PUBLISH_OK;
}

int main(void)
{
    flex1500_iq_ring ring;
    flex1500_iq_publisher publisher;
    flex1500_iq_sample samples[4] = {
        {1.0f, 2.0f}, {3.0f, 4.0f}, {5.0f, 6.0f}, {7.0f, 8.0f},
    };
    test_writer writer = {.max_write = 7,
                          .forced_result = FLEX1500_PUBLISH_OK};

    CHECK(flex1500_iq_ring_init(&ring, 8));
    CHECK(flex1500_iq_publisher_init(&publisher, &ring, 2));
    CHECK(flex1500_iq_ring_push(&ring, samples, 4) == 4);

    CHECK(flex1500_iq_publisher_pump(&publisher, write_test_bytes, &writer) ==
           FLEX1500_PUBLISH_OK);
    CHECK(publisher.pending_offset == 7);
    CHECK(ring.size == 2);

    writer.forced_result = FLEX1500_PUBLISH_WOULD_BLOCK;
    CHECK(flex1500_iq_publisher_pump(&publisher, write_test_bytes, &writer) ==
           FLEX1500_PUBLISH_WOULD_BLOCK);
    CHECK(publisher.pending_offset == 7);

    writer.forced_result = FLEX1500_PUBLISH_OK;
    while (publisher.stats.frames_sent == 0) {
        CHECK(flex1500_iq_publisher_pump(&publisher, write_test_bytes, &writer) ==
               FLEX1500_PUBLISH_OK);
    }
    CHECK(publisher.stats.samples_sent == 2);
    CHECK(memcmp(writer.bytes, "F15I", 4) == 0);

    writer.forced_result = FLEX1500_PUBLISH_DISCONNECTED;
    CHECK(flex1500_iq_publisher_pump(&publisher, write_test_bytes, &writer) ==
           FLEX1500_PUBLISH_DISCONNECTED);
    CHECK(publisher.pending_length == 0);
    CHECK(publisher.next_sequence == 2);
    CHECK(publisher.stats.disconnects == 1);
    CHECK(ring.size == 0);

    flex1500_iq_ring_destroy(&ring);
    return 0;
}
