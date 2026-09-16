// SPDX-License-Identifier: GPL-3.0-only

#define _POSIX_C_SOURCE 200809L

#include "flex1500/config.h"
#include "test_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool write_config(const char *path, const char *contents)
{
    FILE *file = fopen(path, "w");
    if (file == NULL) return false;
    bool ok = fputs(contents, file) >= 0;
    if (fclose(file) != 0) ok = false;
    return ok;
}

int main(void)
{
    flex1500_config config;
    flex1500_config_defaults(&config);
    CHECK(config.daemon_enabled);
    CHECK(config.radio_mode == FLEX1500_RADIO_RX_TUNING);
    CHECK(config.http_port == 15000);
    CHECK(!config.rtl_tcp_enabled);
    CHECK(config.rtl_tcp_port == 1234);

    char path[] = "/tmp/flex1500-config-test-XXXXXX";
    int fd = mkstemp(path);
    CHECK(fd >= 0);
    close(fd);
    CHECK(write_config(path,
        "[daemon]\nenabled=yes\n[radio]\nmode=transmit\n"
        "[http]\nbind=127.0.0.1\nport=16000\ntest_page=true\n"
        "[rtl_tcp]\nenabled=true\nbind=127.0.0.2\nport=1235\n"));
    char error[256];
    CHECK(flex1500_config_load(&config, path, false, error, sizeof(error)));
    CHECK(config.daemon_enabled);
    CHECK(config.radio_mode == FLEX1500_RADIO_TRANSMIT);
    CHECK(strcmp(config.http_bind, "127.0.0.1") == 0);
    CHECK(config.http_port == 16000);
    CHECK(config.test_page_enabled);
    CHECK(config.rtl_tcp_enabled);
    CHECK(strcmp(config.rtl_tcp_bind, "127.0.0.2") == 0);
    CHECK(config.rtl_tcp_port == 1235);

    CHECK(write_config(path, "[http]\nport=15000\nport=16000\n"));
    flex1500_config_defaults(&config);
    CHECK(!flex1500_config_load(&config, path, false, error, sizeof(error)));
    CHECK(strstr(error, "duplicate key") != NULL);
    unlink(path);

    flex1500_radio_mode mode;
    CHECK(flex1500_parse_radio_mode("rx-tuning", &mode));
    CHECK(mode == FLEX1500_RADIO_RX_TUNING);
    CHECK(!flex1500_parse_radio_mode("tx", &mode));
    return 0;
}
