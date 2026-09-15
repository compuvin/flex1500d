// SPDX-License-Identifier: GPL-3.0-only

#include "test_assert.h"

#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>

#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    CHECK(argc == 2 || argc == 3);
    const std::string port = argv[1];
    const SoapySDR::Kwargs args{{"driver", "flex1500"},
                                {"host", "127.0.0.1"}, {"port", port}};
    if (argc == 3) {
        SoapySDR::Device *listener = SoapySDR::Device::make(args);
        CHECK(listener != nullptr);
        CHECK(listener->getNumChannels(SOAPY_SDR_RX) == 1);
        CHECK(listener->getNumChannels(SOAPY_SDR_TX) == 0);
        CHECK(listener->getHardwareInfo().at("station_owner") == "false");
        listener->setFrequency(SOAPY_SDR_RX, 0, 7001000.0);
        CHECK(listener->getFrequency(SOAPY_SDR_RX, 0) == 7001000.0);
        bool rejected = false;
        try {
            listener->setFrequency(SOAPY_SDR_RX, 0, 7030000.0);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        CHECK(rejected);
        SoapySDR::Device::unmake(listener);
        return EXIT_SUCCESS;
    }

    const auto devices = SoapySDR::Device::enumerate(args);
    CHECK(devices.size() == 1);

    SoapySDR::Device *device = SoapySDR::Device::make(args);
    CHECK(device != nullptr);
    CHECK(device->getDriverKey() == "flex1500");
    CHECK(device->getNumChannels(SOAPY_SDR_RX) == 1);
    CHECK(device->getNumChannels(SOAPY_SDR_TX) == 1);
    CHECK(device->getHardwareInfo().at("daemon_transmit_enabled") == "true");
    CHECK(device->getHardwareInfo().at("station_owner") == "true");
    CHECK(!device->getHardwareInfo().at("adapter_version").empty());
    CHECK(!device->getHardwareInfo().at("adapter_git_revision").empty());
    CHECK(device->getSampleRate(SOAPY_SDR_RX, 0) == 48000.0);
    CHECK(device->getFrequencyRange(SOAPY_SDR_RX, 0).front().minimum() == 100000.0);
    CHECK(device->getFrequencyRange(SOAPY_SDR_RX, 0).back().maximum() == 54000000.0);

    device->setSampleRate(SOAPY_SDR_RX, 0, 48000.0);
    device->setFrequency(SOAPY_SDR_RX, 0, 7100000.0);
    CHECK(device->getFrequency(SOAPY_SDR_RX, 0) == 7100000.0);
    CHECK(device->listGains(SOAPY_SDR_RX, 0) == std::vector<std::string>{"RX"});
    CHECK(device->getGain(SOAPY_SDR_RX, 0) == 20.0);
    device->setGain(SOAPY_SDR_RX, 0, 10.0);
    CHECK(device->getGain(SOAPY_SDR_RX, 0) == 10.0);
    CHECK(device->getGainRange(SOAPY_SDR_RX, 0).minimum() == -10.0);
    CHECK(device->getGainRange(SOAPY_SDR_RX, 0).maximum() == 30.0);
    device->setBandwidth(SOAPY_SDR_RX, 0, 2400.0);
    CHECK(device->getBandwidth(SOAPY_SDR_RX, 0) == 2400.0);
    device->writeSetting("squelch_db", "-60");
    CHECK(device->readSetting("squelch_db") == "-60");

    const std::string listenerCommand = std::string(argv[0]) + " " + port +
        " listener";
    CHECK(std::system(listenerCommand.c_str()) == 0);

    SoapySDR::Stream *stream = device->setupStream(
        SOAPY_SDR_RX, SOAPY_SDR_CF32);
    CHECK(stream != nullptr);
    CHECK(device->activateStream(stream) == 0);
    device->setFrequency(SOAPY_SDR_RX, 0, 7100000.0);
    std::complex<float> samples[3]{};
    void *buffers[] = {samples};
    int flags = 0;
    long long timeNs = 0;
    CHECK(device->readStream(stream, buffers, 3, flags, timeNs, 1000000) == 3);
    CHECK(std::abs(samples[0].real() - 100.0f) < 0.001f);
    CHECK(std::abs(samples[0].imag() - 200.0f) < 0.001f);
    CHECK(std::abs(samples[2].real() - 500.0f) < 0.001f);
    CHECK(std::abs(samples[2].imag() - 600.0f) < 0.001f);
    CHECK(device->deactivateStream(stream) == 0);
    device->closeStream(stream);

    stream = device->setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16);
    CHECK(stream != nullptr);
    CHECK(device->activateStream(stream) == 0);
    int16_t integerSamples[6]{};
    void *integerBuffers[] = {integerSamples};
    flags = 0;
    timeNs = 0;
    CHECK(device->readStream(stream, integerBuffers, 3, flags, timeNs,
                             1000000) == 3);
    CHECK(integerSamples[0] == 100 && integerSamples[1] == 200);
    CHECK(integerSamples[4] == 500 && integerSamples[5] == 600);
    CHECK(device->deactivateStream(stream) == 0);
    device->closeStream(stream);

    CHECK(device->getSampleRate(SOAPY_SDR_TX, 0) == 48000.0);
    CHECK(device->getBandwidth(SOAPY_SDR_TX, 0) == 48000.0);
    CHECK(device->getFrequencyRange(SOAPY_SDR_TX, 0).size() == 9);
    double fullScale = 0.0;
    CHECK(device->getNativeStreamFormat(SOAPY_SDR_TX, 0, fullScale) ==
          SOAPY_SDR_CS16);
    bool rejected = false;
    try {
        device->setFrequency(SOAPY_SDR_TX, 0, 15000000.0);
    } catch (const std::runtime_error &) {
        rejected = true;
    }
    CHECK(rejected);
    device->setFrequency(SOAPY_SDR_TX, 0, 7100000.0);
    device->setGain(SOAPY_SDR_TX, 0, 75.0);
    CHECK(device->getGain(SOAPY_SDR_TX, 0) == 75.0);
    stream = device->setupStream(SOAPY_SDR_TX, SOAPY_SDR_CF32);
    CHECK(stream != nullptr);
    CHECK(device->activateStream(stream) == 0);
    std::vector<std::complex<float>> txSamples(33000, {0.25f, 0.5f});
    flags = 0;
    size_t txOffset = 0;
    while (txOffset < txSamples.size()) {
        const void *part[] = {txSamples.data() + txOffset};
        const int written = device->writeStream(
            stream, part, txSamples.size() - txOffset, flags, timeNs, 1000000);
        CHECK(written > 0);
        txOffset += static_cast<size_t>(written);
    }
    CHECK(device->deactivateStream(stream) == 0);
    device->closeStream(stream);
    SoapySDR::Device::unmake(device);
    std::cout << "SoapySDR RX/TX adapter test passed\n";
    return EXIT_SUCCESS;
}
