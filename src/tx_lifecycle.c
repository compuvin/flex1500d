// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_lifecycle.h"

uint32_t flex1500_tx_cleanup_plan(uint32_t state)
{
    uint32_t actions = FLEX1500_TX_CLEANUP_NONE;

    if ((state & FLEX1500_TX_PARTIAL_KEY_ATTEMPTED) != 0) {
        actions |= FLEX1500_TX_CLEANUP_TRANSITION_MUTE |
                   FLEX1500_TX_CLEANUP_UNKEY;
    }
    if ((state & (FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED |
                  FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED)) != 0) {
        actions |= FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY |
                   FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE;
    }
    if ((state & FLEX1500_TX_PARTIAL_STREAM_STARTED) != 0) {
        actions |= FLEX1500_TX_CLEANUP_STOP_STREAM;
    }
    if ((state & FLEX1500_TX_PARTIAL_PA_SELECTED) != 0) {
        actions |= FLEX1500_TX_CLEANUP_RESET_PA_FILTER;
    }
    if ((state & FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED) != 0) {
        actions |= FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER;
    }
    return actions;
}

int flex1500_tx_execute_cleanup(uint32_t plan,
                               flex1500_tx_cleanup_callback callback,
                               void *context, uint32_t *attempted_actions)
{
    static const flex1500_tx_cleanup_action order[] = {
        FLEX1500_TX_CLEANUP_TRANSITION_MUTE,
        FLEX1500_TX_CLEANUP_UNKEY,
        FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY,
        FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE,
        FLEX1500_TX_CLEANUP_RESET_PA_FILTER,
        FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER,
        FLEX1500_TX_CLEANUP_STOP_STREAM,
    };
    uint32_t attempted = FLEX1500_TX_CLEANUP_NONE;
    int first_error = 0;
    if (callback == 0) return -1;

    for (unsigned int index = 0;
         index < sizeof(order) / sizeof(order[0]); ++index) {
        flex1500_tx_cleanup_action action = order[index];
        if ((plan & (uint32_t)action) == 0) continue;
        attempted |= (uint32_t)action;
        int result = callback(action, context);
        if (first_error == 0 && result != 0) first_error = result;
    }
    if (attempted_actions != 0) *attempted_actions = attempted;
    return first_error;
}
