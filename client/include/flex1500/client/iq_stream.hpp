// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_IQ_STREAM_HPP
#define FLEX1500_CLIENT_IQ_STREAM_HPP

extern "C" {
#include "flex1500/iq.h"
}

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace flex1500::client {

class IqStream {
public:
    IqStream(std::string host, std::uint16_t port);
    ~IqStream();
    IqStream(const IqStream &) = delete;
    IqStream &operator=(const IqStream &) = delete;

    void connect();
    std::vector<flex1500_iq_sample> read_frame();
    void close();

private:
    void receive_until(std::size_t bytes);

    std::string host_;
    std::uint16_t port_;
    std::atomic_int socket_{-1};
    std::vector<std::uint8_t> pending_;
    std::uint32_t expected_sequence_ = 0;
    bool have_sequence_ = false;
};

} // namespace flex1500::client

#endif
