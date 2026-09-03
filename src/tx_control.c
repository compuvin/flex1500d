// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_control.h"

#include <stddef.h>

static bool valid_owner(flex1500_tx_owner owner)
{
    return owner >= FLEX1500_TX_OWNER_TUNE &&
           owner <= FLEX1500_TX_OWNER_SOAPY;
}

static flex1500_tx_control_result stop_owner(flex1500_tx_control *control)
{
    if (control->owner == FLEX1500_TX_OWNER_NONE) return FLEX1500_TX_CONTROL_OK;
    flex1500_tx_owner owner = control->owner;
    control->state = FLEX1500_TX_STATE_UNKEYING;
    int result = control->stop_callback != NULL
        ? control->stop_callback(control->callback_context, owner) : -1;
    control->cleanup_failed = result != 0;
    control->owner = FLEX1500_TX_OWNER_NONE;
    control->started_ms = 0;
    control->state = result == 0 ? FLEX1500_TX_STATE_RX
                                 : FLEX1500_TX_STATE_FAULTED;
    return result == 0 ? FLEX1500_TX_CONTROL_OK
                       : FLEX1500_TX_CONTROL_HARDWARE_ERROR;
}

void flex1500_tx_control_init(flex1500_tx_control *control, bool enabled,
                              uint64_t maximum_key_ms, void *context,
                              flex1500_tx_owner_start start_callback,
                              flex1500_tx_owner_stop stop_callback)
{
    if (control == NULL) return;
    *control = (flex1500_tx_control){
        .enabled = enabled,
        .state = enabled ? FLEX1500_TX_STATE_STARTUP_INHIBIT
                         : FLEX1500_TX_STATE_RX,
        .maximum_key_ms = maximum_key_ms,
        .callback_context = context,
        .start_callback = start_callback,
        .stop_callback = stop_callback,
    };
}

void flex1500_tx_control_arm_physical_ptt(flex1500_tx_control *control)
{
    if (control == NULL || !control->enabled ||
        control->owner != FLEX1500_TX_OWNER_NONE) return;
    control->physical_ptt_armed = true;
    if (control->state == FLEX1500_TX_STATE_STARTUP_INHIBIT) {
        control->state = FLEX1500_TX_STATE_RX;
    }
}

flex1500_tx_control_result flex1500_tx_control_request(
    flex1500_tx_control *control, flex1500_tx_owner owner, uint64_t now_ms)
{
    if (control == NULL || !control->enabled) return FLEX1500_TX_CONTROL_DISABLED;
    if (!valid_owner(owner)) return FLEX1500_TX_CONTROL_INVALID_OWNER;
    if (owner == FLEX1500_TX_OWNER_PHYSICAL_MIC &&
        !control->physical_ptt_armed) return FLEX1500_TX_CONTROL_INHIBITED;
    if (control->state == FLEX1500_TX_STATE_FAULTED ||
        control->state == FLEX1500_TX_STATE_RECOVERING) {
        return FLEX1500_TX_CONTROL_INHIBITED;
    }
    if (control->owner == owner) return FLEX1500_TX_CONTROL_OK;
    if (control->owner != FLEX1500_TX_OWNER_NONE) {
        return FLEX1500_TX_CONTROL_BUSY;
    }
    control->cleanup_failed = false;
    control->state = FLEX1500_TX_STATE_PREPARING;
    if (control->start_callback == NULL ||
        control->start_callback(control->callback_context, owner) != 0) {
        int cleanup_result = control->stop_callback != NULL
            ? control->stop_callback(control->callback_context, owner) : -1;
        control->cleanup_failed = cleanup_result != 0;
        control->state = FLEX1500_TX_STATE_FAULTED;
        return FLEX1500_TX_CONTROL_HARDWARE_ERROR;
    }
    control->owner = owner;
    control->started_ms = now_ms;
    control->state = FLEX1500_TX_STATE_TRANSMITTING;
    return FLEX1500_TX_CONTROL_OK;
}

flex1500_tx_control_result flex1500_tx_control_release(
    flex1500_tx_control *control, flex1500_tx_owner owner)
{
    if (control == NULL || !control->enabled) return FLEX1500_TX_CONTROL_DISABLED;
    if (!valid_owner(owner)) return FLEX1500_TX_CONTROL_INVALID_OWNER;
    if (control->owner != owner) return FLEX1500_TX_CONTROL_BUSY;
    return stop_owner(control);
}

flex1500_tx_control_result flex1500_tx_control_physical_ptt(
    flex1500_tx_control *control, bool pressed, uint64_t now_ms)
{
    if (control == NULL || !control->enabled) return FLEX1500_TX_CONTROL_DISABLED;
    control->physical_ptt_pressed = pressed;
    if (!control->physical_ptt_armed) {
        if (pressed) return FLEX1500_TX_CONTROL_INHIBITED;
        control->physical_ptt_armed = true;
        if (control->state == FLEX1500_TX_STATE_STARTUP_INHIBIT) {
            control->state = FLEX1500_TX_STATE_RX;
        }
        return FLEX1500_TX_CONTROL_OK;
    }
    if (!pressed) {
        if (control->owner == FLEX1500_TX_OWNER_PHYSICAL_MIC) {
            return stop_owner(control);
        }
        return FLEX1500_TX_CONTROL_OK;
    }
    if (control->owner != FLEX1500_TX_OWNER_NONE &&
        control->owner != FLEX1500_TX_OWNER_PHYSICAL_MIC) {
        flex1500_tx_control_result stopped = stop_owner(control);
        if (stopped != FLEX1500_TX_CONTROL_OK) return stopped;
    }
    return flex1500_tx_control_request(control,
                                       FLEX1500_TX_OWNER_PHYSICAL_MIC, now_ms);
}

flex1500_tx_control_result flex1500_tx_control_tick(
    flex1500_tx_control *control, uint64_t now_ms)
{
    if (control == NULL || !control->enabled) return FLEX1500_TX_CONTROL_DISABLED;
    if (control->owner == FLEX1500_TX_OWNER_NONE ||
        control->state != FLEX1500_TX_STATE_TRANSMITTING) {
        return FLEX1500_TX_CONTROL_OK;
    }
    if (control->maximum_key_ms != 0 &&
        now_ms - control->started_ms >= control->maximum_key_ms) {
        flex1500_tx_control_result stopped = stop_owner(control);
        return stopped == FLEX1500_TX_CONTROL_OK ? FLEX1500_TX_CONTROL_MAX_KEY
                                                 : stopped;
    }
    return FLEX1500_TX_CONTROL_OK;
}

void flex1500_tx_control_shutdown(flex1500_tx_control *control)
{
    if (control != NULL && control->owner != FLEX1500_TX_OWNER_NONE) {
        (void)stop_owner(control);
    }
}

bool flex1500_tx_control_set_timeout_seconds(flex1500_tx_control *control,
                                             uint32_t seconds)
{
    if (control == NULL || control->owner != FLEX1500_TX_OWNER_NONE ||
        seconds < FLEX1500_TX_TIMEOUT_MIN_SECONDS ||
        seconds > FLEX1500_TX_TIMEOUT_MAX_SECONDS) return false;
    control->maximum_key_ms = (uint64_t)seconds * UINT64_C(1000);
    return true;
}

uint32_t flex1500_tx_control_timeout_seconds(
    const flex1500_tx_control *control)
{
    return control != NULL
        ? (uint32_t)(control->maximum_key_ms / UINT64_C(1000)) : 0;
}

const char *flex1500_tx_owner_name(flex1500_tx_owner owner)
{
    static const char *names[] = {"none", "tune", "physical_mic", "http",
                                  "soapy"};
    return owner <= FLEX1500_TX_OWNER_SOAPY ? names[owner] : "unknown";
}

const char *flex1500_tx_state_name(flex1500_tx_state state)
{
    static const char *names[] = {"startup_inhibit", "rx", "preparing",
                                  "transmitting", "unkeying", "recovering",
                                  "faulted"};
    return state <= FLEX1500_TX_STATE_FAULTED ? names[state] : "unknown";
}
