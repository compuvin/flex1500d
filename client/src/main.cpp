// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/http_client.hpp"
#include "flex1500/client/audio_ring.hpp"
#include "flex1500/client/iq_stream.hpp"
#include "flex1500/client/pipewire_source.hpp"
#include "flex1500/client/rig_control.hpp"
extern "C" {
#include "flex1500/dsp.h"
}

#include <atomic>
#include <chrono>
#include <cctype>
#include <csignal>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic_bool stop_requested{false};
constexpr float pipewire_receive_gain = 0.1f;

void request_stop(int)
{
    stop_requested.store(true);
}

void usage(const char *program)
{
    std::cout << "Usage: " << program
              << " [--host HOST] [--port PORT] [--rigctl-port PORT]\n"
              << "       " << program << " --version\n\n"
              << "Connects to flex1500d, acquires station control when "
                 "available, and renews it until stopped.\n";
}

std::uint16_t parse_port(const std::string &text)
{
    std::size_t consumed = 0;
    const unsigned long value = std::stoul(text, &consumed);
    if (consumed != text.size() || value == 0 || value > 65535) {
        throw std::runtime_error("invalid TCP port: " + text);
    }
    return static_cast<std::uint16_t>(value);
}

std::optional<std::string> json_string(const std::string &json,
                                       const std::string &name)
{
    const std::string key = "\"" + name + "\"";
    std::size_t position = json.find(key);
    if (position == std::string::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return std::nullopt;
    position = json.find('"', position + 1);
    if (position == std::string::npos) return std::nullopt;
    const std::size_t end = json.find('"', position + 1);
    if (end == std::string::npos) return std::nullopt;
    return json.substr(position + 1, end - position - 1);
}

std::optional<std::uint64_t> json_unsigned(const std::string &json,
                                           const std::string &name)
{
    const std::string key = "\"" + name + "\"";
    std::size_t position = json.find(key);
    if (position == std::string::npos) return std::nullopt;
    position = json.find(':', position + key.size());
    if (position == std::string::npos) return std::nullopt;
    ++position;
    while (position < json.size() &&
           std::isspace(static_cast<unsigned char>(json[position])))
        ++position;
    if (position == json.size() ||
        !std::isdigit(static_cast<unsigned char>(json[position])))
        return std::nullopt;
    std::size_t consumed = 0;
    const std::uint64_t value = std::stoull(json.substr(position), &consumed);
    return consumed == 0 ? std::nullopt
                         : std::optional<std::uint64_t>(value);
}

void require_ok(const char *operation,
                const flex1500::client::HttpResponse &response)
{
    if (response.status < 200 || response.status >= 300) {
        throw std::runtime_error(std::string(operation) + " returned HTTP " +
                                 std::to_string(response.status) + ": " +
                                 response.body);
    }
}

class BridgeSession {
public:
    BridgeSession(std::string host, std::uint16_t port)
        : host_(std::move(host)), client_(host_, port)
    {
    }

    ~BridgeSession() { release(); }

    void connect()
    {
        const auto status = client_.get("/v1/status");
        require_ok("daemon status", status);
        const auto radio = client_.get("/v1/radio");
        require_ok("radio status", radio);

        const auto version = json_string(status.body, "software_version")
                                 .value_or("unknown");
        const auto revision = json_string(status.body, "git_revision")
                                  .value_or("unknown");
        const auto model = json_string(radio.body, "model").value_or("radio");
        const auto frequency = json_unsigned(radio.body, "frequency_hz");
        const auto mode = json_string(radio.body, "rx_mode").value_or("am");
        const auto bandwidth = json_unsigned(radio.body, "rx_bandwidth_hz")
                                   .value_or(6000);
        std::cout << "[daemon] connected to " << host_ << "; flex1500d "
                  << version << " (" << revision << ")\n";
        std::cout << "[radio] " << model;
        if (frequency)
            std::cout << " at " << *frequency << " Hz";
        std::cout << ' ' << mode;
        std::cout << '\n';

        std::lock_guard<std::mutex> lock(mutex_);
        frequency_hz_ = frequency.value_or(0);
        mode_ = mode;
        bandwidth_hz_ = static_cast<std::uint32_t>(bandwidth);
        acquire();
        connected_ = true;
    }

    std::string mode() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return mode_;
    }

    flex1500::client::RigState rig_state() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string presented_mode = mode_;
        for (char &character : presented_mode)
            character = static_cast<char>(std::toupper(
                static_cast<unsigned char>(character)));
        return {frequency_hz_, presented_mode, bandwidth_hz_, lease_.has_value()};
    }

    bool set_frequency(std::uint64_t frequency_hz)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!lease_) return false;
        const auto response = client_.request(
            "PUT", "/v1/radio/frequency/" + std::to_string(frequency_hz),
            lease_headers());
        if (response.status < 200 || response.status >= 300) return false;
        frequency_hz_ = frequency_hz;
        std::cout << "[rig] frequency set to " << frequency_hz << " Hz\n";
        return true;
    }

    bool set_mode(const std::string &mode, std::int32_t bandwidth_hz)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!lease_) return false;
        std::string api_mode = mode;
        for (char &character : api_mode)
            character = static_cast<char>(std::tolower(
                static_cast<unsigned char>(character)));
        const std::uint32_t previous_bandwidth = bandwidth_hz_;
        const auto mode_response = client_.request(
            "PUT", "/v1/radio/mode/" + api_mode, lease_headers());
        if (mode_response.status < 200 || mode_response.status >= 300)
            return false;
        mode_ = api_mode;
        if (const auto applied = json_unsigned(mode_response.body,
                                               "rx_bandwidth_hz"))
            bandwidth_hz_ = static_cast<std::uint32_t>(*applied);
        const std::int32_t requested_bandwidth = bandwidth_hz == -1
            ? static_cast<std::int32_t>(previous_bandwidth) : bandwidth_hz;
        if (requested_bandwidth > 0) {
            const auto bandwidth_response = client_.request(
                "PUT", "/v1/radio/bandwidth/" +
                           std::to_string(requested_bandwidth),
                lease_headers());
            if (bandwidth_response.status < 200 ||
                bandwidth_response.status >= 300)
                return false;
            bandwidth_hz_ = static_cast<std::uint32_t>(requested_bandwidth);
        }
        std::cout << "[rig] mode set to " << mode << "; bandwidth "
                  << bandwidth_hz_ << " Hz\n";
        return true;
    }

    void maintain()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!connected_) return;
        if (!lease_) {
            acquire();
            return;
        }
        const auto response = client_.request(
            "PUT", "/v1/control/owner/keepalive", lease_headers());
        if (response.status == 200) return;
        if (response.status == 410 || response.status == 409) {
            std::cout << "[owner] station-control lease lost; receive-only\n";
            lease_.reset();
            return;
        }
        require_ok("station-control keepalive", response);
    }

    void release() noexcept
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!lease_) return;
        try {
            const auto response = client_.request(
                "DELETE", "/v1/control/owner", lease_headers());
            if (response.status == 200)
                std::cout << "[owner] station control released\n";
            else
                std::cerr << "[owner] release returned HTTP "
                          << response.status << "\n";
        } catch (const std::exception &error) {
            std::cerr << "[owner] release failed: " << error.what() << '\n';
        }
        lease_.reset();
    }

private:
    std::map<std::string, std::string> lease_headers() const
    {
        return {{"X-Flex1500-Control-Lease", std::to_string(*lease_)}};
    }

    void acquire()
    {
        const auto response = client_.request("POST", "/v1/control/owner");
        if (response.status == 201) {
            lease_ = json_unsigned(response.body, "lease");
            if (!lease_ || *lease_ == 0)
                throw std::runtime_error("owner response omitted its lease");
            reported_receive_only_ = false;
            std::cout << "[owner] station control acquired\n";
            return;
        }
        if (response.status == 409) {
            if (!reported_receive_only_) {
                std::cout << "[owner] another station owns control; "
                             "receive-only\n";
                reported_receive_only_ = true;
            }
            return;
        }
        if (response.status == 404) {
            if (!reported_receive_only_) {
                std::cout << "[owner] daemon has no station-control service; "
                             "receive-only\n";
                reported_receive_only_ = true;
            }
            return;
        }
        require_ok("station-control acquire", response);
    }

    std::string host_;
    flex1500::client::HttpClient client_;
    mutable std::mutex mutex_;
    std::optional<std::uint64_t> lease_;
    bool connected_ = false;
    bool reported_receive_only_ = false;
    std::string mode_ = "am";
    std::uint64_t frequency_hz_ = 0;
    std::uint32_t bandwidth_hz_ = 6000;
};

flex1500_demod_mode demod_mode(const std::string &name)
{
    flex1500_demod_mode mode = FLEX1500_DEMOD_AM;
    if (flex1500_parse_demod_mode(name.c_str(), &mode)) return mode;
    if (name == "cw") return FLEX1500_DEMOD_USB;
    throw std::runtime_error("unsupported daemon receive mode: " + name);
}

} // namespace

int main(int argc, char **argv)
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 15000;
    std::uint16_t rigctl_port = 4532;

    try {
        for (int index = 1; index < argc; ++index) {
            const std::string argument = argv[index];
            if (argument == "--help" || argument == "-h") {
                usage(argv[0]);
                return 0;
            }
            if (argument == "--version") {
                std::cout << "flex1500-client " << FLEX1500_CLIENT_VERSION
                          << " (" << FLEX1500_CLIENT_GIT_REVISION << ")\n";
                return 0;
            }
            if (argument == "--host" && index + 1 < argc) {
                host = argv[++index];
                continue;
            }
            if (argument == "--port" && index + 1 < argc) {
                port = parse_port(argv[++index]);
                continue;
            }
            if (argument == "--rigctl-port" && index + 1 < argc) {
                rigctl_port = parse_port(argv[++index]);
                continue;
            }
            throw std::runtime_error("unknown or incomplete option: " +
                                     argument);
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        BridgeSession session(host, port);
        session.connect();
        flex1500::client::AudioRing audio(96000);
        flex1500::client::PipeWireSource pipewire(audio);
        pipewire.start();
        std::cout << "[audio] PipeWire source ready: FLEX-1500 RX\n";

        flex1500_dsp dsp{};
        const flex1500_dsp_config dsp_config = {
            demod_mode(session.mode()), 48000.0f, 0.0f, true,
        };
        if (!flex1500_dsp_init(&dsp, &dsp_config))
            throw std::runtime_error("initialize receive DSP failed");

        flex1500::client::RigControlServer rig(
            rigctl_port,
            {
                [&session] { return session.rig_state(); },
                [&session](std::uint64_t frequency) {
                    return session.set_frequency(frequency);
                },
                [&session](const std::string &mode, std::int32_t bandwidth) {
                    return session.set_mode(mode, bandwidth);
                },
            });
        rig.start();
        std::cout << "[rig] Hamlib-compatible control ready on "
                  << "127.0.0.1:" << rigctl_port << '\n';

        flex1500::client::IqStream iq(host, port);
        std::thread receive_thread([&] {
            std::string active_mode = session.mode();
            while (!stop_requested.load()) {
                try {
                    iq.connect();
                    std::cout << "[audio] RX IQ stream connected\n";
                    while (!stop_requested.load()) {
                        auto samples = iq.read_frame();
                        // The FLEX/native API Q orientation is opposite the
                        // conventional complex spectrum used by client-side
                        // USB/LSB demodulation.  Match the established Soapy
                        // and rtl_tcp boundary conversion.
                        for (auto &sample : samples) sample.q = -sample.q;
                        const std::string requested_mode = session.mode();
                        if (requested_mode != active_mode) {
                            const flex1500_dsp_config updated = {
                                demod_mode(requested_mode), 48000.0f, 0.0f,
                                true,
                            };
                            if (!flex1500_dsp_init(&dsp, &updated))
                                throw std::runtime_error(
                                    "change receive DSP mode failed");
                            active_mode = requested_mode;
                            std::cout << "[audio] receive DSP changed to "
                                      << active_mode << '\n';
                        }
                        std::vector<float> demodulated(samples.size());
                        const std::size_t count = flex1500_dsp_process(
                            &dsp, samples.data(), samples.size(),
                            demodulated.data(), demodulated.size());
                        for (std::size_t index = 0; index < count; ++index)
                            demodulated[index] *= pipewire_receive_gain;
                        audio.push(demodulated.data(), count);
                    }
                } catch (const std::exception &error) {
                    if (stop_requested.load()) break;
                    std::cerr << "[audio] RX stream error: " << error.what()
                              << "; retrying\n";
                    for (int tenth = 0;
                         tenth < 20 && !stop_requested.load(); ++tenth)
                        std::this_thread::sleep_for(
                            std::chrono::milliseconds(100));
                }
            }
        });
        while (!stop_requested.load()) {
            for (int tenth = 0; tenth < 50 && !stop_requested.load(); ++tenth)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (!stop_requested.load()) session.maintain();
        }
        iq.close();
        receive_thread.join();
        rig.stop();
        pipewire.stop();
        std::cout << "[audio] RX stopped; dropped=" << audio.dropped()
                  << " underrun=" << audio.underruns() << '\n';
        session.release();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "flex1500-client: " << error.what() << '\n';
        return 1;
    }
}
