// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/protocol.h"

#include <stdio.h>

int main(void)
{
    puts("FLEX-1500 known USB layout (offline data only)");
    printf("device: %04x:%04x\n", FLEX1500_USB_VENDOR_ID,
           FLEX1500_USB_PRODUCT_ID);
    printf("interface: %d\n", FLEX1500_STREAMING_INTERFACE);
    printf("endpoint 0x%02x: %s, %d bytes\n", FLEX1500_EP_SAMPLE_IN,
           flex1500_endpoint_name(FLEX1500_EP_SAMPLE_IN),
           FLEX1500_SAMPLE_PACKET_SIZE);
    printf("endpoint 0x%02x: %s, %d bytes\n", FLEX1500_EP_STATUS_IN,
           flex1500_endpoint_name(FLEX1500_EP_STATUS_IN),
           FLEX1500_STATUS_PACKET_SIZE);
    printf("endpoint 0x%02x: %s (blocked)\n", FLEX1500_EP_SAMPLE_OUT,
           flex1500_endpoint_name(FLEX1500_EP_SAMPLE_OUT));
    printf("endpoint 0x%02x: %s (blocked)\n", FLEX1500_EP_COMMAND_OUT,
           flex1500_endpoint_name(FLEX1500_EP_COMMAND_OUT));
    puts("No USB device was opened.");
    return 0;
}
