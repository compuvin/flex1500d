// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/network.h"
#include "flex1500/rtl_tcp.h"
#include "test_assert.h"

#include <string.h>

int main(void)
{
    uint8_t header[FLEX1500_RTL_TCP_HEADER_SIZE];
    flex1500_rtl_tcp_header(header);
    CHECK(memcmp(header, "RTL0", 4) == 0);
    for (size_t i = 4; i < sizeof(header); ++i) CHECK(header[i] == 0);

    flex1500_rtl_tcp_parser parser = {0};
    flex1500_rtl_tcp_command command;
    const uint8_t bytes[] = {0x01, 0x01, 0xb2, 0xb8, 0x60};
    for (size_t i = 0; i < sizeof(bytes) - 1; ++i)
        CHECK(!flex1500_rtl_tcp_parse_byte(&parser, bytes[i], &command));
    CHECK(flex1500_rtl_tcp_parse_byte(&parser, bytes[4], &command));
    CHECK(command.id == FLEX1500_RTL_TCP_SET_FREQUENCY);
    CHECK(command.parameter == 28489824);

    const flex1500_iq_sample samples[] = {
        {.i = -1.0f, .q = 0.0f},
        {.i = 1.0f, .q = 0.5f},
    };
    uint8_t framed[64];
    size_t framed_length = flex1500_encode_iq_frame(
        7, samples, 2, framed, sizeof(framed));
    uint8_t output[4];
    CHECK(flex1500_rtl_tcp_encode_iq(framed, framed_length, output,
                                     sizeof(output)) == sizeof(output));
    CHECK(output[0] == 0);
    CHECK(output[1] == 128);
    CHECK(output[2] == 255);
    CHECK(output[3] == 64);

    uint8_t repeated[8];
    CHECK(flex1500_rtl_tcp_encode_iq_repeated(
              framed, framed_length, 2, repeated, sizeof(repeated)) ==
          sizeof(repeated));
    CHECK(repeated[0] == 0 && repeated[1] == 128);
    CHECK(repeated[2] == 0 && repeated[3] == 128);
    CHECK(repeated[4] == 255 && repeated[5] == 64);
    CHECK(repeated[6] == 255 && repeated[7] == 64);
    return 0;
}
