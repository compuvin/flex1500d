// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TUNE_CONTROL_H
#define FLEX1500_TUNE_CONTROL_H

#include "flex1500/tx_control.h"

#include <stdbool.h>
#include <stdint.h>

enum {
    FLEX1500_TUNE_LEASE_MS = 15000,
    FLEX1500_TUNE_HARD_LIMIT_MS = 60000,
};

typedef enum flex1500_tune_result {
    FLEX1500_TUNE_OK,
    FLEX1500_TUNE_DISABLED,
    FLEX1500_TUNE_BUSY,
    FLEX1500_TUNE_INVALID_LEASE,
    FLEX1500_TUNE_EXPIRED,
    FLEX1500_TUNE_HARD_LIMIT,
    FLEX1500_TUNE_HARDWARE_ERROR,
} flex1500_tune_result;

typedef struct flex1500_tx_diagnostics {
    uint64_t starts;
    uint64_t stops;
    uint64_t underruns;
    uint64_t clipped_frames;
    uint64_t dropped_microphone_frames;
    uint64_t rejected_ownership_requests;
    uint64_t watchdog_stops;
    uint64_t cleanup_failures;
} flex1500_tx_diagnostics;

typedef struct flex1500_tune_control {
    bool enabled;
    bool active;
    uint64_t lease;
    uint64_t started_ms;
    uint64_t renewed_ms;
    flex1500_tx_control *tx_control;
    flex1500_tx_diagnostics diagnostics;
} flex1500_tune_control;

void flex1500_tune_control_init(
    flex1500_tune_control *control, bool enabled,
    flex1500_tx_control *tx_control);
flex1500_tune_result flex1500_tune_control_start(
    flex1500_tune_control *control, uint64_t now_ms, uint64_t lease);
flex1500_tune_result flex1500_tune_control_keepalive(
    flex1500_tune_control *control, uint64_t now_ms, uint64_t lease);
flex1500_tune_result flex1500_tune_control_stop(
    flex1500_tune_control *control, uint64_t lease);
flex1500_tune_result flex1500_tune_control_tick(
    flex1500_tune_control *control, uint64_t now_ms);
void flex1500_tune_control_reconcile(flex1500_tune_control *control,
                                     bool watchdog_stop);
void flex1500_tune_control_shutdown(flex1500_tune_control *control);
const flex1500_tx_diagnostics *flex1500_tune_control_diagnostics(
    const flex1500_tune_control *control);
void flex1500_tune_control_record_underrun(flex1500_tune_control *control,
                                           uint64_t count);
void flex1500_tune_control_record_audio_quality(
    flex1500_tune_control *control, uint64_t clipped_frames,
    uint64_t dropped_microphone_frames);
void flex1500_tune_control_record_cleanup_failure(
    flex1500_tune_control *control);
const char *flex1500_tune_result_name(flex1500_tune_result result);

#endif
