// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_TX_STREAM_HPP
#define FLEX1500_CLIENT_TX_STREAM_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace flex1500::client {

class TxStream {
public:
    TxStream(std::string host, std::uint16_t port);
    ~TxStream();
    TxStream(const TxStream &) = delete;
    TxStream &operator=(const TxStream &) = delete;

    void connect(std::uint64_t control_lease, std::uint64_t tx_lease);
    void send_samples(const void *data, std::size_t bytes);
    void close() noexcept;

private:
    std::string host_;
    std::uint16_t port_;
    std::atomic_int socket_{-1};
};

} // namespace flex1500::client

#endif
