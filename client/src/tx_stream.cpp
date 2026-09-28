// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/tx_stream.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace flex1500::client {
namespace {

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
        throw std::runtime_error("connect TX stream: " +
                                 std::string(std::strerror(saved_error)));
    return connected;
}

void send_all(int fd, const void *data, std::size_t bytes)
{
    const auto *input = static_cast<const std::uint8_t *>(data);
    std::size_t sent = 0;
    while (sent < bytes) {
        const ssize_t count = send(fd, input + sent, bytes - sent,
                                   MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0)
            throw std::runtime_error("send TX stream: " +
                                     std::string(std::strerror(errno)));
        sent += static_cast<std::size_t>(count);
    }
}

} // namespace

TxStream::TxStream(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port)
{
}

TxStream::~TxStream()
{
    close();
}

void TxStream::connect(std::uint64_t control_lease, std::uint64_t tx_lease)
{
    close();
    const int socket = open_socket(host_, port_);
    socket_.store(socket);
    try {
        const std::string request =
            "CONNECT /v1/tx/stream HTTP/1.1\r\nHost: " + host_ +
            "\r\nX-Flex1500-Control-Lease: " +
            std::to_string(control_lease) +
            "\r\nX-Flex1500-TX-Lease: " + std::to_string(tx_lease) +
            "\r\nConnection: keep-alive\r\n\r\n";
        send_all(socket, request.data(), request.size());
        std::string response;
        while (response.find("\r\n\r\n") == std::string::npos) {
            char buffer[1024];
            const ssize_t count = recv(socket, buffer, sizeof(buffer), 0);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0)
                throw std::runtime_error("TX stream response ended early");
            response.append(buffer, static_cast<std::size_t>(count));
            if (response.size() > 8192)
                throw std::runtime_error("oversized TX stream response");
        }
        if (response.rfind("HTTP/1.1 200", 0) != 0 &&
            response.rfind("HTTP/1.0 200", 0) != 0)
            throw std::runtime_error("daemon rejected TX stream");
    } catch (...) {
        close();
        throw;
    }
}

void TxStream::send_samples(const void *data, std::size_t bytes)
{
    const int socket = socket_.load();
    if (socket < 0) throw std::runtime_error("TX stream is not connected");
    send_all(socket, data, bytes);
}

void TxStream::close() noexcept
{
    const int socket = socket_.exchange(-1);
    if (socket >= 0) {
        shutdown(socket, SHUT_RDWR);
        ::close(socket);
    }
}

} // namespace flex1500::client
