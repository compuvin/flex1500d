// SPDX-License-Identifier: GPL-3.0-only

#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Errors.h>
#include <SoapySDR/Formats.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Types.hpp>
#include <SoapySDR/Version.hpp>

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>
#include <vector>

namespace {

constexpr double SAMPLE_RATE = 48000.0;
constexpr double MIN_FREQUENCY = 100000.0;
constexpr double MAX_FREQUENCY = 54000000.0;
constexpr std::size_t FRAME_HEADER_SIZE = 20;
constexpr std::size_t STREAM_MTU = 1024;

const SoapySDR::RangeList TX_FREQUENCY_RANGES = {
    {1824000.0, 1976000.0}, {3524000.0, 3976000.0},
    {7024000.0, 7276000.0}, {14024000.0, 14326000.0},
    {18092000.0, 18144000.0}, {21024000.0, 21426000.0},
    {24914000.0, 24966000.0}, {28024000.0, 29676000.0},
    {50024000.0, 53976000.0},
};

bool txFrequencyAllowed(double frequency)
{
    return std::any_of(TX_FREQUENCY_RANGES.begin(), TX_FREQUENCY_RANGES.end(),
        [frequency](const SoapySDR::Range &range) {
            return frequency >= range.minimum() && frequency <= range.maximum();
        });
}

class Socket {
public:
    Socket() = default;
    explicit Socket(int fd) : fd_(fd) {}
    ~Socket() { close(); }
    Socket(const Socket &) = delete;
    Socket &operator=(const Socket &) = delete;
    Socket(Socket &&other) noexcept : fd_(other.fd_) { other.fd_ = -1; }
    Socket &operator=(Socket &&other) noexcept
    {
        if (this != &other) {
            close();
            fd_ = other.fd_;
            other.fd_ = -1;
        }
        return *this;
    }
    int get() const { return fd_; }
    bool valid() const { return fd_ >= 0; }
    void close()
    {
        if (fd_ >= 0) ::close(fd_);
        fd_ = -1;
    }

private:
    int fd_ = -1;
};

struct HttpResponse {
    int status = 0;
    std::string body;
    Socket socket;
};

std::string argument(const SoapySDR::Kwargs &args, const char *key,
                     const char *fallback)
{
    const auto found = args.find(key);
    return found == args.end() ? fallback : found->second;
}

uint16_t parsePort(const std::string &text)
{
    std::size_t used = 0;
    const unsigned long value = std::stoul(text, &used, 10);
    if (used != text.size() || value == 0 || value > 65535)
        throw std::runtime_error("flex1500: port must be 1..65535");
    return static_cast<uint16_t>(value);
}

Socket connectTcp(const std::string &host, uint16_t port)
{
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *addresses = nullptr;
    const std::string portText = std::to_string(port);
    const int lookup = getaddrinfo(host.c_str(), portText.c_str(), &hints,
                                   &addresses);
    if (lookup != 0)
        throw std::runtime_error("flex1500: cannot resolve daemon host: " +
                                 std::string(gai_strerror(lookup)));
    Socket connected;
    for (addrinfo *address = addresses; address != nullptr;
         address = address->ai_next) {
        Socket candidate(::socket(address->ai_family, address->ai_socktype,
                                  address->ai_protocol));
        if (candidate.valid() &&
            ::connect(candidate.get(), address->ai_addr,
                      address->ai_addrlen) == 0) {
            connected = std::move(candidate);
            break;
        }
    }
    freeaddrinfo(addresses);
    if (!connected.valid())
        throw std::runtime_error("flex1500: cannot connect to daemon at " +
                                 host + ":" + portText);
    return connected;
}

void sendAll(int fd, const std::string &data)
{
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t count = ::send(fd, data.data() + offset,
                                     data.size() - offset, MSG_NOSIGNAL);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("flex1500: HTTP send failed");
        offset += static_cast<std::size_t>(count);
    }
}

std::string receiveUntil(int fd, const std::string &delimiter,
                         std::size_t limit)
{
    std::string data;
    char chunk[1024];
    while (data.find(delimiter) == std::string::npos) {
        const ssize_t count = ::recv(fd, chunk, sizeof(chunk), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("flex1500: truncated HTTP response");
        data.append(chunk, static_cast<std::size_t>(count));
        if (data.size() > limit)
            throw std::runtime_error("flex1500: HTTP response header too large");
    }
    return data;
}

HttpResponse request(const std::string &host, uint16_t port,
                     const std::string &method, const std::string &path,
                     bool keepSocket = false,
                     const std::string &requestBody = {},
                     const std::vector<std::string> &headers = {})
{
    Socket socket = connectTcp(host, port);
    std::string wire = method + " " + path + " HTTP/1.1\r\nHost: " + host +
        "\r\nConnection: " + (keepSocket ? "keep-alive" : "close") + "\r\n";
    for (const auto &header : headers) wire += header + "\r\n";
    if (!requestBody.empty())
        wire += "Content-Length: " + std::to_string(requestBody.size()) +
                "\r\n";
    wire += "\r\n" + requestBody;
    sendAll(socket.get(), wire);
    std::string received = receiveUntil(socket.get(), "\r\n\r\n", 16384);
    const std::size_t split = received.find("\r\n\r\n");
    std::string header = received.substr(0, split + 4);
    std::string body = received.substr(split + 4);
    const std::size_t firstSpace = header.find(' ');
    if (header.rfind("HTTP/1.", 0) != 0 || firstSpace == std::string::npos)
        throw std::runtime_error("flex1500: invalid HTTP response");
    const int status = std::stoi(header.substr(firstSpace + 1, 3));
    if (keepSocket) return {status, std::move(body), std::move(socket)};

    std::size_t contentLength = 0;
    const std::string key = "Content-Length:";
    const std::size_t lengthAt = header.find(key);
    if (lengthAt != std::string::npos)
        contentLength = std::stoul(header.substr(lengthAt + key.size()));
    while (body.size() < contentLength) {
        char chunk[1024];
        const ssize_t count = ::recv(socket.get(), chunk, sizeof(chunk), 0);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        body.append(chunk, static_cast<std::size_t>(count));
    }
    return {status, std::move(body), Socket()};
}

double jsonNumber(const std::string &json, const std::string &name,
                  double fallback)
{
    const std::string key = "\"" + name + "\"";
    std::size_t at = json.find(key);
    if (at == std::string::npos) return fallback;
    at = json.find(':', at + key.size());
    if (at == std::string::npos) return fallback;
    const char *start = json.c_str() + at + 1;
    char *end = nullptr;
    const double value = std::strtod(start, &end);
    return end == start ? fallback : value;
}

bool jsonBool(const std::string &json, const std::string &name, bool fallback)
{
    const std::string key = "\"" + name + "\"";
    std::size_t at = json.find(key);
    if (at == std::string::npos) return fallback;
    at = json.find(':', at + key.size());
    if (at == std::string::npos) return fallback;
    at = json.find_first_not_of(" \t\r\n", at + 1);
    if (json.compare(at, 4, "true") == 0) return true;
    if (json.compare(at, 5, "false") == 0) return false;
    return fallback;
}

uint32_t be32(const uint8_t *bytes)
{
    return (uint32_t(bytes[0]) << 24) | (uint32_t(bytes[1]) << 16) |
           (uint32_t(bytes[2]) << 8) | uint32_t(bytes[3]);
}

float f32le(const uint8_t *bytes)
{
    uint32_t bits = uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) |
                    (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

struct RxStream {
    explicit RxStream(std::string requestedFormat)
        : format(std::move(requestedFormat)) {}

    std::string format;
    Socket socket;
    bool active = false;
    std::vector<uint8_t> input;
    std::vector<std::complex<float>> samples;
    std::size_t sampleOffset = 0;
    uint32_t expectedSequence = 0;
    bool haveSequence = false;
};

struct TxStream {
    explicit TxStream(std::string requestedFormat)
        : format(std::move(requestedFormat)) {}

    std::string format;
    Socket socket;
    uint64_t lease = 0;
    std::size_t submittedFrames = 0;
    bool active = false;
    bool keyed = false;
    std::chrono::steady_clock::time_point lastKeepalive{};
};

class Flex1500Device final : public SoapySDR::Device {
public:
    explicit Flex1500Device(const SoapySDR::Kwargs &args)
        : host_(argument(args, "host", "127.0.0.1")),
          port_(parsePort(argument(args, "port", "15000")))
    {
        const HttpResponse radio = request(host_, port_, "GET", "/v1/radio");
        if (radio.status != 200)
            throw std::runtime_error("flex1500: daemon radio endpoint unavailable");
        daemonTransmitEnabled_ = jsonBool(
            radio.body, "transmit_enabled", false);
        tuningEnabled_ = jsonBool(radio.body, "rx_tuning_enabled", false);
        frequency_ = jsonNumber(radio.body, "frequency_hz", 0.0);
        gain_ = jsonNumber(radio.body, "rx_gain_db", 20.0);
        txDrive_ = jsonNumber(radio.body, "tx_drive_percent", 100.0);
        bandwidth_ = jsonNumber(radio.body, "rx_bandwidth_hz", 6000.0);
        squelch_ = jsonNumber(radio.body, "rx_squelch_db", -120.0);
    }
    ~Flex1500Device() override
    {
        if (txStream_ != nullptr) stopTx(*txStream_);
        delete rxStream_;
        delete txStream_;
    }

    std::string getDriverKey() const override { return "flex1500"; }
    std::string getHardwareKey() const override { return "FLEX-1500 via flex1500d"; }
    SoapySDR::Kwargs getHardwareInfo() const override
    {
        return {{"daemon", host_ + ":" + std::to_string(port_)},
                {"receive_only", daemonTransmitEnabled_ ? "false" : "true"},
                {"daemon_transmit_enabled",
                 daemonTransmitEnabled_ ? "true" : "false"},
                {"transport", "flex1500d API v1"}};
    }
    size_t getNumChannels(int direction) const override
    {
        return direction == SOAPY_SDR_RX ||
            (direction == SOAPY_SDR_TX && daemonTransmitEnabled_) ? 1 : 0;
    }
    bool getFullDuplex(int, size_t) const override { return false; }
    std::vector<std::string> getStreamFormats(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return {SOAPY_SDR_CF32, SOAPY_SDR_CS16};
    }
    std::string getNativeStreamFormat(int direction, size_t channel,
                                      double &fullScale) const override
    {
        checkChannel(direction, channel);
        fullScale = 32768.0;
        return direction == SOAPY_SDR_RX ? SOAPY_SDR_CF32 : SOAPY_SDR_CS16;
    }
    SoapySDR::Stream *setupStream(int direction, const std::string &format,
                                  const std::vector<size_t> &channels,
                                  const SoapySDR::Kwargs &) override
    {
        checkChannel(direction, 0);
        if (!channels.empty() && (channels.size() != 1 || channels[0] != 0))
            throw std::runtime_error("flex1500: only channel 0 is supported");
        if (format != SOAPY_SDR_CF32 && format != SOAPY_SDR_CS16)
            throw std::runtime_error("flex1500: stream format must be CF32 or CS16");
        if (direction == SOAPY_SDR_RX) {
            if (rxStream_ != nullptr)
                throw std::runtime_error("flex1500: only one RX stream is supported");
            rxStream_ = new RxStream{format};
            return reinterpret_cast<SoapySDR::Stream *>(rxStream_);
        }
        if (txStream_ != nullptr)
            throw std::runtime_error("flex1500: only one TX stream is supported");
        txStream_ = new TxStream{format};
        return reinterpret_cast<SoapySDR::Stream *>(txStream_);
    }
    void closeStream(SoapySDR::Stream *handle) override
    {
        if (handle == reinterpret_cast<SoapySDR::Stream *>(rxStream_)) {
            delete rxStream_;
            rxStream_ = nullptr;
            return;
        }
        if (handle == reinterpret_cast<SoapySDR::Stream *>(txStream_)) {
            stopTx(*txStream_);
            delete txStream_;
            txStream_ = nullptr;
            return;
        }
        throw std::runtime_error("flex1500: invalid stream handle");
    }
    size_t getStreamMTU(SoapySDR::Stream *handle) const override
    {
        checkedDirection(handle);
        return STREAM_MTU;
    }
    int activateStream(SoapySDR::Stream *handle, int flags, long long,
                       size_t numElems) override
    {
        if (flags != 0 || numElems != 0) return SOAPY_SDR_NOT_SUPPORTED;
        if (handle == reinterpret_cast<SoapySDR::Stream *>(txStream_))
            return activateTx(*txStream_);
        RxStream *stream = checkedRx(handle);
        HttpResponse response = request(host_, port_, "GET", "/v1/stream/iq", true);
        if (response.status != 200) return SOAPY_SDR_STREAM_ERROR;
        stream->socket = std::move(response.socket);
        stream->input.assign(response.body.begin(), response.body.end());
        stream->samples.clear();
        stream->sampleOffset = 0;
        stream->haveSequence = false;
        stream->active = true;
        return 0;
    }
    int deactivateStream(SoapySDR::Stream *handle, int flags, long long) override
    {
        if (flags != 0) return SOAPY_SDR_NOT_SUPPORTED;
        if (handle == reinterpret_cast<SoapySDR::Stream *>(txStream_)) {
            stopTx(*txStream_);
            return 0;
        }
        RxStream *stream = checkedRx(handle);
        stream->socket.close();
        stream->active = false;
        return 0;
    }
    int readStream(SoapySDR::Stream *handle, void *const *buffers,
                   size_t numElems, int &flags, long long &timeNs,
                   long timeoutUs) override
    {
        RxStream *stream = checkedRx(handle);
        flags = 0;
        timeNs = 0;
        if (!stream->active || buffers == nullptr || buffers[0] == nullptr)
            return SOAPY_SDR_TIMEOUT;
        if (stream->sampleOffset == stream->samples.size()) {
            const int loaded = loadFrame(*stream, timeoutUs);
            if (loaded < 0) return loaded;
        }
        const size_t count = std::min(numElems,
            stream->samples.size() - stream->sampleOffset);
        if (stream->format == SOAPY_SDR_CF32) {
            auto *output = static_cast<std::complex<float> *>(buffers[0]);
            for (size_t index = 0; index < count; ++index) {
                const auto sample = stream->samples[stream->sampleOffset + index];
                output[index] = {sample.real(), -sample.imag()};
            }
        } else {
            auto *output = static_cast<int16_t *>(buffers[0]);
            for (size_t index = 0; index < count; ++index) {
                const auto sample = stream->samples[stream->sampleOffset + index];
                output[index * 2] = toS16(sample.real());
                output[index * 2 + 1] = toS16(-sample.imag());
            }
        }
        stream->sampleOffset += count;
        if (stream->sampleOffset < stream->samples.size())
            flags |= SOAPY_SDR_MORE_FRAGMENTS;
        return static_cast<int>(count);
    }
    int writeStream(SoapySDR::Stream *handle, const void *const *buffers,
                    size_t numElems, int &flags, long long,
                    long timeoutUs) override
    {
        TxStream *stream = checkedTx(handle);
        if (!stream->active || buffers == nullptr || buffers[0] == nullptr)
            return SOAPY_SDR_STREAM_ERROR;
        if (flags != 0)
            return SOAPY_SDR_NOT_SUPPORTED;
        if (numElems == 0) return 0;
        if (!keepTxAlive(*stream)) return SOAPY_SDR_STREAM_ERROR;
        std::vector<int16_t> wire(numElems * 2);
        if (stream->format == SOAPY_SDR_CF32) {
            const auto *input = static_cast<const std::complex<float> *>(buffers[0]);
            for (size_t index = 0; index < numElems; ++index) {
                wire[index * 2] = normalizedS16(input[index].real());
                wire[index * 2 + 1] = normalizedS16(-input[index].imag());
            }
        } else {
            const auto *input = static_cast<const int16_t *>(buffers[0]);
            for (size_t index = 0; index < numElems; ++index) {
                wire[index * 2] = input[index * 2];
                wire[index * 2 + 1] = input[index * 2 + 1] == INT16_MIN
                    ? INT16_MAX : static_cast<int16_t>(-input[index * 2 + 1]);
            }
        }
        const int sent = sendTxBytes(stream->socket.get(), wire.data(),
                                     wire.size() * sizeof(int16_t), timeoutUs);
        if (sent < 0) return sent;
        stream->submittedFrames += numElems;
        if (!stream->keyed && stream->submittedFrames > 24000) {
            const HttpResponse keyed = leaseRequest(
                "PUT", "/v1/tx/ptt/start", stream->lease);
            if (keyed.status == 200) stream->keyed = true;
            else if (keyed.status != 409) return SOAPY_SDR_STREAM_ERROR;
        }
        return static_cast<int>(numElems);
    }
    void setFrequency(int direction, size_t channel, double frequency,
                      const SoapySDR::Kwargs &) override
    {
        checkChannel(direction, channel);
        if (!tuningEnabled_)
            throw std::runtime_error("flex1500: daemon RX tuning is disabled");
        if (!std::isfinite(frequency) || frequency < MIN_FREQUENCY ||
            frequency > MAX_FREQUENCY)
            throw std::runtime_error("flex1500: frequency outside 100 kHz..54 MHz");
        if (direction == SOAPY_SDR_TX && !txFrequencyAllowed(frequency))
            throw std::runtime_error(
                "flex1500: TX center plus/minus 24 kHz must fit an allowed band");
        const uint32_t rounded = static_cast<uint32_t>(std::llround(frequency));
        const HttpResponse response = request(host_, port_, "PUT",
            "/v1/radio/frequency/" + std::to_string(rounded));
        if (response.status != 200)
            throw std::runtime_error("flex1500: daemon rejected RX frequency");
        frequency_ = rounded;
        if (direction == SOAPY_SDR_RX && rxStream_ != nullptr &&
            rxStream_->active) {
            rxStream_->socket.close();
            HttpResponse replacement = request(host_, port_, "GET",
                                                "/v1/stream/iq", true);
            if (replacement.status != 200) {
                rxStream_->active = false;
                throw std::runtime_error(
                    "flex1500: tuned, but could not reconnect RX stream");
            }
            rxStream_->socket = std::move(replacement.socket);
            rxStream_->input.assign(replacement.body.begin(),
                                  replacement.body.end());
            rxStream_->samples.clear();
            rxStream_->sampleOffset = 0;
            rxStream_->haveSequence = false;
        }
    }
    double getFrequency(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return frequency_;
    }
    std::vector<std::string> listFrequencies(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return {"RF"};
    }
    SoapySDR::RangeList getFrequencyRange(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return direction == SOAPY_SDR_RX
            ? SoapySDR::RangeList{SoapySDR::Range(
                  MIN_FREQUENCY, MAX_FREQUENCY, 1.0)}
            : TX_FREQUENCY_RANGES;
    }
    std::vector<std::string> listGains(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return {direction == SOAPY_SDR_RX ? "RX" : "TX"};
    }
    void setGain(int direction, size_t channel, double gain) override
    {
        checkChannel(direction, channel);
        if (direction == SOAPY_SDR_TX) {
            if (!std::isfinite(gain) || gain < 1.0 || gain > 100.0)
                throw std::runtime_error("flex1500: TX drive must be 1..100 percent");
            const int value = static_cast<int>(std::lround(gain));
            const HttpResponse response = request(host_, port_, "PUT",
                "/v1/radio/tx-drive/" + std::to_string(value));
            if (response.status != 200)
                throw std::runtime_error("flex1500: daemon rejected TX drive");
            txDrive_ = value;
            return;
        }
        const double rounded = std::round(gain / 10.0) * 10.0;
        if (!std::isfinite(gain) || std::abs(gain - rounded) > 0.001 ||
            rounded < -10.0 || rounded > 30.0)
            throw std::runtime_error("flex1500: RX gain must be -10, 0, 10, 20, or 30 dB");
        const int value = static_cast<int>(rounded);
        const HttpResponse response = request(host_, port_, "PUT",
            "/v1/radio/gain/" + std::to_string(value));
        if (response.status != 200)
            throw std::runtime_error("flex1500: daemon rejected RX gain");
        gain_ = rounded;
    }
    double getGain(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return direction == SOAPY_SDR_RX ? gain_ : txDrive_;
    }
    SoapySDR::Range getGainRange(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return direction == SOAPY_SDR_RX ? SoapySDR::Range(-10.0, 30.0, 10.0)
                                         : SoapySDR::Range(1.0, 100.0, 1.0);
    }
    void setBandwidth(int direction, size_t channel, double bandwidth) override
    {
        checkChannel(direction, channel);
        if (direction == SOAPY_SDR_TX) {
            if (std::abs(bandwidth - SAMPLE_RATE) > 0.5)
                throw std::runtime_error("flex1500: TX I/Q bandwidth is fixed at 48000 Hz");
            return;
        }
        if (!std::isfinite(bandwidth) || bandwidth < 100.0 || bandwidth > 20000.0)
            throw std::runtime_error("flex1500: bandwidth outside 100..20000 Hz");
        const uint32_t rounded = static_cast<uint32_t>(std::llround(bandwidth));
        const HttpResponse response = request(host_, port_, "PUT",
            "/v1/radio/bandwidth/" + std::to_string(rounded));
        if (response.status != 200)
            throw std::runtime_error("flex1500: daemon rejected RX bandwidth");
        bandwidth_ = rounded;
    }
    double getBandwidth(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return direction == SOAPY_SDR_RX ? bandwidth_ : SAMPLE_RATE;
    }
    SoapySDR::RangeList getBandwidthRange(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return direction == SOAPY_SDR_RX
            ? SoapySDR::RangeList{SoapySDR::Range(100.0, 20000.0, 1.0)}
            : SoapySDR::RangeList{SoapySDR::Range(SAMPLE_RATE, SAMPLE_RATE)};
    }
    SoapySDR::ArgInfoList getSettingInfo(void) const override
    {
        SoapySDR::ArgInfo squelch;
        squelch.key = "squelch_db";
        squelch.name = "RX squelch";
        squelch.description = "Host-side receive squelch threshold; -120 is open";
        squelch.units = "dBFS";
        squelch.type = SoapySDR::ArgInfo::FLOAT;
        squelch.range = SoapySDR::Range(-120.0, 0.0, 1.0);
        squelch.value = std::to_string(static_cast<int>(squelch_));
        return {squelch};
    }
    void writeSetting(const std::string &key, const std::string &value) override
    {
        if (key != "squelch_db")
            throw std::runtime_error("flex1500: unknown setting " + key);
        std::size_t used = 0;
        const int threshold = std::stoi(value, &used);
        if (used != value.size() || threshold < -120 || threshold > 0)
            throw std::runtime_error("flex1500: squelch must be -120..0 dBFS");
        const HttpResponse response = request(host_, port_, "PUT",
            "/v1/radio/squelch/" + std::to_string(threshold));
        if (response.status != 200)
            throw std::runtime_error("flex1500: daemon rejected RX squelch");
        squelch_ = threshold;
    }
    std::string readSetting(const std::string &key) const override
    {
        if (key != "squelch_db")
            throw std::runtime_error("flex1500: unknown setting " + key);
        return std::to_string(static_cast<int>(squelch_));
    }
    void setSampleRate(int direction, size_t channel, double rate) override
    {
        checkChannel(direction, channel);
        if (std::abs(rate - SAMPLE_RATE) > 0.5)
            throw std::runtime_error("flex1500: sample rate is fixed at 48000");
    }
    double getSampleRate(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return SAMPLE_RATE;
    }
    std::vector<double> listSampleRates(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return {SAMPLE_RATE};
    }
    SoapySDR::RangeList getSampleRateRange(int direction, size_t channel) const override
    {
        checkChannel(direction, channel);
        return {SoapySDR::Range(SAMPLE_RATE, SAMPLE_RATE)};
    }

private:
    void checkChannel(int direction, size_t channel) const
    {
        if (channel != 0 || (direction != SOAPY_SDR_RX &&
            direction != SOAPY_SDR_TX) ||
            (direction == SOAPY_SDR_TX && !daemonTransmitEnabled_))
            throw std::runtime_error("flex1500: requested channel is unavailable");
    }
    int checkedDirection(SoapySDR::Stream *handle) const
    {
        if (handle == reinterpret_cast<SoapySDR::Stream *>(rxStream_))
            return SOAPY_SDR_RX;
        if (handle == reinterpret_cast<SoapySDR::Stream *>(txStream_))
            return SOAPY_SDR_TX;
        throw std::runtime_error("flex1500: invalid stream handle");
    }
    RxStream *checkedRx(SoapySDR::Stream *handle) const
    {
        auto *stream = reinterpret_cast<RxStream *>(handle);
        if (stream == nullptr || stream != rxStream_)
            throw std::runtime_error("flex1500: invalid stream handle");
        return stream;
    }
    TxStream *checkedTx(SoapySDR::Stream *handle) const
    {
        auto *stream = reinterpret_cast<TxStream *>(handle);
        if (stream == nullptr || stream != txStream_)
            throw std::runtime_error("flex1500: invalid TX stream handle");
        return stream;
    }
    HttpResponse leaseRequest(const std::string &method,
                              const std::string &path, uint64_t lease,
                              bool keepSocket = false)
    {
        return request(host_, port_, method, path, keepSocket, {},
            {"X-Flex1500-TX-Lease: " + std::to_string(lease)});
    }
    int activateTx(TxStream &stream)
    {
        if (stream.active) return 0;
        std::ostringstream profile;
        profile << "{\"mode\":\"iq\",\"source\":\"iq\","
                << "\"drive_percent\":" << static_cast<int>(txDrive_)
                << ",\"sample_rate\":48000,"
                << "\"sample_format\":\"cs16le\",\"channels\":1}";
        const HttpResponse acquired = request(
            host_, port_, "POST", "/v1/tx/sessions", false, profile.str(),
            {"Content-Type: application/json"});
        if (acquired.status != 201) return SOAPY_SDR_STREAM_ERROR;
        const double leaseNumber = jsonNumber(acquired.body, "lease", 0.0);
        if (!std::isfinite(leaseNumber) || leaseNumber < 1.0)
            return SOAPY_SDR_CORRUPTION;
        stream.lease = static_cast<uint64_t>(leaseNumber);
        HttpResponse connected = leaseRequest(
            "CONNECT", "/v1/tx/stream", stream.lease, true);
        if (connected.status != 200) {
            try {
                (void)leaseRequest("DELETE", "/v1/tx/sessions/current",
                                   stream.lease);
            } catch (...) {}
            stream.lease = 0;
            return SOAPY_SDR_STREAM_ERROR;
        }
        stream.socket = std::move(connected.socket);
        stream.submittedFrames = 0;
        stream.keyed = false;
        stream.active = true;
        stream.lastKeepalive = std::chrono::steady_clock::now();
        return 0;
    }
    void stopTx(TxStream &stream) noexcept
    {
        if (stream.lease != 0) {
            try {
                if (stream.keyed)
                    (void)leaseRequest("PUT", "/v1/tx/ptt/stop",
                                       stream.lease);
                (void)leaseRequest("DELETE", "/v1/tx/sessions/current",
                                   stream.lease);
            } catch (...) {}
        }
        stream.socket.close();
        stream.lease = 0;
        stream.submittedFrames = 0;
        stream.keyed = false;
        stream.active = false;
    }
    bool keepTxAlive(TxStream &stream)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now - stream.lastKeepalive < std::chrono::seconds(5)) return true;
        const HttpResponse response = leaseRequest(
            "PUT", "/v1/tx/sessions/keepalive", stream.lease);
        if (response.status != 200) return false;
        stream.lastKeepalive = now;
        return true;
    }
    static int sendTxBytes(int fd, const void *data, size_t length,
                           long timeoutUs)
    {
        const auto *bytes = static_cast<const uint8_t *>(data);
        const auto start = std::chrono::steady_clock::now();
        size_t sent = 0;
        while (sent < length) {
            const ssize_t count = ::send(fd, bytes + sent, length - sent,
                                         MSG_NOSIGNAL | MSG_DONTWAIT);
            if (count > 0) {
                sent += static_cast<size_t>(count);
                continue;
            }
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                return SOAPY_SDR_STREAM_ERROR;
            long remaining = timeoutUs;
            if (timeoutUs >= 0) {
                const auto elapsed = std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                remaining -= static_cast<long>(elapsed);
                if (remaining <= 0) return SOAPY_SDR_TIMEOUT;
            }
            pollfd writable{fd, POLLOUT, 0};
            const int milliseconds = remaining < 0 ? -1 :
                static_cast<int>((remaining + 999) / 1000);
            int ready;
            do ready = ::poll(&writable, 1, milliseconds);
            while (ready < 0 && errno == EINTR);
            if (ready == 0) return SOAPY_SDR_TIMEOUT;
            if (ready < 0 || (writable.revents &
                (POLLERR | POLLHUP | POLLNVAL))) return SOAPY_SDR_STREAM_ERROR;
        }
        return 0;
    }
    static int16_t normalizedS16(float value)
    {
        if (!std::isfinite(value)) return 0;
        const long rounded = std::lround(value * 32767.0f);
        return static_cast<int16_t>(std::max<long>(-32768,
            std::min<long>(32767, rounded)));
    }
    static int16_t toS16(float value)
    {
        const long rounded = std::lround(value);
        return static_cast<int16_t>(std::max<long>(-32768,
            std::min<long>(32767, rounded)));
    }
    static int waitReadable(int fd, long timeoutUs)
    {
        const int milliseconds = timeoutUs < 0 ? -1 :
            static_cast<int>((timeoutUs + 999) / 1000);
        pollfd descriptor{fd, POLLIN, 0};
        int result;
        do result = ::poll(&descriptor, 1, milliseconds);
        while (result < 0 && errno == EINTR);
        return result;
    }
    static int receiveBytes(RxStream &stream, size_t required, long timeoutUs)
    {
        const auto start = std::chrono::steady_clock::now();
        while (stream.input.size() < required) {
            long remaining = timeoutUs;
            if (timeoutUs >= 0) {
                const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - start).count();
                remaining = timeoutUs - static_cast<long>(elapsed);
                if (remaining <= 0) return SOAPY_SDR_TIMEOUT;
            }
            if (waitReadable(stream.socket.get(), remaining) == 0)
                return SOAPY_SDR_TIMEOUT;
            uint8_t chunk[8192];
            const ssize_t count = ::recv(stream.socket.get(), chunk, sizeof(chunk), 0);
            if (count < 0 && errno == EINTR) continue;
            if (count <= 0) return SOAPY_SDR_STREAM_ERROR;
            stream.input.insert(stream.input.end(), chunk, chunk + count);
        }
        return 0;
    }
    static int loadFrame(RxStream &stream, long timeoutUs)
    {
        int result = receiveBytes(stream, FRAME_HEADER_SIZE, timeoutUs);
        if (result < 0) return result;
        const uint8_t *header = stream.input.data();
        const uint32_t sequence = be32(header + 8);
        const uint32_t rate = be32(header + 12);
        const uint32_t count = be32(header + 16);
        if (std::memcmp(header, "F15I", 4) != 0 || header[4] != 1 ||
            header[5] != 1 || header[6] != 0 || header[7] != 20 ||
            rate != 48000 || count == 0 || count > 1048576)
            return SOAPY_SDR_CORRUPTION;
        if (stream.haveSequence && sequence != stream.expectedSequence)
            return SOAPY_SDR_OVERFLOW;
        const size_t frameSize = FRAME_HEADER_SIZE + size_t(count) * 8;
        result = receiveBytes(stream, frameSize, timeoutUs);
        if (result < 0) return result;
        stream.samples.resize(count);
        for (uint32_t index = 0; index < count; ++index) {
            const uint8_t *sample = stream.input.data() + FRAME_HEADER_SIZE + index * 8;
            stream.samples[index] = {f32le(sample), f32le(sample + 4)};
        }
        stream.input.erase(stream.input.begin(), stream.input.begin() + frameSize);
        stream.sampleOffset = 0;
        stream.expectedSequence = sequence + 1;
        stream.haveSequence = true;
        return 0;
    }

    std::string host_;
    uint16_t port_;
    bool tuningEnabled_ = false;
    bool daemonTransmitEnabled_ = false;
    double frequency_ = 0.0;
    double gain_ = 20.0;
    double txDrive_ = 100.0;
    double bandwidth_ = 6000.0;
    double squelch_ = -120.0;
    RxStream *rxStream_ = nullptr;
    TxStream *txStream_ = nullptr;
};

SoapySDR::KwargsList findFlex1500(const SoapySDR::Kwargs &args)
{
    if (args.count("driver") != 0 && args.at("driver") != "flex1500") return {};
    SoapySDR::Kwargs result{{"driver", "flex1500"},
                            {"label", "FLEX-1500 via flex1500d"},
                            {"host", argument(args, "host", "127.0.0.1")},
                            {"port", argument(args, "port", "15000")}};
    try {
        Flex1500Device probe(result);
        return {result};
    } catch (...) {
        return {};
    }
}

SoapySDR::Device *makeFlex1500(const SoapySDR::Kwargs &args)
{
    return new Flex1500Device(args);
}

static SoapySDR::Registry registerFlex1500("flex1500", findFlex1500,
                                           makeFlex1500,
                                           SOAPY_SDR_ABI_VERSION);

} // namespace
