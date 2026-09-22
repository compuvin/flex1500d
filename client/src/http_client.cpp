// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/http_client.hpp"

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>

#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>

namespace flex1500::client {
namespace {

class Socket {
public:
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { if (fd_ >= 0) close(fd_); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    int get() const { return fd_; }

private:
    int fd_;
};

int connect_to(const std::string &host, std::uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    const std::string service = std::to_string(port);
    addrinfo *addresses = nullptr;
    const int lookup = getaddrinfo(host.c_str(), service.c_str(), &hints,
                                   &addresses);
    if (lookup != 0) {
        throw std::runtime_error("resolve " + host + ": " +
                                 gai_strerror(lookup));
    }

    int connected = -1;
    int saved_error = ECONNREFUSED;
    for (addrinfo *address = addresses; address != nullptr;
         address = address->ai_next) {
        const int fd = socket(address->ai_family, address->ai_socktype,
                              address->ai_protocol);
        if (fd < 0) {
            saved_error = errno;
            continue;
        }
        if (connect(fd, address->ai_addr, address->ai_addrlen) == 0) {
            connected = fd;
            break;
        }
        saved_error = errno;
        close(fd);
    }
    freeaddrinfo(addresses);
    if (connected < 0) {
        throw std::runtime_error("connect to " + host + ':' + service +
                                 ": " + std::strerror(saved_error));
    }
    return connected;
}

void send_all(int fd, const std::string &request)
{
    std::size_t sent = 0;
    while (sent < request.size()) {
        const ssize_t count = send(fd, request.data() + sent,
                                   request.size() - sent, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) {
            throw std::runtime_error("send request: " +
                                     std::string(std::strerror(errno)));
        }
        sent += static_cast<std::size_t>(count);
    }
}

std::string receive_all(int fd)
{
    std::string response;
    char buffer[4096];
    for (;;) {
        const ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
        if (count > 0) {
            response.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) {
            throw std::runtime_error("receive response: " +
                                     std::string(std::strerror(errno)));
        }
        return response;
    }
}

HttpResponse parse_response(const std::string &response)
{
    const std::size_t first_space = response.find(' ');
    const std::size_t header_end = response.find("\r\n\r\n");
    if (first_space == std::string::npos || header_end == std::string::npos ||
        first_space + 4 > response.size()) {
        throw std::runtime_error("daemon returned an invalid HTTP response");
    }
    int status = 0;
    try {
        status = std::stoi(response.substr(first_space + 1, 3));
    } catch (const std::exception &) {
        throw std::runtime_error("daemon returned an invalid HTTP status");
    }
    return {status, response.substr(header_end + 4)};
}

} // namespace

HttpClient::HttpClient(std::string host, std::uint16_t port)
    : host_(std::move(host)), port_(port)
{
}

HttpResponse HttpClient::get(const std::string &path) const
{
    return request("GET", path);
}

HttpResponse HttpClient::request(
    const std::string &method, const std::string &path,
    const std::map<std::string, std::string> &headers,
    const std::string &body) const
{
    Socket socket(connect_to(host_, port_));
    std::string wire = method + " " + path + " HTTP/1.1\r\nHost: " + host_ +
        "\r\nConnection: close\r\nAccept: application/json\r\n";
    for (const auto &[name, value] : headers)
        wire += name + ": " + value + "\r\n";
    if (!body.empty()) {
        wire += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        wire += "Content-Type: application/json\r\n";
    }
    wire += "\r\n" + body;
    send_all(socket.get(), wire);
    return parse_response(receive_all(socket.get()));
}

} // namespace flex1500::client
