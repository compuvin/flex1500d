// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_NETWORK_TX_H
#define FLEX1500_NETWORK_TX_H

#include "flex1500/tx_control.h"
#include "flex1500/tx_limits.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLEX1500_NETWORK_TX_LEASE_MS = 15000,
    FLEX1500_NETWORK_TX_DATA_MS = 1000,
    FLEX1500_NETWORK_TX_MIN_PREBUFFER_FRAMES = 4096,
};

typedef enum flex1500_network_tx_source {
    FLEX1500_NETWORK_TX_AUDIO,
    FLEX1500_NETWORK_TX_IQ,
} flex1500_network_tx_source;

typedef enum flex1500_network_tx_mode {
    FLEX1500_NETWORK_TX_USB,
    FLEX1500_NETWORK_TX_LSB,
    FLEX1500_NETWORK_TX_AM,
    FLEX1500_NETWORK_TX_IQ_MODE,
} flex1500_network_tx_mode;

typedef struct flex1500_network_tx_profile {
    flex1500_network_tx_mode mode;
    flex1500_network_tx_source source;
    unsigned int drive_percent;
    unsigned int sample_rate;
} flex1500_network_tx_profile;

typedef enum flex1500_network_tx_result {
    FLEX1500_NETWORK_TX_OK,
    FLEX1500_NETWORK_TX_DISABLED,
    FLEX1500_NETWORK_TX_BUSY,
    FLEX1500_NETWORK_TX_INVALID,
    FLEX1500_NETWORK_TX_STALE,
    FLEX1500_NETWORK_TX_NOT_READY,
    FLEX1500_NETWORK_TX_HARDWARE_ERROR,
    FLEX1500_NETWORK_TX_EXPIRED,
    FLEX1500_NETWORK_TX_DATA_TIMEOUT,
} flex1500_network_tx_result;

typedef struct flex1500_network_tx {
    bool enabled;
    bool reserved;
    bool stream_connected;
    bool keyed;
    uint64_t lease;
    uint64_t renewed_ms;
    uint64_t last_data_ms;
    size_t buffered_frames;
    flex1500_network_tx_profile profile;
    flex1500_tx_control *tx_control;
} flex1500_network_tx;

void flex1500_network_tx_init(flex1500_network_tx *session, bool enabled,
                              flex1500_tx_control *tx_control);
flex1500_network_tx_result flex1500_network_tx_acquire(
    flex1500_network_tx *session, const flex1500_network_tx_profile *profile,
    uint64_t lease, uint64_t now_ms);
flex1500_network_tx_result flex1500_network_tx_keepalive(
    flex1500_network_tx *session, uint64_t lease, uint64_t now_ms);
flex1500_network_tx_result flex1500_network_tx_attach_stream(
    flex1500_network_tx *session, uint64_t lease, uint64_t now_ms);
flex1500_network_tx_result flex1500_network_tx_record_data(
    flex1500_network_tx *session, uint64_t lease, size_t frames,
    uint64_t now_ms);
flex1500_network_tx_result flex1500_network_tx_ptt_start(
    flex1500_network_tx *session, uint64_t lease, uint64_t now_ms);
flex1500_network_tx_result flex1500_network_tx_ptt_stop(
    flex1500_network_tx *session, uint64_t lease);
flex1500_network_tx_result flex1500_network_tx_release(
    flex1500_network_tx *session, uint64_t lease);
flex1500_network_tx_result flex1500_network_tx_disconnect_stream(
    flex1500_network_tx *session);
flex1500_network_tx_result flex1500_network_tx_tick(
    flex1500_network_tx *session, uint64_t now_ms);
void flex1500_network_tx_reconcile(flex1500_network_tx *session);
const char *flex1500_network_tx_result_name(flex1500_network_tx_result result);

#endif
