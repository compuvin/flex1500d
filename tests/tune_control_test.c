// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tune_control.h"

#include "test_assert.h"

typedef struct callbacks {
    int starts;
    int stops;
    int start_result;
    int stop_result;
} callbacks;

static int start_tune(void *context, flex1500_tx_owner owner)
{
    callbacks *state = context;
    CHECK(owner == FLEX1500_TX_OWNER_TUNE);
    ++state->starts;
    return state->start_result;
}

static int stop_tune(void *context, flex1500_tx_owner owner)
{
    callbacks *state = context;
    CHECK(owner == FLEX1500_TX_OWNER_TUNE);
    ++state->stops;
    return state->stop_result;
}

int main(void)
{
    flex1500_tune_control control;
    flex1500_tx_control tx_control;
    callbacks state = {0};
    flex1500_tx_control_init(&tx_control, false, 120000, &state, start_tune,
                             stop_tune);
    flex1500_tune_control_init(&control, false, &tx_control);
    CHECK(flex1500_tune_control_start(&control, 1000, 1) ==
          FLEX1500_TUNE_DISABLED);
    CHECK(state.starts == 0 && state.stops == 0);

    flex1500_tx_control_init(&tx_control, true, 120000, &state, start_tune,
                             stop_tune);
    flex1500_tune_control_init(&control, true, &tx_control);
    CHECK(flex1500_tune_control_start(&control, 1000, 0) ==
          FLEX1500_TUNE_INVALID_LEASE);
    CHECK(flex1500_tune_control_start(&control, 1000, 0x1234) ==
          FLEX1500_TUNE_OK);
    CHECK(control.active && state.starts == 1);
    CHECK(flex1500_tune_control_start(&control, 1001, 0x5678) ==
          FLEX1500_TUNE_BUSY);
    CHECK(flex1500_tune_control_keepalive(&control, 11000, 0x5678) ==
          FLEX1500_TUNE_INVALID_LEASE);
    CHECK(flex1500_tune_control_keepalive(&control, 11000, 0x1234) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_tick(&control, 25999) == FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_tick(&control, 26000) ==
          FLEX1500_TUNE_EXPIRED);
    CHECK(!control.active && state.stops == 1);

    CHECK(flex1500_tune_control_start(&control, 100000, 7) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_keepalive(&control, 114000, 7) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_keepalive(&control, 128000, 7) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_keepalive(&control, 142000, 7) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_keepalive(&control, 156000, 7) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tune_control_tick(&control, 160000) ==
          FLEX1500_TUNE_HARD_LIMIT);
    CHECK(!control.active && state.stops == 2);

    /* A shared-owner timeout or preemption must not leave Tune stale. */
    CHECK(flex1500_tune_control_start(&control, 170000, 70) ==
          FLEX1500_TUNE_OK);
    CHECK(flex1500_tx_control_tick(&tx_control, 290000) ==
          FLEX1500_TX_CONTROL_MAX_KEY);
    CHECK(control.active);
    flex1500_tune_control_reconcile(&control, true);
    CHECK(!control.active && control.lease == 0);
    CHECK(state.stops == 3);
    flex1500_tune_control_reconcile(&control, true);
    CHECK(state.stops == 3);

    state.start_result = -1;
    CHECK(flex1500_tune_control_start(&control, 200000, 8) ==
          FLEX1500_TUNE_HARDWARE_ERROR);
    CHECK(!control.active && state.starts == 4 && state.stops == 4);
    state.start_result = 0;
    flex1500_tx_control_init(&tx_control, true, 120000, &state, start_tune,
                             stop_tune);
    CHECK(flex1500_tune_control_start(&control, 300000, 9) ==
          FLEX1500_TUNE_OK);
    flex1500_tune_control_shutdown(&control);
    CHECK(!control.active && state.stops == 5);
    flex1500_tune_control_shutdown(&control);
    CHECK(state.stops == 5);
    CHECK(flex1500_tune_control_stop(&control, 9) ==
          FLEX1500_TUNE_INVALID_LEASE);
    const flex1500_tx_diagnostics *diagnostics =
        flex1500_tune_control_diagnostics(&control);
    CHECK(diagnostics != NULL);
    CHECK(diagnostics->starts == 4);
    CHECK(diagnostics->stops == 4);
    CHECK(diagnostics->underruns == 0);
    CHECK(diagnostics->rejected_ownership_requests == 3);
    CHECK(diagnostics->watchdog_stops == 3);
    CHECK(diagnostics->cleanup_failures == 0);
    flex1500_tune_control_record_underrun(&control, 4);
    flex1500_tune_control_record_audio_quality(&control, 5, 6);
    flex1500_tune_control_record_cleanup_failure(&control);
    CHECK(diagnostics->underruns == 4);
    CHECK(diagnostics->clipped_frames == 5);
    CHECK(diagnostics->dropped_microphone_frames == 6);
    CHECK(diagnostics->cleanup_failures == 1);
    return 0;
}
