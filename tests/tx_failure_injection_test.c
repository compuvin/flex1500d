// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_lifecycle.h"
#include "flex1500/usb_io.h"

#include "test_assert.h"

#include <stdbool.h>
#include <stdint.h>

enum { START_OPERATIONS = 9, TOTAL_OPERATIONS = 16 };

typedef struct fault_io {
    unsigned int operation;
    unsigned int fail_operation;
    uint32_t cleanup_called;
    unsigned int stops;
} fault_io;

static int next_result(fault_io *io)
{
    ++io->operation;
    return io->operation == io->fail_operation ? -17 : 0;
}

static int send_command(void *context, const uint8_t *packet, size_t bytes,
                        size_t *transferred)
{
    fault_io *io = context;
    (void)packet;
    if (next_result(io) != 0) return -17;
    *transferred = bytes;
    return 0;
}

static int start_stream(void *context, bool dynamic_samples)
{
    (void)dynamic_samples;
    return next_result(context);
}

static int service_stream(void *context, uint64_t duration_ms)
{
    (void)duration_ms;
    return next_result(context);
}

static void stop_stream(void *context)
{
    fault_io *io = context;
    ++io->operation;
    ++io->stops;
}

static const flex1500_usb_io_ops OPS = {
    send_command, start_stream, service_stream, stop_stream};

static int command(flex1500_usb_io *usb)
{
    const uint8_t packet[20] = {0x7e, 0x04};
    size_t transferred = 0;
    int result = flex1500_usb_io_send_command(
        usb, packet, sizeof(packet), &transferred);
    return result != 0 || transferred != sizeof(packet) ? -17 : 0;
}

static int start_sequence(flex1500_usb_io *usb, uint32_t *partial)
{
    *partial |= FLEX1500_TX_PARTIAL_PA_SELECTED;
    if (command(usb) != 0) return -17;
    *partial |= FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED;
    if (command(usb) != 0) return -17;
    *partial |= FLEX1500_TX_PARTIAL_STREAM_STARTED;
    if (flex1500_usb_io_start_tx_stream(usb, true) != 0) return -17;
    if (flex1500_usb_io_service_tx_stream(usb, 250) != 0) return -17;
    *partial |= FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED;
    if (command(usb) != 0) return -17;
    *partial |= FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED;
    if (command(usb) != 0) return -17;
    *partial |= FLEX1500_TX_PARTIAL_KEY_ATTEMPTED;
    if (command(usb) != 0) return -17;
    if (flex1500_usb_io_service_tx_stream(usb, 200) != 0) return -17;
    if (command(usb) != 0) return -17;
    return 0;
}

typedef struct cleanup_context {
    flex1500_usb_io *usb;
    fault_io *fault;
} cleanup_context;

static int cleanup(flex1500_tx_cleanup_action action, void *context)
{
    cleanup_context *cleanup_context = context;
    cleanup_context->fault->cleanup_called |= (uint32_t)action;
    if (action == FLEX1500_TX_CLEANUP_STOP_STREAM) {
        flex1500_usb_io_stop_tx_stream(cleanup_context->usb);
        return 0;
    }
    return command(cleanup_context->usb);
}

static void run_case(unsigned int failure)
{
    fault_io fault = {.fail_operation = failure};
    flex1500_usb_io usb;
    CHECK(flex1500_usb_io_init(&usb, &OPS, &fault));
    uint32_t partial = FLEX1500_TX_PARTIAL_NONE;
    int start_result = start_sequence(&usb, &partial);
    CHECK((failure != 0 && failure <= START_OPERATIONS) ==
          (start_result != 0));

    uint32_t plan = flex1500_tx_cleanup_plan(partial);
    cleanup_context context = {.usb = &usb, .fault = &fault};
    uint32_t attempted = 0;
    int cleanup_result = flex1500_tx_execute_cleanup(
        plan, cleanup, &context, &attempted);
    CHECK(attempted == plan);
    CHECK(fault.cleanup_called == plan);
    if (failure > START_OPERATIONS && failure <= TOTAL_OPERATIONS) {
        CHECK(cleanup_result == -17 || failure == TOTAL_OPERATIONS);
    }
    if ((plan & FLEX1500_TX_CLEANUP_STOP_STREAM) != 0) {
        CHECK(fault.stops == 1);
    }
}

int main(void)
{
    /* Fail each start and cleanup I/O operation exactly once. */
    for (unsigned int failure = 1; failure <= TOTAL_OPERATIONS; ++failure) {
        run_case(failure);
    }
    run_case(0);
    return 0;
}
