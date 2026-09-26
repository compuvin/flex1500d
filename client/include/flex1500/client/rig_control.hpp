// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_RIG_CONTROL_HPP
#define FLEX1500_CLIENT_RIG_CONTROL_HPP

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace flex1500::client {

struct RigState {
    std::uint64_t frequency_hz = 0;
    std::string mode = "AM";
    std::uint32_t bandwidth_hz = 6000;
    bool writable = false;
};

struct RigControlCallbacks {
    std::function<RigState()> state;
    std::function<bool(std::uint64_t)> set_frequency;
    std::function<bool(const std::string &, std::int32_t)> set_mode;
};

class RigControlServer {
public:
    RigControlServer(std::uint16_t port, RigControlCallbacks callbacks);
    ~RigControlServer();
    RigControlServer(const RigControlServer &) = delete;
    RigControlServer &operator=(const RigControlServer &) = delete;

    void start();
    void stop() noexcept;

private:
    void run();

    std::uint16_t port_;
    RigControlCallbacks callbacks_;
    std::atomic_bool stop_requested_{false};
    int listener_ = -1;
    std::thread thread_;
};

} // namespace flex1500::client

#endif
