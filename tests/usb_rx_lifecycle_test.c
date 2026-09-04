// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/iq.h"
#include "flex1500/usb_rx.h"

#include "test_assert.h"
#include <stdint.h>

int main(void)
{
    flex1500_iq_ring ring;
    CHECK(flex1500_iq_ring_init(&ring, 1024));

    flex1500_usb_rx *receiver = flex1500_usb_rx_create(&ring);
    CHECK(receiver != NULL);
    CHECK(!flex1500_usb_rx_is_running(receiver));
    CHECK(flex1500_usb_rx_get_counters(receiver)->usb_packets == 0);
    CHECK(flex1500_usb_rx_get_iq_stats(receiver)->frames == 0);
    CHECK(flex1500_usb_rx_last_error(receiver)[0] == '\0');
    uint32_t value = 0;
    CHECK(flex1500_usb_rx_tune(receiver, 10000000) != 0);
    CHECK(flex1500_usb_rx_enable_transmit_preparation(receiver) != 0);
    CHECK(flex1500_usb_rx_disable_transmit_preparation(receiver) != 0);
    CHECK(!flex1500_usb_rx_transmit_prepared(receiver));
    CHECK(flex1500_usb_rx_tune_carrier_start(receiver) != 0);
    CHECK(flex1500_usb_rx_tune_carrier_stop(receiver) != 0);
    CHECK(!flex1500_usb_rx_tune_carrier_active(receiver));
    CHECK(flex1500_usb_rx_microphone_tx_start(
        receiver, FLEX1500_TX_USB, 50, 1.0f, false) != 0);
    CHECK(flex1500_usb_rx_microphone_tx_stop(receiver) != 0);
    CHECK(flex1500_usb_rx_microphone_tx_stats(receiver) != NULL);
    CHECK(flex1500_usb_rx_microphone_tx_stats(receiver)->microphone_frames ==
          0);
    CHECK(!flex1500_usb_rx_frequency(receiver, &value));
    CHECK(!flex1500_usb_rx_filter(receiver, &value));
    CHECK(flex1500_usb_rx_get_counters(receiver)->rx_tune_operations == 0);
    CHECK(flex1500_usb_rx_get_counters(receiver)->command_errors == 0);
    CHECK(flex1500_usb_rx_get_counters(receiver)->status_packets == 0);
    flex1500_physical_inputs inputs;
    CHECK(!flex1500_usb_rx_physical_inputs(receiver, &inputs));

    /* Destroying an unstarted receiver must remain entirely offline. */
    flex1500_usb_rx_destroy(receiver);
    flex1500_iq_ring_destroy(&ring);
    return 0;
}
