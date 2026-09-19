// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network_tx.h"
#include "test_assert.h"

typedef struct calls { unsigned starts, stops, graceful_stops; } calls;
static int start(void *context, flex1500_tx_owner owner)
{ CHECK(owner == FLEX1500_TX_OWNER_HTTP); ++((calls *)context)->starts; return 0; }
static int stop(void *context, flex1500_tx_owner owner, bool graceful)
{
    CHECK(owner == FLEX1500_TX_OWNER_HTTP);
    calls *state = context;
    ++state->stops;
    if (graceful) ++state->graceful_stops;
    return 0;
}

int main(void)
{
    calls c = {0};
    flex1500_tx_control tx;
    flex1500_tx_control_init(&tx, true, 180000, &c, start, stop);
    flex1500_tx_control_arm_physical_ptt(&tx);
    flex1500_network_tx session;
    flex1500_network_tx_init(&session, true, &tx);
    flex1500_network_tx_profile audio = {
        FLEX1500_NETWORK_TX_USB, FLEX1500_NETWORK_TX_AUDIO, 25, 48000};
    CHECK(flex1500_network_tx_acquire(&session, &audio, 42, 100) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_ptt_start(&session, 42, 101) ==
          FLEX1500_NETWORK_TX_NOT_READY);
    CHECK(flex1500_network_tx_attach_stream(&session, 42, 102) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_record_data(
              &session, 42,
              FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES - 1, 103) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_ptt_start(&session, 42, 104) ==
          FLEX1500_NETWORK_TX_NOT_READY);
    CHECK(flex1500_network_tx_record_data(&session, 42, 1, 105) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_ptt_start(&session, 42, 106) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(c.starts == 1 && tx.owner == FLEX1500_TX_OWNER_HTTP);
    CHECK(flex1500_network_tx_ptt_stop(&session, 42) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(c.graceful_stops == 1);
    CHECK(flex1500_network_tx_ptt_start(&session, 42, 107) ==
          FLEX1500_NETWORK_TX_NOT_READY);
    CHECK(flex1500_network_tx_record_data(
              &session, 42, FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES, 108) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_ptt_start(&session, 42, 109) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_disconnect_stream(&session) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(c.stops == 2 && tx.owner == FLEX1500_TX_OWNER_NONE);
    CHECK(c.graceful_stops == 1);
    CHECK(flex1500_network_tx_release(&session, 42) == FLEX1500_NETWORK_TX_OK);

    flex1500_network_tx_profile iq = {
        FLEX1500_NETWORK_TX_IQ_MODE, FLEX1500_NETWORK_TX_IQ, 50, 48000};
    CHECK(flex1500_network_tx_acquire(&session, &iq, 43, 1000) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_attach_stream(&session, 43, 1001) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_record_data(
              &session, 43, FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES, 1002) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_ptt_start(&session, 43, 1003) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_tick(&session, 2003) ==
          FLEX1500_NETWORK_TX_DATA_TIMEOUT);
    CHECK(c.stops == 3 && !session.reserved);

    CHECK(flex1500_network_tx_acquire(&session, &audio, 44, 3000) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_tick(&session, 18000) ==
          FLEX1500_NETWORK_TX_EXPIRED);
    CHECK(flex1500_network_tx_keepalive(&session, 44, 18001) ==
          FLEX1500_NETWORK_TX_STALE);

    audio.sample_rate = 44100;
    CHECK(flex1500_network_tx_acquire(&session, &audio, 45, 20000) ==
          FLEX1500_NETWORK_TX_INVALID);
    audio.sample_rate = 48000;
    audio.drive_percent = FLEX1500_TX_MIN_DRIVE_PERCENT - 1;
    CHECK(flex1500_network_tx_acquire(&session, &audio, 46, 20000) ==
          FLEX1500_NETWORK_TX_INVALID);
    audio.drive_percent = FLEX1500_TX_MAX_DRIVE_PERCENT + 1;
    CHECK(flex1500_network_tx_acquire(&session, &audio, 47, 20000) ==
          FLEX1500_NETWORK_TX_INVALID);
    audio.drive_percent = FLEX1500_TX_MAX_DRIVE_PERCENT;
    CHECK(flex1500_network_tx_acquire(&session, &audio, 48, 20000) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_release(&session, 48) ==
          FLEX1500_NETWORK_TX_OK);
    flex1500_network_tx_profile am = {
        FLEX1500_NETWORK_TX_AM, FLEX1500_NETWORK_TX_AUDIO, 50, 48000};
    CHECK(flex1500_network_tx_acquire(&session, &am, 49, 21000) ==
          FLEX1500_NETWORK_TX_OK);
    CHECK(flex1500_network_tx_release(&session, 49) ==
          FLEX1500_NETWORK_TX_OK);
    return 0;
}
