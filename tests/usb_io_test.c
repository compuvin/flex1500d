// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/usb_io.h"

#include "test_assert.h"

#include <string.h>

typedef struct mock_usb {
    unsigned int operation;
    unsigned int fail_operation;
    unsigned int commands;
    unsigned int starts;
    unsigned int services;
    unsigned int stops;
    bool generated_audio;
    uint64_t serviced_ms;
    uint8_t last_packet[20];
} mock_usb;

static int result(mock_usb *mock)
{
    ++mock->operation;
    return mock->operation == mock->fail_operation ? -17 : 0;
}

static int send_command(void *context, const uint8_t *packet, size_t bytes,
                        size_t *transferred)
{
    mock_usb *mock = context;
    ++mock->commands;
    if (result(mock) != 0) return -17;
    memcpy(mock->last_packet, packet, bytes);
    *transferred = bytes;
    return 0;
}

static int start_stream(void *context, bool generated_audio)
{
    mock_usb *mock = context;
    ++mock->starts;
    mock->generated_audio = generated_audio;
    return result(mock);
}

static int service_stream(void *context, uint64_t duration_ms)
{
    mock_usb *mock = context;
    ++mock->services;
    mock->serviced_ms += duration_ms;
    return result(mock);
}

static void stop_stream(void *context)
{
    mock_usb *mock = context;
    ++mock->stops;
    ++mock->operation;
}

static const flex1500_usb_io_ops MOCK_OPS = {
    send_command, start_stream, service_stream, stop_stream};

int main(void)
{
    CHECK(!flex1500_usb_io_init(NULL, &MOCK_OPS, NULL));
    flex1500_usb_io io;
    flex1500_usb_io_ops incomplete = {0};
    CHECK(!flex1500_usb_io_init(&io, &incomplete, NULL));

    for (unsigned int failure = 1; failure <= 3; ++failure) {
        mock_usb mock = {.fail_operation = failure};
        CHECK(flex1500_usb_io_init(&io, &MOCK_OPS, &mock));
        const uint8_t packet[20] = {0x7e, 0x04};
        size_t transferred = 99;
        int command = flex1500_usb_io_send_command(
            &io, packet, sizeof(packet), &transferred);
        int start = flex1500_usb_io_start_tx_stream(&io, true);
        int service = flex1500_usb_io_service_tx_stream(&io, 250);
        flex1500_usb_io_stop_tx_stream(&io);
        CHECK((failure == 1) == (command == -17));
        CHECK((failure == 2) == (start == -17));
        CHECK((failure == 3) == (service == -17));
        CHECK(mock.commands == 1 && mock.starts == 1);
        CHECK(mock.services == 1 && mock.stops == 1);
    }

    mock_usb mock = {0};
    CHECK(flex1500_usb_io_init(&io, &MOCK_OPS, &mock));
    const uint8_t packet[20] = {0x7e, 0x04};
    size_t transferred = 0;
    CHECK(flex1500_usb_io_send_command(
              &io, packet, sizeof(packet), &transferred) == 0);
    CHECK(transferred == sizeof(packet));
    CHECK(memcmp(packet, mock.last_packet, sizeof(packet)) == 0);
    CHECK(flex1500_usb_io_start_tx_stream(&io, false) == 0);
    CHECK(!mock.generated_audio);
    CHECK(flex1500_usb_io_service_tx_stream(&io, 200) == 0);
    flex1500_usb_io_stop_tx_stream(&io);
    CHECK(mock.serviced_ms == 200 && mock.stops == 1);
    return 0;
}
