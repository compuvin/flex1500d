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
#include <vector>

namespace {

constexpr double SAMPLE_RATE = 48000.0;
constexpr double MIN_FREQUENCY = 100000.0;
constexpr double MAX_FREQUENCY = 54000000.0;
constexpr std::size_t FRAME_HEADER_SIZE = 20;
constexpr std::size_t STREAM_MTU = 1024;

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
                     bool keepSocket = false)
{
    Socket socket = connectTcp(host, port);
    sendAll(socket.get(), method + " " + path + " HTTP/1.1\r\nHost: " + host +
                              "\r\nConnection: close\r\n\r\n");
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

class Flex1500Device final : public SoapySDR::Device {
public:
    explicit Flex1500Device(const SoapySDR::Kwargs &args)
        : host_(argument(args, "host", "127.0.0.1")),
          port_(parsePort(argument(args, "port", "15000")))
    {
        const HttpResponse radio = request(host_, port_, "GET", "/v1/radio");
        if (radio.status != 200)
            throw std::runtime_error("flex1500: daemon radio endpoint unavailable");
        if (!jsonBool(radio.body, "receive_only", false) ||
            jsonBool(radio.body, "transmit_enabled", true))
            throw std::runtime_error("flex1500: daemon safety contract mismatch");
        tuningEnabled_ = jsonBool(radio.body, "rx_tuning_enabled", false);
        frequency_ = jsonNumber(radio.body, "frequency_hz", 0.0);
    }
    ~Flex1500Device() override { delete stream_; }

    std::string getDriverKey() const override { return "flex1500"; }
    std::string getHardwareKey() const override { return "FLEX-1500 via flex1500d"; }
    SoapySDR::Kwargs getHardwareInfo() const override
    {
        return {{"daemon", host_ + ":" + std::to_string(port_)},
                {"receive_only", "true"}, {"transport", "flex1500d API v1"}};
    }
    size_t getNumChannels(int direction) const override
    {
        return direction == SOAPY_SDR_RX ? 1 : 0;
    }
    bool getFullDuplex(int, size_t) const override { return false; }
    std::vector<std::string> getStreamFormats(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return {SOAPY_SDR_CF32, SOAPY_SDR_CS16};
    }
    std::string getNativeStreamFormat(int direction, size_t channel,
                                      double &fullScale) const override
    {
        checkRx(direction, channel);
        fullScale = 32768.0;
        return SOAPY_SDR_CF32;
    }
    SoapySDR::Stream *setupStream(int direction, const std::string &format,
                                  const std::vector<size_t> &channels,
                                  const SoapySDR::Kwargs &) override
    {
        if (direction != SOAPY_SDR_RX ||
            (!channels.empty() && (channels.size() != 1 || channels[0] != 0)))
            throw std::runtime_error("flex1500: only RX channel 0 is supported");
        if (format != SOAPY_SDR_CF32 && format != SOAPY_SDR_CS16)
            throw std::runtime_error("flex1500: stream format must be CF32 or CS16");
        if (stream_ != nullptr)
            throw std::runtime_error("flex1500: only one RX stream is supported");
        stream_ = new RxStream{format};
        return reinterpret_cast<SoapySDR::Stream *>(stream_);
    }
    void closeStream(SoapySDR::Stream *handle) override
    {
        RxStream *stream = checked(handle);
        delete stream;
        stream_ = nullptr;
    }
    size_t getStreamMTU(SoapySDR::Stream *handle) const override
    {
        checked(handle);
        return STREAM_MTU;
    }
    int activateStream(SoapySDR::Stream *handle, int flags, long long,
                       size_t numElems) override
    {
        RxStream *stream = checked(handle);
        if (flags != 0 || numElems != 0) return SOAPY_SDR_NOT_SUPPORTED;
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
        RxStream *stream = checked(handle);
        if (flags != 0) return SOAPY_SDR_NOT_SUPPORTED;
        stream->socket.close();
        stream->active = false;
        return 0;
    }
    int readStream(SoapySDR::Stream *handle, void *const *buffers,
                   size_t numElems, int &flags, long long &timeNs,
                   long timeoutUs) override
    {
        RxStream *stream = checked(handle);
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
            std::copy_n(stream->samples.data() + stream->sampleOffset, count, output);
        } else {
            auto *output = static_cast<int16_t *>(buffers[0]);
            for (size_t index = 0; index < count; ++index) {
                const auto sample = stream->samples[stream->sampleOffset + index];
                output[index * 2] = toS16(sample.real());
                output[index * 2 + 1] = toS16(sample.imag());
            }
        }
        stream->sampleOffset += count;
        if (stream->sampleOffset < stream->samples.size())
            flags |= SOAPY_SDR_MORE_FRAGMENTS;
        return static_cast<int>(count);
    }
    void setFrequency(int direction, size_t channel, double frequency,
                      const SoapySDR::Kwargs &) override
    {
        checkRx(direction, channel);
        if (!tuningEnabled_)
            throw std::runtime_error("flex1500: daemon RX tuning is disabled");
        if (!std::isfinite(frequency) || frequency < MIN_FREQUENCY ||
            frequency > MAX_FREQUENCY)
            throw std::runtime_error("flex1500: frequency outside 100 kHz..54 MHz");
        const uint32_t rounded = static_cast<uint32_t>(std::llround(frequency));
        const HttpResponse response = request(host_, port_, "PUT",
            "/v1/radio/frequency/" + std::to_string(rounded));
        if (response.status != 200)
            throw std::runtime_error("flex1500: daemon rejected RX frequency");
        frequency_ = rounded;
        if (stream_ != nullptr && stream_->active) {
            stream_->socket.close();
            HttpResponse replacement = request(host_, port_, "GET",
                                                "/v1/stream/iq", true);
            if (replacement.status != 200) {
                stream_->active = false;
                throw std::runtime_error(
                    "flex1500: tuned, but could not reconnect RX stream");
            }
            stream_->socket = std::move(replacement.socket);
            stream_->input.assign(replacement.body.begin(),
                                  replacement.body.end());
            stream_->samples.clear();
            stream_->sampleOffset = 0;
            stream_->haveSequence = false;
        }
    }
    double getFrequency(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return frequency_;
    }
    std::vector<std::string> listFrequencies(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return {"RF"};
    }
    SoapySDR::RangeList getFrequencyRange(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return {SoapySDR::Range(MIN_FREQUENCY, MAX_FREQUENCY, 1.0)};
    }
    void setSampleRate(int direction, size_t channel, double rate) override
    {
        checkRx(direction, channel);
        if (std::abs(rate - SAMPLE_RATE) > 0.5)
            throw std::runtime_error("flex1500: sample rate is fixed at 48000");
    }
    double getSampleRate(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return SAMPLE_RATE;
    }
    std::vector<double> listSampleRates(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return {SAMPLE_RATE};
    }
    SoapySDR::RangeList getSampleRateRange(int direction, size_t channel) const override
    {
        checkRx(direction, channel);
        return {SoapySDR::Range(SAMPLE_RATE, SAMPLE_RATE)};
    }
private:
    void checkRx(int direction, size_t channel) const
    {
        if (direction != SOAPY_SDR_RX || channel != 0)
            throw std::runtime_error("flex1500: only RX channel 0 is supported");
    }
    RxStream *checked(SoapySDR::Stream *handle) const
    {
        auto *stream = reinterpret_cast<RxStream *>(handle);
        if (stream == nullptr || stream != stream_)
            throw std::runtime_error("flex1500: invalid stream handle");
        return stream;
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
    double frequency_ = 0.0;
    RxStream *stream_ = nullptr;
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
