// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network_tx.h"

#include <string.h>

static bool valid_profile(const flex1500_network_tx_profile *profile)
{
    if (profile == NULL ||
        profile->drive_percent < FLEX1500_TX_MIN_DRIVE_PERCENT ||
        profile->drive_percent > FLEX1500_TX_MAX_DRIVE_PERCENT ||
        profile->sample_rate != 48000) {
        return false;
    }
    return (profile->source == FLEX1500_NETWORK_TX_AUDIO &&
            (profile->mode == FLEX1500_NETWORK_TX_USB ||
             profile->mode == FLEX1500_NETWORK_TX_LSB)) ||
           (profile->source == FLEX1500_NETWORK_TX_IQ &&
            profile->mode == FLEX1500_NETWORK_TX_IQ_MODE);
}

static bool owns(const flex1500_network_tx *session, uint64_t lease)
{
    return session != NULL && session->reserved && lease != 0 &&
           session->lease == lease;
}

static flex1500_network_tx_result stop_if_keyed(flex1500_network_tx *session)
{
    if (!session->keyed) return FLEX1500_NETWORK_TX_OK;
    flex1500_tx_control_result result = flex1500_tx_control_release(
        session->tx_control, FLEX1500_TX_OWNER_HTTP);
    session->keyed = false;
    return result == FLEX1500_TX_CONTROL_OK ? FLEX1500_NETWORK_TX_OK
                                            : FLEX1500_NETWORK_TX_HARDWARE_ERROR;
}

static void clear_session(flex1500_network_tx *session)
{
    flex1500_tx_control *tx = session->tx_control;
    bool enabled = session->enabled;
    memset(session, 0, sizeof(*session));
    session->enabled = enabled;
    session->tx_control = tx;
}

void flex1500_network_tx_init(flex1500_network_tx *session, bool enabled,
                              flex1500_tx_control *tx_control)
{
    if (session == NULL) return;
    *session = (flex1500_network_tx){
        .enabled = enabled,
        .tx_control = tx_control,
    };
}

flex1500_network_tx_result flex1500_network_tx_acquire(
    flex1500_network_tx *session, const flex1500_network_tx_profile *profile,
    uint64_t lease, uint64_t now_ms)
{
    if (session == NULL || !session->enabled || session->tx_control == NULL ||
        !session->tx_control->enabled) return FLEX1500_NETWORK_TX_DISABLED;
    if (!valid_profile(profile) || lease == 0) return FLEX1500_NETWORK_TX_INVALID;
    if (session->reserved || session->tx_control->owner != FLEX1500_TX_OWNER_NONE) {
        return FLEX1500_NETWORK_TX_BUSY;
    }
    session->reserved = true;
    session->lease = lease;
    session->renewed_ms = now_ms;
    session->last_data_ms = now_ms;
    session->profile = *profile;
    return FLEX1500_NETWORK_TX_OK;
}

flex1500_network_tx_result flex1500_network_tx_keepalive(
    flex1500_network_tx *session, uint64_t lease, uint64_t now_ms)
{
    if (!owns(session, lease)) return FLEX1500_NETWORK_TX_STALE;
    session->renewed_ms = now_ms;
    return FLEX1500_NETWORK_TX_OK;
}

flex1500_network_tx_result flex1500_network_tx_attach_stream(
    flex1500_network_tx *session, uint64_t lease, uint64_t now_ms)
{
    if (!owns(session, lease)) return FLEX1500_NETWORK_TX_STALE;
    if (session->stream_connected) return FLEX1500_NETWORK_TX_BUSY;
    session->stream_connected = true;
    session->buffered_frames = 0;
    session->last_data_ms = now_ms;
    return FLEX1500_NETWORK_TX_OK;
}

flex1500_network_tx_result flex1500_network_tx_record_data(
    flex1500_network_tx *session, uint64_t lease, size_t frames,
    uint64_t now_ms)
{
    if (!owns(session, lease)) return FLEX1500_NETWORK_TX_STALE;
    if (!session->stream_connected || frames == 0) return FLEX1500_NETWORK_TX_INVALID;
    if (session->buffered_frames < FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES) {
        size_t room = FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES -
                      session->buffered_frames;
        session->buffered_frames += frames < room ? frames : room;
    }
    session->last_data_ms = now_ms;
    return FLEX1500_NETWORK_TX_OK;
}

flex1500_network_tx_result flex1500_network_tx_ptt_start(
    flex1500_network_tx *session, uint64_t lease, uint64_t now_ms)
{
    if (!owns(session, lease)) return FLEX1500_NETWORK_TX_STALE;
    if (!session->stream_connected ||
        session->buffered_frames < FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES) {
        return FLEX1500_NETWORK_TX_NOT_READY;
    }
    if (session->keyed) return FLEX1500_NETWORK_TX_OK;
    flex1500_tx_control_result result = flex1500_tx_control_request(
        session->tx_control, FLEX1500_TX_OWNER_HTTP, now_ms);
    if (result == FLEX1500_TX_CONTROL_BUSY) return FLEX1500_NETWORK_TX_BUSY;
    if (result != FLEX1500_TX_CONTROL_OK) {
        return FLEX1500_NETWORK_TX_HARDWARE_ERROR;
    }
    session->keyed = true;
    session->renewed_ms = now_ms;
    return FLEX1500_NETWORK_TX_OK;
}

flex1500_network_tx_result flex1500_network_tx_ptt_stop(
    flex1500_network_tx *session, uint64_t lease)
{
    if (!owns(session, lease)) return FLEX1500_NETWORK_TX_STALE;
    bool was_keyed = session->keyed;
    flex1500_network_tx_result result = stop_if_keyed(session);
    if (was_keyed) session->buffered_frames = 0;
    return result;
}

flex1500_network_tx_result flex1500_network_tx_release(
    flex1500_network_tx *session, uint64_t lease)
{
    if (!owns(session, lease)) return FLEX1500_NETWORK_TX_STALE;
    flex1500_network_tx_result result = stop_if_keyed(session);
    clear_session(session);
    return result;
}

flex1500_network_tx_result flex1500_network_tx_disconnect_stream(
    flex1500_network_tx *session)
{
    if (session == NULL || !session->reserved) return FLEX1500_NETWORK_TX_OK;
    flex1500_network_tx_result result = stop_if_keyed(session);
    session->stream_connected = false;
    session->buffered_frames = 0;
    return result;
}

flex1500_network_tx_result flex1500_network_tx_tick(
    flex1500_network_tx *session, uint64_t now_ms)
{
    if (session == NULL || !session->reserved) return FLEX1500_NETWORK_TX_OK;
    bool data_timeout = session->keyed &&
        now_ms - session->last_data_ms >= FLEX1500_NETWORK_TX_DATA_MS;
    bool expired = now_ms - session->renewed_ms >= FLEX1500_NETWORK_TX_LEASE_MS;
    if (!data_timeout && !expired) return FLEX1500_NETWORK_TX_OK;
    flex1500_network_tx_result stopped = stop_if_keyed(session);
    clear_session(session);
    if (stopped != FLEX1500_NETWORK_TX_OK) return stopped;
    return data_timeout ? FLEX1500_NETWORK_TX_DATA_TIMEOUT
                        : FLEX1500_NETWORK_TX_EXPIRED;
}

void flex1500_network_tx_reconcile(flex1500_network_tx *session)
{
    if (session != NULL && session->reserved && session->keyed &&
        (session->tx_control == NULL ||
         session->tx_control->owner != FLEX1500_TX_OWNER_HTTP)) {
        clear_session(session);
    }
}

const char *flex1500_network_tx_result_name(flex1500_network_tx_result result)
{
    static const char *names[] = {"ok", "disabled", "busy", "invalid",
        "stale", "not_ready", "hardware_error", "expired", "data_timeout"};
    return result <= FLEX1500_NETWORK_TX_DATA_TIMEOUT ? names[result] : "unknown";
}
