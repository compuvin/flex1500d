// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CONFIG_H
#define FLEX1500_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FLEX1500_DEFAULT_CONFIG_PATH "/etc/flex1500d/flex1500d.conf"

typedef enum flex1500_radio_mode {
    FLEX1500_RADIO_DISABLED = 0,
    FLEX1500_RADIO_RECEIVE,
    FLEX1500_RADIO_RX_TUNING,
    FLEX1500_RADIO_TRANSMIT,
} flex1500_radio_mode;

typedef struct flex1500_config {
    bool daemon_enabled;
    flex1500_radio_mode radio_mode;
    char http_bind[64];
    uint16_t http_port;
    bool test_page_enabled;
} flex1500_config;

void flex1500_config_defaults(flex1500_config *config);
bool flex1500_config_load(flex1500_config *config, const char *path,
                          bool missing_is_ok, char *error, size_t error_size);
const char *flex1500_radio_mode_name(flex1500_radio_mode mode);
bool flex1500_parse_radio_mode(const char *text, flex1500_radio_mode *mode);
void flex1500_config_print(const flex1500_config *config, const char *path);

#endif
