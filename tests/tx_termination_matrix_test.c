// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network_tx.h"
#include "flex1500/tx_control.h"
#include "flex1500/tx_lifecycle.h"

#include "test_assert.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    ALL_PARTIAL = FLEX1500_TX_PARTIAL_PA_SELECTED |
                  FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED |
                  FLEX1500_TX_PARTIAL_STREAM_STARTED |
                  FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED |
                  FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED |
                  FLEX1500_TX_PARTIAL_KEY_ATTEMPTED,
    ALL_CLEANUP = FLEX1500_TX_CLEANUP_TRANSITION_MUTE |
                  FLEX1500_TX_CLEANUP_UNKEY |
                  FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                  FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE |
                  FLEX1500_TX_CLEANUP_STOP_STREAM |
                  FLEX1500_TX_CLEANUP_RESET_PA_FILTER |
                  FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER,
};

typedef struct harness {
    uint32_t partial;
    uint32_t attempted;
    uint32_t called;
    uint32_t fail_action;
    unsigned int starts;
    unsigned int stops;
    bool fail_start;
    bool graceful;
} harness;

static int cleanup_action(flex1500_tx_cleanup_action action, void *context)
{
    harness *state = context;
    state->called |= (uint32_t)action;
    return (uint32_t)action == state->fail_action ? -17 : 0;
}

static int start_owner(void *context, flex1500_tx_owner owner)
{
    harness *state = context;
    CHECK(owner != FLEX1500_TX_OWNER_NONE);
    ++state->starts;
    state->partial = ALL_PARTIAL;
    return state->fail_start ? -17 : 0;
}

static int stop_owner(void *context, flex1500_tx_owner owner, bool graceful)
{
    harness *state = context;
    CHECK(owner != FLEX1500_TX_OWNER_NONE);
    ++state->stops;
    state->graceful = graceful;
    uint32_t plan = flex1500_tx_cleanup_plan(state->partial);
    int result = flex1500_tx_execute_cleanup(
        plan, cleanup_action, state, &state->attempted);
    state->partial = FLEX1500_TX_PARTIAL_NONE;
    return result;
}

static void expect_cleanup(const harness *state, bool graceful)
{
    CHECK(state->stops == 1);
    CHECK(state->attempted == ALL_CLEANUP);
    CHECK(state->called == ALL_CLEANUP);
    CHECK(state->graceful == graceful);
}

static void init_control(flex1500_tx_control *control, harness *state,
                         uint64_t maximum_key_ms)
{
    *state = (harness){0};
    flex1500_tx_control_init(control, true, maximum_key_ms, state,
                             start_owner, stop_owner);
    flex1500_tx_control_arm_physical_ptt(control);
}

static void start_network(flex1500_tx_control *control,
                          flex1500_network_tx *session, uint64_t lease,
                          uint64_t now_ms)
{
    flex1500_network_tx_profile profile = {
        FLEX1500_NETWORK_TX_USB, FLEX1500_NETWORK_TX_AUDIO, 50, 48000};
    flex1500_network_tx_init(session, true, control);
    CHECK(flex1500_network_tx_acquire(session, &profile, lease, now_ms) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_attach_stream(session, lease, now_ms + 1) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_record_data(
              session, lease, FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES,
              now_ms + 2) == FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_ptt_start(session, lease, now_ms + 3) ==
          FLEX1500_NETWORK_TX_OK);
}

int main(void)
{
    harness state;
    flex1500_tx_control control;
    flex1500_network_tx session;

    /* An explicit operator stop drains gracefully, then performs full cleanup. */
    init_control(&control, &state, 30000);
    start_network(&control, &session, 1, 100);
    CHECK(flex1500_network_tx_ptt_stop(&session, 1) ==
          FLEX1500_NETWORK_TX_OK);
    expect_cleanup(&state, true);

    /* A stream error/client disconnect must unkey immediately. */
    init_control(&control, &state, 30000);
    start_network(&control, &session, 2, 200);
    CHECK(flex1500_network_tx_disconnect_stream(&session) ==
          FLEX1500_NETWORK_TX_OK);
    expect_cleanup(&state, false);

    /* Missing sample data is a fatal stream watchdog event. */
    init_control(&control, &state, 30000);
    start_network(&control, &session, 3, 300);
    CHECK(flex1500_network_tx_tick(
              &session, 303 + FLEX1500_NETWORK_TX_DATA_MS) ==
          FLEX1500_NETWORK_TX_DATA_TIMEOUT);
    expect_cleanup(&state, false);

    /* Lease expiry also unkeys even when sample data is still arriving. */
    init_control(&control, &state, 30000);
    start_network(&control, &session, 4, 400);
    CHECK(flex1500_network_tx_record_data(
              &session, 4, 1, 403 + FLEX1500_NETWORK_TX_LEASE_MS - 1) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_tick(
              &session, 403 + FLEX1500_NETWORK_TX_LEASE_MS) ==
          FLEX1500_NETWORK_TX_EXPIRED);
    expect_cleanup(&state, false);

    /* The shared maximum-key timer uses the same immediate cleanup. */
    init_control(&control, &state, 30000);
    start_network(&control, &session, 5, 500);
    CHECK(flex1500_tx_control_tick(&control, 503 + 30000) ==
          FLEX1500_TX_CONTROL_MAX_KEY);
    expect_cleanup(&state, false);

    /* SIGINT, SIGTERM, and normal process exit converge on shutdown(). */
    init_control(&control, &state, 30000);
    start_network(&control, &session, 6, 600);
    flex1500_tx_control_shutdown(&control);
    expect_cleanup(&state, false);

    /* Physical PTT release is graceful but uses the identical safety plan. */
    init_control(&control, &state, 30000);
    CHECK(flex1500_tx_control_physical_ptt(&control, true, 700) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(flex1500_tx_control_physical_ptt(&control, false, 701) ==
          FLEX1500_TX_CONTROL_OK);
    expect_cleanup(&state, true);

    /* A partial start failure invokes cleanup before reporting faulted. */
    init_control(&control, &state, 30000);
    state.fail_start = true;
    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_HTTP, 800) ==
          FLEX1500_TX_CONTROL_HARDWARE_ERROR);
    expect_cleanup(&state, false);
    CHECK(control.state == FLEX1500_TX_STATE_FAULTED);
    CHECK(control.owner == FLEX1500_TX_OWNER_NONE);

    /* A cleanup write failure must not suppress any later cleanup action. */
    for (uint32_t failed = FLEX1500_TX_CLEANUP_TRANSITION_MUTE;
         failed <= FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER; failed <<= 1) {
        init_control(&control, &state, 30000);
        state.fail_action = failed;
        start_network(&control, &session, 9, 900);
        CHECK(flex1500_network_tx_disconnect_stream(&session) ==
              FLEX1500_NETWORK_TX_HARDWARE_ERROR);
        expect_cleanup(&state, false);
        CHECK(control.cleanup_failed);
        CHECK(control.state == FLEX1500_TX_STATE_FAULTED);
    }

    return 0;
}
