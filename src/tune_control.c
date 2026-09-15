// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tune_control.h"

#include <stddef.h>

static flex1500_tune_result stop_active(flex1500_tune_control *control,
                                        flex1500_tune_result reason)
{
    if (control == NULL || !control->active) return reason;
    flex1500_tx_control_result callback_result =
        flex1500_tx_control_release(control->tx_control,
                                    FLEX1500_TX_OWNER_TUNE);
    ++control->diagnostics.stops;
    if (reason == FLEX1500_TUNE_EXPIRED ||
        reason == FLEX1500_TUNE_HARD_LIMIT) {
        ++control->diagnostics.watchdog_stops;
    }
    if (callback_result != 0) ++control->diagnostics.cleanup_failures;
    control->active = false;
    control->lease = 0;
    control->started_ms = 0;
    control->renewed_ms = 0;
    return callback_result == FLEX1500_TX_CONTROL_OK
        ? reason : FLEX1500_TUNE_HARDWARE_ERROR;
}

void flex1500_tune_control_init(
    flex1500_tune_control *control, bool enabled,
    flex1500_tx_control *tx_control)
{
    if (control == NULL) return;
    *control = (flex1500_tune_control){
        .enabled = enabled,
        .tx_control = tx_control,
    };
}

flex1500_tune_result flex1500_tune_control_start(
    flex1500_tune_control *control, uint64_t now_ms, uint64_t lease)
{
    if (control == NULL || !control->enabled) return FLEX1500_TUNE_DISABLED;
    if (control->active) {
        ++control->diagnostics.rejected_ownership_requests;
        return FLEX1500_TUNE_BUSY;
    }
    if (lease == 0 || control->tx_control == NULL) {
        return FLEX1500_TUNE_INVALID_LEASE;
    }
    flex1500_tx_control_result start_result = flex1500_tx_control_request(
        control->tx_control, FLEX1500_TX_OWNER_TUNE, now_ms);
    if (start_result != FLEX1500_TX_CONTROL_OK) {
        if (control->tx_control->cleanup_failed) {
            ++control->diagnostics.cleanup_failures;
        }
        return start_result == FLEX1500_TX_CONTROL_BUSY
            ? FLEX1500_TUNE_BUSY : FLEX1500_TUNE_HARDWARE_ERROR;
    }
    control->active = true;
    control->lease = lease;
    control->started_ms = now_ms;
    control->renewed_ms = now_ms;
    ++control->diagnostics.starts;
    return FLEX1500_TUNE_OK;
}

flex1500_tune_result flex1500_tune_control_keepalive(
    flex1500_tune_control *control, uint64_t now_ms, uint64_t lease)
{
    if (control == NULL || !control->enabled) return FLEX1500_TUNE_DISABLED;
    if (!control->active || lease == 0 || lease != control->lease) {
        ++control->diagnostics.rejected_ownership_requests;
        return FLEX1500_TUNE_INVALID_LEASE;
    }
    if (now_ms - control->started_ms >= FLEX1500_TUNE_HARD_LIMIT_MS) {
        return stop_active(control, FLEX1500_TUNE_HARD_LIMIT);
    }
    if (now_ms - control->renewed_ms >= FLEX1500_TUNE_LEASE_MS) {
        return stop_active(control, FLEX1500_TUNE_EXPIRED);
    }
    control->renewed_ms = now_ms;
    return FLEX1500_TUNE_OK;
}

flex1500_tune_result flex1500_tune_control_stop(
    flex1500_tune_control *control, uint64_t lease)
{
    if (control == NULL || !control->enabled) return FLEX1500_TUNE_DISABLED;
    if (!control->active || lease == 0 || lease != control->lease) {
        ++control->diagnostics.rejected_ownership_requests;
        return FLEX1500_TUNE_INVALID_LEASE;
    }
    return stop_active(control, FLEX1500_TUNE_OK);
}

flex1500_tune_result flex1500_tune_control_tick(
    flex1500_tune_control *control, uint64_t now_ms)
{
    if (control == NULL || !control->enabled) return FLEX1500_TUNE_DISABLED;
    if (!control->active) return FLEX1500_TUNE_OK;
    if (now_ms - control->started_ms >= FLEX1500_TUNE_HARD_LIMIT_MS) {
        return stop_active(control, FLEX1500_TUNE_HARD_LIMIT);
    }
    if (now_ms - control->renewed_ms >= FLEX1500_TUNE_LEASE_MS) {
        return stop_active(control, FLEX1500_TUNE_EXPIRED);
    }
    return FLEX1500_TUNE_OK;
}

void flex1500_tune_control_reconcile(flex1500_tune_control *control,
                                     bool watchdog_stop)
{
    if (control == NULL || !control->active || control->tx_control == NULL ||
        control->tx_control->owner == FLEX1500_TX_OWNER_TUNE) {
        return;
    }
    control->active = false;
    control->lease = 0;
    control->started_ms = 0;
    control->renewed_ms = 0;
    ++control->diagnostics.stops;
    if (watchdog_stop) ++control->diagnostics.watchdog_stops;
}

void flex1500_tune_control_shutdown(flex1500_tune_control *control)
{
    if (control != NULL && control->active) {
        (void)stop_active(control, FLEX1500_TUNE_OK);
    }
}

const flex1500_tx_diagnostics *flex1500_tune_control_diagnostics(
    const flex1500_tune_control *control)
{
    return control != NULL ? &control->diagnostics : NULL;
}

void flex1500_tune_control_record_underrun(flex1500_tune_control *control,
                                           uint64_t count)
{
    if (control != NULL) control->diagnostics.underruns += count;
}

void flex1500_tune_control_record_audio_quality(
    flex1500_tune_control *control, uint64_t clipped_frames,
    uint64_t dropped_microphone_frames)
{
    if (control == NULL) return;
    control->diagnostics.clipped_frames += clipped_frames;
    control->diagnostics.dropped_microphone_frames +=
        dropped_microphone_frames;
}

void flex1500_tune_control_record_graceful_drain(
    flex1500_tune_control *control, uint64_t peak_queued_frames,
    uint64_t stop_requested_frames, uint64_t drained_frames,
    uint64_t discarded_frames, uint64_t drain_ms)
{
    if (control == NULL) return;
    if (peak_queued_frames > control->diagnostics.peak_queued_frames) {
        control->diagnostics.peak_queued_frames = peak_queued_frames;
    }
    control->diagnostics.stop_requested_frames += stop_requested_frames;
    control->diagnostics.graceful_drained_frames += drained_frames;
    control->diagnostics.graceful_discarded_frames += discarded_frames;
    control->diagnostics.graceful_drain_ms += drain_ms;
}

void flex1500_tune_control_record_cleanup_failure(
    flex1500_tune_control *control)
{
    if (control != NULL) ++control->diagnostics.cleanup_failures;
}

const char *flex1500_tune_result_name(flex1500_tune_result result)
{
    static const char *names[] = {
        "ok", "disabled", "busy", "invalid_lease", "expired",
        "hard_limit", "hardware_error",
    };
    return result <= FLEX1500_TUNE_HARDWARE_ERROR ? names[result] : "unknown";
}
