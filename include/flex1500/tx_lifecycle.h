// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TX_LIFECYCLE_H
#define FLEX1500_TX_LIFECYCLE_H

#include <stdint.h>

typedef enum flex1500_tx_partial_state {
    FLEX1500_TX_PARTIAL_NONE = 0,
    FLEX1500_TX_PARTIAL_PA_SELECTED = 1u << 0,
    FLEX1500_TX_PARTIAL_AMP_ENABLE_ATTEMPTED = 1u << 1,
    FLEX1500_TX_PARTIAL_STREAM_STARTED = 1u << 2,
    FLEX1500_TX_PARTIAL_TRANSITION_ATTEMPTED = 1u << 3,
    FLEX1500_TX_PARTIAL_FREQUENCY_ATTEMPTED = 1u << 4,
    FLEX1500_TX_PARTIAL_KEY_ATTEMPTED = 1u << 5,
} flex1500_tx_partial_state;

typedef enum flex1500_tx_cleanup_action {
    FLEX1500_TX_CLEANUP_NONE = 0,
    FLEX1500_TX_CLEANUP_TRANSITION_MUTE = 1u << 0,
    FLEX1500_TX_CLEANUP_UNKEY = 1u << 1,
    FLEX1500_TX_CLEANUP_RESTORE_RX_FREQUENCY = 1u << 2,
    FLEX1500_TX_CLEANUP_TRANSITION_UNMUTE = 1u << 3,
    FLEX1500_TX_CLEANUP_STOP_STREAM = 1u << 4,
    FLEX1500_TX_CLEANUP_RESET_PA_FILTER = 1u << 5,
    FLEX1500_TX_CLEANUP_DISABLE_AMPLIFIER = 1u << 6,
} flex1500_tx_cleanup_action;

/* Pure, offline safety policy used by both the USB backend and its tests. */
uint32_t flex1500_tx_cleanup_plan(uint32_t partial_state);

typedef int (*flex1500_tx_cleanup_callback)(
    flex1500_tx_cleanup_action action, void *context);

/* Executes every planned action in safety order, even after an earlier error. */
int flex1500_tx_execute_cleanup(uint32_t plan,
                               flex1500_tx_cleanup_callback callback,
                               void *context, uint32_t *attempted_actions);

#endif
