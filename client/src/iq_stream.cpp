// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/iq_stream.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace flex1500::client {
namespace {

std::uint32_t be32(const std::uint8_t *bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | bytes[3];
}

int open_socket(const std::string &host, std::uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *addresses = nullptr;
    const std::string service = std::to_string(port);
    const int lookup = getaddrinfo(host.c_str(), service.c_str(), &hints,
                                   &addresses);
    if (lookup != 0)
        throw std::runtime_error("resolve " + host + ": " +
                                 gai_strerror(lookup));
    int connected = -1;
    int saved_error = ECONNREFUSED;
    for (addrinfo *address = addresses; address != nullptr;
         address = address->ai_next) {
        const int fd = socket(address->ai_family, address->ai_socktype,
                              address->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            connected = fd;
            break;
        }
        saved_error = errno;
        ::close(fd);
    }
    freeaddrinfo(addresses);
    if (connected < 0)
        throw std::runtime_error("connect IQ stream: " +
                                 std::string(std::strerror(saved_error)));
    return connected;
}

void send_request(int fd, const std::string &host)
{
    const std::string request =
        "GET /v1/stream/iq HTTP/1.1\r\nHost: " + host +
        "\r\nConnection: close\r\nAccept: application/octet-stream\r\n\r\n";
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t count = send(fd, request.data() + sent,
                                   request.size() - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("send IQ stream request failed");
        sent += static_cast<std::size_t>(count);
    }
}

} // namespace

IqStream::IqStream(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port)
{
}

IqStream::~IqStream()
{
    close();
}

void IqStream::connect()
{
    close();
    const int socket = open_socket(host_, port_);
    socket_.store(socket);
    send_request(socket, host_);
    pending_.clear();
    for (;;) {
        std::uint8_t buffer[4096];
        const ssize_t count = recv(socket, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("IQ HTTP response ended early");
        pending_.insert(pending_.end(), buffer, buffer + count);
        const std::string text(pending_.begin(), pending_.end());
        const std::size_t end = text.find("\r\n\r\n");
        if (end == std::string::npos) continue;
        if (text.rfind("HTTP/1.1 200", 0) != 0 &&
            text.rfind("HTTP/1.0 200", 0) != 0)
            throw std::runtime_error("daemon rejected IQ stream");
        pending_.erase(pending_.begin(), pending_.begin() +
                       static_cast<std::ptrdiff_t>(end + 4));
        break;
    }
    have_sequence_ = false;
}

void IqStream::receive_until(std::size_t bytes)
{
    while (pending_.size() < bytes) {
        std::uint8_t buffer[8192];
        const int socket = socket_.load();
        if (socket < 0) throw std::runtime_error("IQ stream stopped");
        const ssize_t count = recv(socket, buffer, sizeof(buffer), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("IQ stream disconnected");
        pending_.insert(pending_.end(), buffer, buffer + count);
    }
}

std::vector<flex1500_iq_sample> IqStream::read_frame()
{
    receive_until(20);
    if (std::memcmp(pending_.data(), "F15I", 4) != 0 ||
        pending_[4] != 1 || pending_[5] != 1 ||
        pending_[6] != 0 || pending_[7] != 20 ||
        be32(pending_.data() + 12) != 48000)
        throw std::runtime_error("invalid F15I stream frame");
    const std::uint32_t sequence = be32(pending_.data() + 8);
    const std::uint32_t count = be32(pending_.data() + 16);
    if (count > 65536) throw std::runtime_error("oversized F15I stream frame");
    const std::size_t bytes = 20 + static_cast<std::size_t>(count) * 8;
    receive_until(bytes);

    if (have_sequence_ && sequence != expected_sequence_)
        throw std::runtime_error("lost F15I stream frame");
    expected_sequence_ = sequence + 1;
    have_sequence_ = true;

    std::vector<flex1500_iq_sample> samples(count);
    for (std::size_t index = 0; index < count; ++index) {
        std::memcpy(&samples[index].i, pending_.data() + 20 + index * 8, 4);
        std::memcpy(&samples[index].q, pending_.data() + 24 + index * 8, 4);
    }
    pending_.erase(pending_.begin(), pending_.begin() +
                   static_cast<std::ptrdiff_t>(bytes));
    return samples;
}

void IqStream::close()
{
    const int socket = socket_.exchange(-1);
    if (socket >= 0) {
        shutdown(socket, SHUT_RDWR);
        ::close(socket);
    }
}

} // namespace flex1500::client
