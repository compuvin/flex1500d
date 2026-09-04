// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/usb_io.h"

bool flex1500_usb_io_init(flex1500_usb_io *io,
                          const flex1500_usb_io_ops *ops, void *context)
{
    if (io == NULL || ops == NULL || ops->send_command == NULL ||
        ops->start_tx_stream == NULL || ops->service_tx_stream == NULL ||
        ops->stop_tx_stream == NULL) return false;
    *io = (flex1500_usb_io){.ops = ops, .context = context};
    return true;
}

int flex1500_usb_io_send_command(flex1500_usb_io *io,
                                 const uint8_t *packet, size_t bytes,
                                 size_t *transferred)
{
    if (io == NULL || io->ops == NULL || packet == NULL || bytes == 0 ||
        transferred == NULL) return -1;
    *transferred = 0;
    return io->ops->send_command(io->context, packet, bytes, transferred);
}

int flex1500_usb_io_start_tx_stream(flex1500_usb_io *io,
                                    bool generated_audio)
{
    if (io == NULL || io->ops == NULL) return -1;
    return io->ops->start_tx_stream(io->context, generated_audio);
}

int flex1500_usb_io_service_tx_stream(flex1500_usb_io *io,
                                      uint64_t duration_ms)
{
    if (io == NULL || io->ops == NULL) return -1;
    return io->ops->service_tx_stream(io->context, duration_ms);
}

void flex1500_usb_io_stop_tx_stream(flex1500_usb_io *io)
{
    if (io != NULL && io->ops != NULL) io->ops->stop_tx_stream(io->context);
}
