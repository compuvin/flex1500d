// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_control.h"

#include "test_assert.h"

typedef struct callbacks {
    unsigned int starts;
    unsigned int stops;
    flex1500_tx_owner last_started;
    flex1500_tx_owner last_stopped;
    int start_result;
    int stop_result;
} callbacks;

static int start_owner(void *context, flex1500_tx_owner owner)
{
    callbacks *calls = context;
    ++calls->starts;
    calls->last_started = owner;
    return calls->start_result;
}

static int stop_owner(void *context, flex1500_tx_owner owner)
{
    callbacks *calls = context;
    ++calls->stops;
    calls->last_stopped = owner;
    return calls->stop_result;
}

int main(void)
{
    callbacks calls = {0};
    flex1500_tx_control control;
    flex1500_tx_control_init(&control, true, 30000, &calls, start_owner,
                             stop_owner);

    /* A held PTT at startup cannot key until release is observed. */
    CHECK(control.state == FLEX1500_TX_STATE_STARTUP_INHIBIT);
    CHECK(flex1500_tx_control_physical_ptt(&control, true, 100) ==
          FLEX1500_TX_CONTROL_INHIBITED);
    CHECK(calls.starts == 0 && control.owner == FLEX1500_TX_OWNER_NONE);
    CHECK(flex1500_tx_control_physical_ptt(&control, false, 200) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(control.physical_ptt_armed && control.state == FLEX1500_TX_STATE_RX);

    /* A caller that has positively established unkeyed hardware may arm
     * without waiting for an edge-only status source to report a release. */
    flex1500_tx_control_init(&control, true, 30000, &calls, start_owner,
                             stop_owner);
    flex1500_tx_control_arm_physical_ptt(&control);
    CHECK(control.physical_ptt_armed && control.state == FLEX1500_TX_STATE_RX);
    CHECK(flex1500_tx_control_physical_ptt(&control, true, 300) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(control.owner == FLEX1500_TX_OWNER_PHYSICAL_MIC);
    CHECK(flex1500_tx_control_physical_ptt(&control, false, 400) ==
          FLEX1500_TX_CONTROL_OK);
    calls = (callbacks){0};
    CHECK(flex1500_tx_control_timeout_seconds(&control) == 30);
    CHECK(!flex1500_tx_control_set_timeout_seconds(&control, 29));
    CHECK(!flex1500_tx_control_set_timeout_seconds(&control, 1801));
    CHECK(flex1500_tx_control_set_timeout_seconds(&control, 180));
    CHECK(flex1500_tx_control_timeout_seconds(&control) == 180);

    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_TUNE, 1000) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(!flex1500_tx_control_set_timeout_seconds(&control, 300));
    CHECK(control.owner == FLEX1500_TX_OWNER_TUNE);
    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_HTTP, 1001) ==
          FLEX1500_TX_CONTROL_BUSY);

    /* A non-owner cannot release or otherwise disturb the current owner. */
    CHECK(flex1500_tx_control_release(&control, FLEX1500_TX_OWNER_SOAPY) ==
          FLEX1500_TX_CONTROL_BUSY);
    CHECK(control.owner == FLEX1500_TX_OWNER_TUNE && calls.stops == 0);

    /* Physical PTT safely stops and preempts the remote Tune owner. */
    CHECK(flex1500_tx_control_physical_ptt(&control, true, 2000) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(calls.last_stopped == FLEX1500_TX_OWNER_TUNE);
    CHECK(calls.last_started == FLEX1500_TX_OWNER_PHYSICAL_MIC);
    CHECK(control.owner == FLEX1500_TX_OWNER_PHYSICAL_MIC);
    CHECK(calls.starts == 2 && calls.stops == 1);

    CHECK(flex1500_tx_control_physical_ptt(&control, false, 2500) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(control.owner == FLEX1500_TX_OWNER_NONE);
    CHECK(calls.last_stopped == FLEX1500_TX_OWNER_PHYSICAL_MIC);

    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_SOAPY, 3000) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(flex1500_tx_control_tick(&control, 182999) == FLEX1500_TX_CONTROL_OK);
    CHECK(flex1500_tx_control_tick(&control, 183000) ==
          FLEX1500_TX_CONTROL_MAX_KEY);
    CHECK(control.owner == FLEX1500_TX_OWNER_NONE);

    /* Disconnect handling uses release for the connection-bound owner. */
    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_HTTP,
                                      200000) == FLEX1500_TX_CONTROL_OK);
    CHECK(flex1500_tx_control_release(&control, FLEX1500_TX_OWNER_HTTP) ==
          FLEX1500_TX_CONTROL_OK);
    CHECK(control.owner == FLEX1500_TX_OWNER_NONE);
    CHECK(calls.last_stopped == FLEX1500_TX_OWNER_HTTP);

    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_SOAPY,
                                      210000) == FLEX1500_TX_CONTROL_OK);
    flex1500_tx_control_shutdown(&control);
    CHECK(control.owner == FLEX1500_TX_OWNER_NONE);
    CHECK(calls.last_stopped == FLEX1500_TX_OWNER_SOAPY);

    calls.start_result = -1;
    CHECK(flex1500_tx_control_request(&control, FLEX1500_TX_OWNER_HTTP, 40000) ==
          FLEX1500_TX_CONTROL_HARDWARE_ERROR);
    CHECK(control.state == FLEX1500_TX_STATE_FAULTED);
    CHECK(control.owner == FLEX1500_TX_OWNER_NONE);
    flex1500_tx_control_shutdown(&control);
    return 0;
}
