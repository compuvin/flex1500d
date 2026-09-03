// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TX_CONTROL_H
#define FLEX1500_TX_CONTROL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum flex1500_tx_owner {
    FLEX1500_TX_OWNER_NONE,
    FLEX1500_TX_OWNER_TUNE,
    FLEX1500_TX_OWNER_PHYSICAL_MIC,
    FLEX1500_TX_OWNER_HTTP,
    FLEX1500_TX_OWNER_SOAPY,
} flex1500_tx_owner;

typedef enum flex1500_tx_state {
    FLEX1500_TX_STATE_STARTUP_INHIBIT,
    FLEX1500_TX_STATE_RX,
    FLEX1500_TX_STATE_PREPARING,
    FLEX1500_TX_STATE_TRANSMITTING,
    FLEX1500_TX_STATE_UNKEYING,
    FLEX1500_TX_STATE_RECOVERING,
    FLEX1500_TX_STATE_FAULTED,
} flex1500_tx_state;

typedef enum flex1500_tx_control_result {
    FLEX1500_TX_CONTROL_OK,
    FLEX1500_TX_CONTROL_DISABLED,
    FLEX1500_TX_CONTROL_INHIBITED,
    FLEX1500_TX_CONTROL_BUSY,
    FLEX1500_TX_CONTROL_INVALID_OWNER,
    FLEX1500_TX_CONTROL_HARDWARE_ERROR,
    FLEX1500_TX_CONTROL_MAX_KEY,
} flex1500_tx_control_result;

typedef int (*flex1500_tx_owner_start)(void *context,
                                       flex1500_tx_owner owner);
typedef int (*flex1500_tx_owner_stop)(void *context,
                                      flex1500_tx_owner owner);

typedef struct flex1500_tx_control {
    bool enabled;
    bool physical_ptt_armed;
    bool physical_ptt_pressed;
    bool cleanup_failed;
    flex1500_tx_owner owner;
    flex1500_tx_state state;
    uint64_t started_ms;
    uint64_t maximum_key_ms;
    void *callback_context;
    flex1500_tx_owner_start start_callback;
    flex1500_tx_owner_stop stop_callback;
} flex1500_tx_control;

enum {
    FLEX1500_TX_TIMEOUT_DEFAULT_SECONDS = 180,
    FLEX1500_TX_TIMEOUT_MIN_SECONDS = 30,
    FLEX1500_TX_TIMEOUT_MAX_SECONDS = 1800,
};

void flex1500_tx_control_init(flex1500_tx_control *control, bool enabled,
                              uint64_t maximum_key_ms, void *context,
                              flex1500_tx_owner_start start_callback,
                              flex1500_tx_owner_stop stop_callback);
/* Call only after the hardware has been explicitly returned to RX/unkeyed. */
void flex1500_tx_control_arm_physical_ptt(flex1500_tx_control *control);
flex1500_tx_control_result flex1500_tx_control_request(
    flex1500_tx_control *control, flex1500_tx_owner owner, uint64_t now_ms);
flex1500_tx_control_result flex1500_tx_control_release(
    flex1500_tx_control *control, flex1500_tx_owner owner);
flex1500_tx_control_result flex1500_tx_control_physical_ptt(
    flex1500_tx_control *control, bool pressed, uint64_t now_ms);
flex1500_tx_control_result flex1500_tx_control_tick(
    flex1500_tx_control *control, uint64_t now_ms);
void flex1500_tx_control_shutdown(flex1500_tx_control *control);
bool flex1500_tx_control_set_timeout_seconds(flex1500_tx_control *control,
                                             uint32_t seconds);
uint32_t flex1500_tx_control_timeout_seconds(
    const flex1500_tx_control *control);
const char *flex1500_tx_owner_name(flex1500_tx_owner owner);
const char *flex1500_tx_state_name(flex1500_tx_state state);

#endif
