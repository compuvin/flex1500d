// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_USB_IO_H
#define FLEX1500_USB_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct flex1500_usb_io_ops {
    int (*send_command)(void *context, const uint8_t *packet, size_t bytes,
                        size_t *transferred);
    int (*start_tx_stream)(void *context, bool generated_audio);
    int (*service_tx_stream)(void *context, uint64_t duration_ms);
    void (*stop_tx_stream)(void *context);
} flex1500_usb_io_ops;

typedef struct flex1500_usb_io {
    const flex1500_usb_io_ops *ops;
    void *context;
} flex1500_usb_io;

bool flex1500_usb_io_init(flex1500_usb_io *io,
                          const flex1500_usb_io_ops *ops, void *context);
int flex1500_usb_io_send_command(flex1500_usb_io *io,
                                 const uint8_t *packet, size_t bytes,
                                 size_t *transferred);
int flex1500_usb_io_start_tx_stream(flex1500_usb_io *io,
                                    bool generated_audio);
int flex1500_usb_io_service_tx_stream(flex1500_usb_io *io,
                                      uint64_t duration_ms);
void flex1500_usb_io_stop_tx_stream(flex1500_usb_io *io);

#endif
