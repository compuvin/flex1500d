// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/http_client.hpp"
#include "flex1500/client/audio_ring.hpp"
#include "flex1500/client/iq_stream.hpp"
#include "flex1500/client/pipewire_source.hpp"
#include "flex1500/client/pipewire_sink.hpp"
#include "flex1500/client/rig_control.hpp"
#include "flex1500/client/tx_stream.hpp"
extern "C" {
#include "flex1500/dsp.h"
}

#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <csignal>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <cmath>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic_bool stop_requested{false};
constexpr float pipewire_receive_gain = 0.1f;
constexpr std::uint64_t default_frequency_hz = 28475000;
constexpr std::uint64_t tx_pacing_lead_frames = 4096;

struct ClientState {
    std::uint64_t frequency_hz = default_frequency_hz;
    std::string mode = "usb";
};

std::optional<std::filesystem::path> state_path()
{
    if (const char *directory = std::getenv("XDG_STATE_HOME");
        directory && *directory)
        return std::filesystem::path(directory) / "flex1500-client" /
               "state.conf";
    if (const char *directory = std::getenv("HOME"); directory && *directory)
        return std::filesystem::path(directory) / ".local" / "state" /
               "flex1500-client" / "state.conf";
    return std::nullopt;
}

ClientState load_state()
{
    ClientState state;
    const auto path = state_path();
    if (!path) return state;
    std::ifstream input(*path);
    std::string line;
    while (std::getline(input, line)) {
        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) continue;
        const std::string name = line.substr(0, separator);
        const std::string value = line.substr(separator + 1);
        try {
            if (name == "frequency_hz") {
                std::size_t consumed = 0;
                const auto frequency = std::stoull(value, &consumed);
                if (consumed == value.size() && frequency >= 100000 &&
                    frequency <= 54000000)
                    state.frequency_hz = frequency;
            } else if (name == "mode" &&
                       (value == "am" || value == "fm" || value == "usb" ||
                        value == "lsb" || value == "cw")) {
                state.mode = value;
            }
        } catch (const std::exception &) {
        }
    }
    return state;
}

void save_state(const ClientState &state)
{
    const auto path = state_path();
    if (!path) {
        std::cerr << "[state] HOME and XDG_STATE_HOME are unavailable; "
                     "state not saved\n";
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(path->parent_path(), error);
    if (error) {
        std::cerr << "[state] cannot create " << path->parent_path() << ": "
                  << error.message() << '\n';
        return;
    }
    const std::filesystem::path temporary = path->string() + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            std::cerr << "[state] cannot write " << temporary << '\n';
            return;
        }
        output << "frequency_hz=" << state.frequency_hz << '\n'
               << "mode=" << state.mode << '\n';
        if (!output) {
            std::cerr << "[state] failed while writing " << temporary << '\n';
            return;
        }
    }
    std::filesystem::rename(temporary, *path, error);
    if (error)
        std::cerr << "[state] cannot replace " << *path << ": "
                  << error.message() << '\n';
}

void request_stop(int)
{
    stop_requested.store(true);
}

void usage(const char *program)
{
    std::cout << "Usage: " << program
              << " [--host HOST] [--port PORT] [--rigctl-port PORT]"
                 " [--no-pipewire]\n"
              << "       " << program << " --version\n\n"
              << "Connects to flex1500d, acquires station control when "
                 "available, and renews it until stopped.\n"
              << "--no-pipewire is intended for headless integration tests; "
                 "it disables local RX and TX audio devices.\n";
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
    BridgeSession(std::string host, std::uint16_t port,
                  flex1500::client::AudioRing &transmit_audio,
                  ClientState restored_state)
        : host_(std::move(host)), client_(host_, port),
          tx_stream_(host_, port),
          mode_(std::move(restored_state.mode)),
          frequency_hz_(restored_state.frequency_hz),
          transmit_audio_(transmit_audio)
    {
    }

    ~BridgeSession()
    {
        set_ptt(false);
        release();
    }

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
        const bool restore_tuning = !frequency.has_value();
        if (frequency) {
            frequency_hz_ = *frequency;
            mode_ = mode;
        } else {
            std::cout << "[state] daemon has no tuning state; selected "
                      << frequency_hz_ << " Hz " << mode_ << " for restore\n";
        }
        bandwidth_hz_ = static_cast<std::uint32_t>(bandwidth);
        acquire();
        if (restore_tuning && lease_) {
            const auto mode_response = client_.request(
                "PUT", "/v1/radio/mode/" + mode_, lease_headers());
            require_ok("restore radio mode", mode_response);
            if (const auto applied = json_unsigned(mode_response.body,
                                                   "rx_bandwidth_hz"))
                bandwidth_hz_ = static_cast<std::uint32_t>(*applied);
            const auto frequency_response = client_.request(
                "PUT", "/v1/radio/frequency/" +
                           std::to_string(frequency_hz_),
                lease_headers());
            require_ok("restore radio frequency", frequency_response);
            std::cout << "[state] restored radio to " << frequency_hz_
                      << " Hz " << mode_ << '\n';
        }
        connected_ = true;
    }

    std::string mode() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return mode_;
    }

    ClientState client_state() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {frequency_hz_, mode_};
    }

    bool consume_tx_fault()
    {
        return tx_fault_pending_.exchange(false);
    }

    flex1500::client::RigState rig_state() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string presented_mode = mode_;
        for (char &character : presented_mode)
            character = static_cast<char>(std::toupper(
                static_cast<unsigned char>(character)));
        return {frequency_hz_, presented_mode, bandwidth_hz_,
                lease_.has_value(), tx_keyed_};
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

    bool set_ptt(bool enabled)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (!enabled) {
            tx_stop_.store(true);
            lock.unlock();
            if (tx_thread_.joinable()) tx_thread_.join();
            lock.lock();
            if (!tx_lease_) return true;
            const auto headers = tx_headers();
            const auto stopped = client_.request(
                "PUT", "/v1/tx/ptt/stop", headers);
            const auto released = client_.request(
                "DELETE", "/v1/tx/sessions/current", headers);
            tx_stream_.close();
            tx_keyed_ = false;
            tx_lease_.reset();
            if (stopped.status < 200 || stopped.status >= 300)
                std::cerr << "[tx] PTT stop failed: HTTP " << stopped.status
                          << ": " << stopped.body;
            if (released.status < 200 || released.status >= 300)
                std::cerr << "[tx] session release failed: HTTP "
                          << released.status << ": " << released.body;
            return stopped.status >= 200 && stopped.status < 300 &&
                   released.status >= 200 && released.status < 300;
        }
        if (tx_keyed_) return true;
        if (!lease_ || frequency_hz_ == 0 || mode_ == "fm" || mode_ == "cw")
            return false;
        transmit_audio_.clear();
        const std::string profile =
            "{\"mode\":\"" + mode_ +
            "\",\"drive_percent\":100,\"source\":\"audio\","
            "\"sample_format\":\"s16le\",\"sample_rate\":48000,"
            "\"channels\":1}";
        const auto created = client_.request(
            "POST", "/v1/tx/sessions", lease_headers(), profile);
        if (created.status != 201) {
            std::cerr << "[tx] session acquire failed: HTTP "
                      << created.status << ": " << created.body;
            return false;
        }
        tx_lease_ = json_unsigned(created.body, "lease");
        if (!tx_lease_) {
            std::cerr << "[tx] session acquire response omitted its lease\n";
            return false;
        }
        std::string silence(4096 * sizeof(std::int16_t), '\0');
        try {
            tx_stream_.connect(*lease_, *tx_lease_);
            tx_stream_.send_samples(silence.data(), silence.size());
        } catch (const std::exception &error) {
            std::cerr << "[tx] persistent stream setup failed: "
                      << error.what() << '\n';
            release_tx_locked();
            return false;
        }
        flex1500::client::HttpResponse started;
        const auto readiness_deadline = std::chrono::steady_clock::now() +
                                        std::chrono::seconds(2);
        do {
            started = client_.request(
                "PUT", "/v1/tx/ptt/start", tx_headers());
            if (started.status != 409 ||
                started.body.find("\"error\":\"tx_not_ready\"") ==
                    std::string::npos)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        } while (std::chrono::steady_clock::now() < readiness_deadline);
        if (started.status < 200 || started.status >= 300) {
            std::cerr << "[tx] PTT start failed: HTTP " << started.status
                      << ": " << started.body;
            release_tx_locked();
            return false;
        }
        tx_keyed_ = true;
        tx_stop_.store(false);
        tx_thread_ = std::thread([this] { upload_tx_audio(); });
        std::cout << "[tx] Hamlib PTT keyed using FLEX-1500 TX audio\n";
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

    std::map<std::string, std::string> tx_headers() const
    {
        auto headers = lease_headers();
        headers["X-Flex1500-TX-Lease"] = std::to_string(*tx_lease_);
        return headers;
    }

    void release_tx_locked() noexcept
    {
        if (!tx_lease_) return;
        try {
            const auto response = client_.request(
                "DELETE", "/v1/tx/sessions/current", tx_headers());
            if (response.status < 200 || response.status >= 300)
                std::cerr << "[tx] session cleanup failed: HTTP "
                          << response.status << ": " << response.body;
        } catch (const std::exception &error) {
            std::cerr << "[tx] session cleanup failed: " << error.what()
                      << '\n';
        }
        tx_stream_.close();
        tx_keyed_ = false;
        tx_lease_.reset();
    }

    void fail_tx_locked(const char *operation,
                        const flex1500::client::HttpResponse &response)
    {
        std::cerr << "[tx] " << operation << " failed: HTTP "
                  << response.status << ": " << response.body;
        tx_stop_.store(true);
        release_tx_locked();
        tx_fault_pending_.store(true);
    }

    void fail_tx_locked(const char *operation, const std::exception &error)
    {
        std::cerr << "[tx] " << operation << " failed: " << error.what()
                  << '\n';
        tx_stop_.store(true);
        release_tx_locked();
        tx_fault_pending_.store(true);
    }

    void upload_tx_audio()
    {
        std::vector<float> input(4800);
        auto last_keepalive = std::chrono::steady_clock::now();
        const auto pace_epoch = std::chrono::steady_clock::now();
        std::uint64_t paced_frames = 0;
        while (!tx_stop_.load()) {
            const auto now = std::chrono::steady_clock::now();
            if (now - last_keepalive >= std::chrono::seconds(5)) {
                try {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!tx_lease_) return;
                    const auto keepalive = client_.request(
                        "PUT", "/v1/tx/sessions/keepalive", tx_headers());
                    if (keepalive.status < 200 || keepalive.status >= 300) {
                        fail_tx_locked("TX keepalive", keepalive);
                        return;
                    }
                    last_keepalive = now;
                } catch (const std::exception &error) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    fail_tx_locked("TX keepalive", error);
                    return;
                }
            }
            const auto pace_now = std::chrono::steady_clock::now();
            const auto elapsed_us = std::chrono::duration_cast<
                std::chrono::microseconds>(pace_now - pace_epoch).count();
            const std::uint64_t elapsed_frames = elapsed_us > 0
                ? static_cast<std::uint64_t>(elapsed_us) * 48000 / 1000000
                : 0;
            const std::uint64_t budget = elapsed_frames +
                                         tx_pacing_lead_frames;
            if (budget <= paced_frames) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            const std::size_t permitted = static_cast<std::size_t>(
                std::min<std::uint64_t>(input.size(), budget - paced_frames));
            const std::size_t count = transmit_audio_.pop(
                input.data(), permitted);
            if (count == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            std::string pcm(count * sizeof(std::int16_t), '\0');
            for (std::size_t index = 0; index < count; ++index) {
                const float sample = input[index];
                const std::int16_t output = static_cast<std::int16_t>(std::lrint(
                    std::max(-1.0f, std::min(1.0f, sample)) * 32767.0f));
                pcm[index * 2] = static_cast<char>(output & 0xff);
                pcm[index * 2 + 1] = static_cast<char>(
                    (static_cast<std::uint16_t>(output) >> 8) & 0xff);
            }
            try {
                tx_stream_.send_samples(pcm.data(), pcm.size());
                paced_frames += count;
            } catch (const std::exception &error) {
                std::lock_guard<std::mutex> lock(mutex_);
                fail_tx_locked("persistent audio stream", error);
                return;
            }
        }
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
    flex1500::client::TxStream tx_stream_;
    mutable std::mutex mutex_;
    std::optional<std::uint64_t> lease_;
    std::optional<std::uint64_t> tx_lease_;
    bool connected_ = false;
    bool reported_receive_only_ = false;
    std::string mode_ = "am";
    std::uint64_t frequency_hz_ = 0;
    std::uint32_t bandwidth_hz_ = 6000;
    flex1500::client::AudioRing &transmit_audio_;
    std::atomic_bool tx_stop_{false};
    std::atomic_bool tx_fault_pending_{false};
    bool tx_keyed_ = false;
    std::thread tx_thread_;
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
    bool pipewire_enabled = true;

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
            if (argument == "--no-pipewire") {
                pipewire_enabled = false;
                continue;
            }
            throw std::runtime_error("unknown or incomplete option: " +
                                     argument);
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        flex1500::client::AudioRing audio(96000);
        flex1500::client::AudioRing transmit_audio(4800);
        BridgeSession session(host, port, transmit_audio, load_state());
        session.connect();
        std::optional<flex1500::client::PipeWireSource> pipewire;
        std::optional<flex1500::client::PipeWireSink> pipewire_tx;
        if (pipewire_enabled) {
            pipewire.emplace(audio);
            pipewire_tx.emplace(transmit_audio);
            pipewire->start();
            std::cout << "[audio] PipeWire source ready: FLEX-1500 RX\n";
            pipewire_tx->start();
            std::cout << "[audio] PipeWire sink ready: FLEX-1500 TX "
                         "(PTT remains disabled)\n";
        } else {
            std::cout << "[audio] PipeWire disabled for headless test\n";
        }

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
                [&session](bool enabled) { return session.set_ptt(enabled); },
                [&session] { return session.consume_tx_fault(); },
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
        if (pipewire) pipewire->stop();
        if (pipewire_tx) pipewire_tx->stop();
        std::cout << "[audio] RX stopped; dropped=" << audio.dropped()
                  << " underrun=" << audio.underruns() << '\n';
        save_state(session.client_state());
        session.release();
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "flex1500-client: " << error.what() << '\n';
        return 1;
    }
}
