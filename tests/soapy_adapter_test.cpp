// SPDX-License-Identifier: GPL-3.0-only

#include "test_assert.h"

#include <SoapySDR/Constants.h>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.hpp>

#include <cmath>
#include <complex>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const std::string port = argv[1];
    const SoapySDR::Kwargs args{{"driver", "flex1500"},
                                {"host", "127.0.0.1"}, {"port", port}};
    const auto devices = SoapySDR::Device::enumerate(args);
    CHECK(devices.size() == 1);

    SoapySDR::Device *device = SoapySDR::Device::make(args);
    CHECK(device != nullptr);
    CHECK(device->getDriverKey() == "flex1500");
    CHECK(device->getNumChannels(SOAPY_SDR_RX) == 1);
    CHECK(device->getNumChannels(SOAPY_SDR_TX) == 0);
    CHECK(device->getSampleRate(SOAPY_SDR_RX, 0) == 48000.0);
    CHECK(device->getFrequencyRange(SOAPY_SDR_RX, 0).front().minimum() == 100000.0);
    CHECK(device->getFrequencyRange(SOAPY_SDR_RX, 0).back().maximum() == 54000000.0);

    device->setSampleRate(SOAPY_SDR_RX, 0, 48000.0);
    device->setFrequency(SOAPY_SDR_RX, 0, 7100000.0);
    CHECK(device->getFrequency(SOAPY_SDR_RX, 0) == 7100000.0);

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
    CHECK(std::abs(samples[0].imag() + 200.0f) < 0.001f);
    CHECK(std::abs(samples[2].real() - 500.0f) < 0.001f);
    CHECK(std::abs(samples[2].imag() + 600.0f) < 0.001f);
    CHECK(device->deactivateStream(stream) == 0);
    device->closeStream(stream);
    SoapySDR::Device::unmake(device);
    std::cout << "receive-only SoapySDR adapter test passed\n";
    return EXIT_SUCCESS;
}
