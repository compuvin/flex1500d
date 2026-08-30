// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_API_H
#define FLEX1500_API_H

#include "flex1500/network.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int (*flex1500_api_tune_rx)(void *context, uint32_t frequency_hz,
                                    uint32_t *rx_filter);
typedef int (*flex1500_api_set_rx_gain)(void *context, int32_t gain_db);
typedef struct flex1500_api_controller {
    bool rx_tuning_enabled;
    bool iq_stream_active;
    bool test_page_enabled;
    const char *rx_mode;
    uint32_t rx_bandwidth_hz;
    int32_t rx_squelch_db;
    void *radio_context;
    flex1500_api_tune_rx tune_rx;
    flex1500_api_set_rx_gain set_rx_gain;
} flex1500_api_controller;

typedef enum flex1500_api_action {
    FLEX1500_API_RESPONSE,
    FLEX1500_API_OPEN_IQ_STREAM,
    FLEX1500_API_SERVE_TEST_PAGE,
    FLEX1500_API_TUNED_RX,
    FLEX1500_API_RX_TUNE_FAILED,
} flex1500_api_action;

void flex1500_api_controller_init(flex1500_api_controller *controller,
                                  bool rx_tuning_enabled,
                                  bool iq_stream_active,
                                  bool test_page_enabled);

uint32_t flex1500_api_rx_bandwidth(const flex1500_api_controller *controller);

flex1500_api_action flex1500_api_dispatch(
    flex1500_api_controller *controller, const char *request,
    const flex1500_service_status *status, const flex1500_radio_info *radio,
    char *response, size_t capacity, size_t *response_length,
    uint32_t *tuned_frequency, uint32_t *tuned_filter);

#endif
