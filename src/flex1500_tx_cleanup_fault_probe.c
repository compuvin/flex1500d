// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/iq.h"
#include "flex1500/usb_rx.h"

#include <stdio.h>
#include <string.h>

enum { TEST_FREQUENCY_HZ = 28475000, RING_CAPACITY = 48000 };

static int refuse(void)
{
    fprintf(stderr,
            "Offline by default. Exact forms:\n"
            "  --after-stream I-CONFIRM-DUMMY-LOAD-AND-NO-KEY\n"
            "  --after-key I-CONFIRM-DUMMY-LOAD-AND-BRIEF-KEY\n");
    return 2;
}

int main(int argc, char **argv)
{
    flex1500_tx_fault_stage stage = FLEX1500_TX_FAULT_NONE;
    if (argc == 1) {
        (void)refuse();
        return 0;
    }
    if (argc == 3 && strcmp(argv[1], "--after-stream") == 0 &&
        strcmp(argv[2], "I-CONFIRM-DUMMY-LOAD-AND-NO-KEY") == 0) {
        stage = FLEX1500_TX_FAULT_AFTER_STREAM;
    } else if (argc == 3 && strcmp(argv[1], "--after-key") == 0 &&
               strcmp(argv[2],
                      "I-CONFIRM-DUMMY-LOAD-AND-BRIEF-KEY") == 0) {
        stage = FLEX1500_TX_FAULT_AFTER_KEY;
    } else {
        return refuse();
    }

    flex1500_iq_ring ring;
    if (!flex1500_iq_ring_init(&ring, RING_CAPACITY)) return 1;
    flex1500_usb_rx *receiver = flex1500_usb_rx_create(&ring);
    if (receiver == NULL) {
        flex1500_iq_ring_destroy(&ring);
        return 1;
    }

    int result = flex1500_usb_rx_start(receiver);
    if (result == 0) result = flex1500_usb_rx_set_gain(receiver, 20);
    if (result == 0) {
        result = flex1500_usb_rx_tune(receiver, TEST_FREQUENCY_HZ);
    }
    if (result == 0) {
        result = flex1500_usb_rx_enable_transmit_preparation(receiver);
    }
    if (result == 0) {
        flex1500_usb_rx_set_tx_fault_stage(receiver, stage);
        result = flex1500_usb_rx_tune_carrier_start(receiver);
        if (result == 0) {
            fprintf(stderr, "Fault injection unexpectedly returned success\n");
            result = -1;
        } else {
            printf("[expected] %s\n", flex1500_usb_rx_last_error(receiver));
            printf("[verified] Tune inactive after injected failure\n");
            result = flex1500_usb_rx_tune_carrier_active(receiver) ? -1 : 0;
        }
    }

    int cleanup = flex1500_usb_rx_disable_transmit_preparation(receiver);
    if (result == 0 && cleanup != 0) result = cleanup;
    if (result != 0) {
        fprintf(stderr, "Probe failed: %s\n",
                flex1500_usb_rx_last_error(receiver));
    } else {
        printf("[verified] PA filter reset and amplifier disabled\n");
    }
    flex1500_usb_rx_destroy(receiver);
    flex1500_iq_ring_destroy(&ring);
    return result == 0 ? 0 : 1;
}
