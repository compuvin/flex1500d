// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_lifecycle.h"

#include "test_assert.h"

static void expect_plan(uint32_t state, uint32_t expected)
{
    CHECK(flex1500_tx_cleanup_plan(state) == expected);
}

typedef struct mock_cleanup {
    uint32_t called;
    uint32_t fail_action;
    unsigned int calls;
} mock_cleanup;

static int mock_action(flex1500_tx_cleanup_action action, void *context)
{
    mock_cleanup *mock = context;
    mock->called |= (uint32_t)action;
    ++mock->calls;
    return (uint32_t)action == mock->fail_action ? -17 : 0;
}

int main(void)
{
    expect_plan(FLEX1500_TX_PARTIAL_NONE, FLEX1500_TX_CLEANUP_NONE);
    expect_plan(FLEX1500_TX_PARTIAL_PA_SELECTED,
                FLEX1500_TX_CLEANUP_RESET_PA_FILTER);
    expect_plan(FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED,
                FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER);
    expect_plan(FLEX1500_TX_PARTIAL_STREAM_STARTED,
                FLEX1500_TX_CLEANUP_STOP_STREAM);
    expect_plan(FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED,
                FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                    FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE);
    expect_plan(FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED,
                FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                    FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE);
    expect_plan(FLEX1500_TX_PARTIAL_KEY_ATTEMPTED,
                FLEX1500_TX_CLEANUP_TRANSITION_MUTE |
                    FLEX1500_TX_CLEANUP_UNKEY);

    const uint32_t all = FLEX1500_TX_PARTIAL_PA_SELECTED |
                         FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED |
                         FLEX1500_TX_PARTIAL_STREAM_STARTED |
                         FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED |
                         FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED |
                         FLEX1500_TX_PARTIAL_KEY_ATTEMPTED;
    const uint32_t all_cleanup = FLEX1500_TX_CLEANUP_TRANSITION_MUTE |
                                 FLEX1500_TX_CLEANUP_UNKEY |
                                 FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                                 FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE |
                                 FLEX1500_TX_CLEANUP_STOP_STREAM |
                                 FLEX1500_TX_CLEANUP_RESET_PA_FILTER |
                                 FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER;
    expect_plan(all, all_cleanup);

    /* Every possible partial-state combination may only add cleanup. */
    for (uint32_t state = 0; state <= all; ++state) {
        uint32_t plan = flex1500_tx_cleanup_plan(state);
        if ((state & FLEX1500_TX_PARTIAL_KEY_ATTEMPTED) != 0) {
            CHECK((plan & FLEX1500_TX_CLEANUP_UNKEY) != 0);
        }
        if ((state & FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED) != 0) {
            CHECK((plan & FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER) != 0);
        }
        if ((state & FLEX1500_TX_PARTIAL_STREAM_STARTED) != 0) {
            CHECK((plan & FLEX1500_TX_CLEANUP_STOP_STREAM) != 0);
        }
    }

    /* A failed cleanup write must never suppress any later safety action. */
    for (uint32_t failed = FLEX1500_TX_CLEANUP_TRANSITION_MUTE;
         failed <= FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER; failed <<= 1) {
        mock_cleanup mock = {.fail_action = failed};
        uint32_t attempted = 0;
        CHECK(flex1500_tx_execute_cleanup(all_cleanup, mock_action, &mock,
                                          &attempted) == -17);
        CHECK(mock.called == all_cleanup);
        CHECK(attempted == all_cleanup);
        CHECK(mock.calls == 7);
    }

    mock_cleanup successful = {0};
    uint32_t attempted = 0;
    CHECK(flex1500_tx_execute_cleanup(all_cleanup, mock_action, &successful,
                                      &attempted) == 0);
    CHECK(successful.called == all_cleanup);
    CHECK(attempted == all_cleanup);
    CHECK(successful.calls == 7);
    return 0;
}
