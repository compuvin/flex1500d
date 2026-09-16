// SPDX-License-Identifier: GPL-3.0-only

#define _POSIX_C_SOURCE 200809L

#include "flex1500/config.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void flex1500_config_defaults(flex1500_config *config)
{
    if (config == NULL) return;
    *config = (flex1500_config){
        .daemon_enabled = true,
        .radio_mode = FLEX1500_RADIO_RX_TUNING,
        .http_bind = "0.0.0.0",
        .http_port = 15000,
        .test_page_enabled = false,
    };
}

const char *flex1500_radio_mode_name(flex1500_radio_mode mode)
{
    switch (mode) {
    case FLEX1500_RADIO_DISABLED: return "offline";
    case FLEX1500_RADIO_RECEIVE: return "receive";
    case FLEX1500_RADIO_RX_TUNING: return "rx-tuning";
    case FLEX1500_RADIO_TRANSMIT: return "transmit";
    }
    return "invalid";
}

bool flex1500_parse_radio_mode(const char *text, flex1500_radio_mode *mode)
{
    if (text == NULL || mode == NULL) return false;
    if (strcmp(text, "offline") == 0) *mode = FLEX1500_RADIO_DISABLED;
    else if (strcmp(text, "receive") == 0) *mode = FLEX1500_RADIO_RECEIVE;
    else if (strcmp(text, "rx-tuning") == 0) *mode = FLEX1500_RADIO_RX_TUNING;
    else if (strcmp(text, "transmit") == 0) *mode = FLEX1500_RADIO_TRANSMIT;
    else return false;
    return true;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) ++text;
    char *end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static bool parse_bool(const char *text, bool *value)
{
    if (strcmp(text, "true") == 0 || strcmp(text, "yes") == 0 ||
        strcmp(text, "1") == 0) {
        *value = true;
        return true;
    }
    if (strcmp(text, "false") == 0 || strcmp(text, "no") == 0 ||
        strcmp(text, "0") == 0) {
        *value = false;
        return true;
    }
    return false;
}

static bool fail(char *error, size_t size, const char *path, unsigned int line,
                 const char *message)
{
    if (error != NULL && size != 0) {
        if (line != 0) snprintf(error, size, "%s:%u: %s", path, line, message);
        else snprintf(error, size, "%s: %s", path, message);
    }
    return false;
}

bool flex1500_config_load(flex1500_config *config, const char *path,
                          bool missing_is_ok, char *error, size_t error_size)
{
    if (config == NULL || path == NULL)
        return fail(error, error_size, "configuration", 0, "invalid argument");
    FILE *file = fopen(path, "r");
    if (file == NULL) {
        if (missing_is_ok && errno == ENOENT) return true;
        char detail[160];
        snprintf(detail, sizeof(detail), "cannot open: %s", strerror(errno));
        return fail(error, error_size, path, 0, detail);
    }

    enum { SECTION_NONE, SECTION_DAEMON, SECTION_RADIO, SECTION_HTTP } section =
        SECTION_NONE;
    enum { SEEN_ENABLED = 1, SEEN_MODE = 2, SEEN_BIND = 4,
           SEEN_PORT = 8, SEEN_TEST_PAGE = 16 };
    unsigned int seen = 0;
    unsigned int line_number = 0;
    char line[512];
    bool valid = true;
    while (fgets(line, sizeof(line), file) != NULL) {
        ++line_number;
        if (strchr(line, '\n') == NULL && !feof(file)) {
            valid = fail(error, error_size, path, line_number, "line is too long");
            break;
        }
        char *text = trim(line);
        if (*text == '\0' || *text == '#' || *text == ';') continue;
        if (*text == '[') {
            size_t length = strlen(text);
            if (length < 3 || text[length - 1] != ']') {
                valid = fail(error, error_size, path, line_number,
                             "malformed section header");
                break;
            }
            text[length - 1] = '\0';
            const char *name = trim(text + 1);
            if (strcmp(name, "daemon") == 0) section = SECTION_DAEMON;
            else if (strcmp(name, "radio") == 0) section = SECTION_RADIO;
            else if (strcmp(name, "http") == 0) section = SECTION_HTTP;
            else {
                valid = fail(error, error_size, path, line_number,
                             "unknown section");
                break;
            }
            continue;
        }
        char *equals = strchr(text, '=');
        if (equals == NULL || section == SECTION_NONE) {
            valid = fail(error, error_size, path, line_number,
                         "expected key=value inside a section");
            break;
        }
        *equals = '\0';
        char *key = trim(text);
        char *value = trim(equals + 1);
        unsigned int bit = 0;
        bool parsed = false;
        if (section == SECTION_DAEMON && strcmp(key, "enabled") == 0) {
            bit = SEEN_ENABLED;
            parsed = parse_bool(value, &config->daemon_enabled);
        } else if (section == SECTION_RADIO && strcmp(key, "mode") == 0) {
            bit = SEEN_MODE;
            parsed = flex1500_parse_radio_mode(value, &config->radio_mode);
        } else if (section == SECTION_HTTP && strcmp(key, "bind") == 0) {
            bit = SEEN_BIND;
            size_t length = strlen(value);
            parsed = length > 0 && length < sizeof(config->http_bind);
            if (parsed) memcpy(config->http_bind, value, length + 1);
        } else if (section == SECTION_HTTP && strcmp(key, "port") == 0) {
            bit = SEEN_PORT;
            char *end = NULL;
            errno = 0;
            unsigned long port = strtoul(value, &end, 10);
            parsed = errno == 0 && end != value && *end == '\0' &&
                     port > 0 && port <= 65535;
            if (parsed) config->http_port = (uint16_t)port;
        } else if (section == SECTION_HTTP && strcmp(key, "test_page") == 0) {
            bit = SEEN_TEST_PAGE;
            parsed = parse_bool(value, &config->test_page_enabled);
        } else {
            valid = fail(error, error_size, path, line_number, "unknown key");
            break;
        }
        if ((seen & bit) != 0) {
            valid = fail(error, error_size, path, line_number, "duplicate key");
            break;
        }
        if (!parsed) {
            valid = fail(error, error_size, path, line_number, "invalid value");
            break;
        }
        seen |= bit;
    }
    if (valid && ferror(file))
        valid = fail(error, error_size, path, 0, "read error");
    fclose(file);
    return valid;
}

void flex1500_config_print(const flex1500_config *config, const char *path)
{
    printf("# effective configuration (%s)\n", path);
    printf("[daemon]\nenabled=%s\n\n", config->daemon_enabled ? "true" : "false");
    printf("[radio]\nmode=%s\n\n", flex1500_radio_mode_name(config->radio_mode));
    printf("[http]\nbind=%s\nport=%u\ntest_page=%s\n",
           config->http_bind, (unsigned int)config->http_port,
           config->test_page_enabled ? "true" : "false");
}
