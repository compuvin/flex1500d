// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_HTTP_CLIENT_HPP
#define FLEX1500_CLIENT_HTTP_CLIENT_HPP

#include <cstdint>
#include <map>
#include <string>

namespace flex1500::client {

struct HttpResponse {
    int status = 0;
    std::string body;
};

class HttpClient {
public:
    HttpClient(std::string host, std::uint16_t port);

    HttpResponse get(const std::string &path) const;
    HttpResponse request(
        const std::string &method, const std::string &path,
        const std::map<std::string, std::string> &headers = {},
        const std::string &body = {}) const;

private:
    std::string host_;
    std::uint16_t port_;
};

} // namespace flex1500::client

#endif
