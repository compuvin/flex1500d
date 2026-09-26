// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/rig_control.hpp"

#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace flex1500::client {
namespace {

constexpr int invalid_argument = -1;
constexpr int not_implemented = -4;
constexpr int rejected = -9;

struct CommandResult {
    std::string response;
    bool close = false;
};

std::string uppercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return text;
}

bool supported_mode(const std::string &mode)
{
    return mode == "AM" || mode == "FM" || mode == "USB" ||
           mode == "LSB" || mode == "CW";
}

bool supported_vfo(const std::string &vfo)
{
    return vfo == "VFOA" || vfo == "currVFO";
}

std::string dump_state()
{
    // Hamlib's NET rigctl backend still consumes the version-0 positional
    // capability dump while opening a connection.  Keep this deliberately
    // conservative: one receive VFO, the FLEX-1500 tuning range, the modes
    // exposed by this bridge, and no advertised transmitter/PTT capability.
    return "0\n"                 // dump protocol version
           "2\n"                 // NET rigctl model
           "1\n"                 // ITU region
           "100000 54000000 0x2f -1 -1 0x1 0x0\n"
           "0 0 0 0 0 0 0\n"     // end RX ranges
           "0 0 0 0 0 0 0\n"     // no TX ranges
           "0x2f 1\n"             // tuning step
           "0 0\n"                // end tuning steps
           "0x1 6000\n"           // AM filter
           "0x20 12000\n"         // FM filter
           "0xc 2700\n"           // USB/LSB filter
           "0x2 500\n"            // CW filter
           "0 0\n"                // end filters
           "0\n"                  // max RIT
           "0\n"                  // max XIT
           "0\n"                  // max IF shift
           "0\n"                  // announce
           "\n"                   // no preamp values
           "\n"                   // no attenuator values
           "0\n"                  // has get_func
           "0\n"                  // has set_func
           "0\n"                  // has get_level
           "0\n"                  // has set_level
           "0\n"                  // has get_parm
           "0\n";                 // has set_parm
}

CommandResult execute(const std::string &line,
                      const RigControlCallbacks &callbacks)
{
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command.empty()) return {};
    if (command == "q" || command == "Q" || command == "\\quit")
        return {{}, true};

    if (command == "\\chk_vfo") return {"CHKVFO 1\n", false};
    if (command == "\\dump_state") return {dump_state(), false};
    if (command == "v" || command == "\\get_vfo")
        return {"VFOA\n", false};
    if (command == "V" || command == "\\set_vfo") {
        std::string vfo;
        input >> vfo;
        return {(vfo == "VFOA" || vfo == "currVFO")
                    ? "RPRT 0\n" : "RPRT -1\n", false};
    }

    if (command == "f" || command == "\\get_freq") {
        const RigState state = callbacks.state();
        if (state.frequency_hz == 0) return {"RPRT -11\n", false};
        return {std::to_string(state.frequency_hz) + "\n", false};
    }
    if (command == "F" || command == "\\set_freq") {
        std::string argument;
        if (!(input >> argument))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        if (supported_vfo(argument) && !(input >> argument))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        std::size_t consumed = 0;
        double frequency = 0.0;
        try {
            frequency = std::stod(argument, &consumed);
        } catch (const std::exception &) {
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        }
        if (consumed != argument.size() || !std::isfinite(frequency) ||
            frequency < 100000.0 || frequency > 54000000.0)
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        const RigState state = callbacks.state();
        if (!state.writable)
            return {"RPRT " + std::to_string(rejected) + "\n", false};
        const auto rounded = static_cast<std::uint64_t>(std::llround(frequency));
        return {callbacks.set_frequency(rounded) ? "RPRT 0\n"
                                                 : "RPRT -9\n", false};
    }

    if (command == "m" || command == "\\get_mode") {
        const RigState state = callbacks.state();
        return {state.mode + "\n" + std::to_string(state.bandwidth_hz) +
                    "\n", false};
    }
    if (command == "M" || command == "\\set_mode") {
        std::string mode;
        std::int32_t bandwidth = 0;
        if (!(input >> mode))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        if (supported_vfo(mode) && !(input >> mode))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        if (!(input >> bandwidth))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        mode = uppercase(mode);
        if (mode == "?") return {"AM FM USB LSB CW\n", false};
        if (!supported_mode(mode) || bandwidth < -1 || bandwidth > 20000)
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        const RigState state = callbacks.state();
        if (!state.writable)
            return {"RPRT " + std::to_string(rejected) + "\n", false};
        return {callbacks.set_mode(mode, bandwidth) ? "RPRT 0\n"
                                                    : "RPRT -9\n", false};
    }

    if (command == "t" || command == "\\get_ptt")
        return {"0\n", false};
    if (command == "T" || command == "\\set_ptt") {
        std::string argument;
        int ptt = -1;
        if (!(input >> argument))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        if (supported_vfo(argument) && !(input >> argument))
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        std::size_t consumed = 0;
        try {
            ptt = std::stoi(argument, &consumed);
        } catch (const std::exception &) {
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        }
        if (consumed != argument.size() || ptt < 0 || ptt > 3)
            return {"RPRT " + std::to_string(invalid_argument) + "\n", false};
        return {ptt == 0 ? "RPRT 0\n"
                         : "RPRT " + std::to_string(not_implemented) + "\n",
                false};
    }
    return {"RPRT " + std::to_string(not_implemented) + "\n", false};
}

bool send_all(int fd, const std::string &text)
{
    std::size_t sent = 0;
    while (sent < text.size()) {
        const ssize_t result = send(fd, text.data() + sent,
                                    text.size() - sent, MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR) continue;
        if (result <= 0) return false;
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

} // namespace

RigControlServer::RigControlServer(std::uint16_t port,
                                   RigControlCallbacks callbacks)
    : port_(port), callbacks_(std::move(callbacks))
{
}

RigControlServer::~RigControlServer()
{
    stop();
}

void RigControlServer::start()
{
    listener_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listener_ < 0)
        throw std::runtime_error("create rig-control socket: " +
                                 std::string(std::strerror(errno)));
    int reuse = 1;
    (void)setsockopt(listener_, SOL_SOCKET, SO_REUSEADDR, &reuse,
                     sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener_, reinterpret_cast<sockaddr *>(&address),
             sizeof(address)) != 0 || listen(listener_, 4) != 0) {
        const int saved = errno;
        close(listener_);
        listener_ = -1;
        throw std::runtime_error("listen on 127.0.0.1:" +
                                 std::to_string(port_) + ": " +
                                 std::strerror(saved));
    }
    stop_requested_.store(false);
    thread_ = std::thread(&RigControlServer::run, this);
}

void RigControlServer::stop() noexcept
{
    stop_requested_.store(true);
    if (listener_ >= 0) shutdown(listener_, SHUT_RDWR);
    if (thread_.joinable()) thread_.join();
    if (listener_ >= 0) close(listener_);
    listener_ = -1;
}

void RigControlServer::run()
{
    int client = -1;
    std::string pending;
    while (!stop_requested_.load()) {
        pollfd descriptors[2] = {
            {listener_, POLLIN, 0},
            {client, POLLIN, 0},
        };
        const nfds_t count = client >= 0 ? 2 : 1;
        const int ready = poll(descriptors, count, 200);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) break;
        if ((descriptors[0].revents & POLLIN) != 0) {
            const int accepted = accept(listener_, nullptr, nullptr);
            if (accepted >= 0) {
                if (client >= 0) close(client);
                client = accepted;
                pending.clear();
                std::cout << "[rig] local Hamlib client connected\n";
            }
        }
        if (client < 0) continue;
        if ((descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
            close(client);
            client = -1;
            pending.clear();
            continue;
        }
        if ((descriptors[1].revents & POLLIN) == 0) continue;
        char buffer[1024];
        const ssize_t received = recv(client, buffer, sizeof(buffer), 0);
        if (received <= 0) {
            close(client);
            client = -1;
            pending.clear();
            continue;
        }
        pending.append(buffer, static_cast<std::size_t>(received));
        for (;;) {
            const std::size_t newline = pending.find('\n');
            if (newline == std::string::npos) break;
            std::string line = pending.substr(0, newline);
            pending.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            CommandResult result;
            try {
                result = execute(line, callbacks_);
            } catch (const std::exception &error) {
                std::cerr << "[rig] command failed: " << error.what() << '\n';
                result.response = "RPRT -6\n";
            }
            if ((!result.response.empty() &&
                 !send_all(client, result.response)) || result.close) {
                close(client);
                client = -1;
                pending.clear();
                break;
            }
        }
    }
    if (client >= 0) close(client);
}

} // namespace flex1500::client
