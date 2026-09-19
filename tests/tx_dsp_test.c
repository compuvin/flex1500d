// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s\n", #x); exit(EXIT_FAILURE); } } while (0)

static float tone_response(flex1500_tx_sideband sideband, float input_hz,
                           float probe_hz)
{
    const float pi = 3.14159265358979323846f;
    flex1500_tx_dsp dsp;
    flex1500_tx_dsp_init(&dsp, sideband);
    float real = 0.0f, imag = 0.0f;
    for (int n = 0; n < 12000; ++n) {
        float phase = 2.0f * pi * input_hz * n / 48000.0f;
        flex1500_iq_sample sample = flex1500_tx_dsp_process(&dsp, cosf(phase));
        if (n > 1000) {
            float probe = 2.0f * pi * probe_hz * n / 48000.0f;
            real += sample.i * cosf(probe) + sample.q * sinf(probe);
            imag += sample.q * cosf(probe) - sample.i * sinf(probe);
        }
    }
    return hypotf(real, imag);
}

int main(void)
{
    flex1500_tx_dsp configured;
    CHECK(flex1500_tx_dsp_init_passband(
        &configured, FLEX1500_TX_USB, 300, 3000));
    CHECK(configured.low_cut_hz == 300 && configured.high_cut_hz == 3000);
    CHECK(!flex1500_tx_dsp_init_passband(
        &configured, FLEX1500_TX_USB, 49, 3000));
    CHECK(!flex1500_tx_dsp_init_passband(
        &configured, FLEX1500_TX_USB, 300, 12001));
    CHECK(!flex1500_tx_dsp_init_passband(
        &configured, FLEX1500_TX_USB, 300, 399));
    CHECK(!flex1500_tx_dsp_init_passband(
        &configured, (flex1500_tx_sideband)99, 300, 3000));

    float usb_negative = tone_response(FLEX1500_TX_USB, 1000.0f, -1000.0f);
    float usb_positive = tone_response(FLEX1500_TX_USB, 1000.0f, 1000.0f);
    float lsb_positive = tone_response(FLEX1500_TX_LSB, 1000.0f, 1000.0f);
    float lsb_negative = tone_response(FLEX1500_TX_LSB, 1000.0f, -1000.0f);
    CHECK(usb_negative > usb_positive * 20.0f);
    CHECK(lsb_positive > lsb_negative * 20.0f);

    float am_carrier = tone_response(
        FLEX1500_TX_AM, 1000.0f, -(float)FLEX1500_TX_AM_IF_HZ);
    float am_upper = tone_response(
        FLEX1500_TX_AM, 1000.0f,
        -(float)FLEX1500_TX_AM_IF_HZ + 1000.0f);
    float am_lower = tone_response(
        FLEX1500_TX_AM, 1000.0f,
        -(float)FLEX1500_TX_AM_IF_HZ - 1000.0f);
    CHECK(am_carrier > am_upper * 2.0f);
    CHECK(am_upper > am_carrier * 0.35f);
    CHECK(fabsf(am_upper - am_lower) < am_upper * 0.01f);
    flex1500_tx_dsp am;
    flex1500_tx_dsp_init(&am, FLEX1500_TX_AM);
    for (int n = 0; n < 1000; ++n) {
        flex1500_iq_sample sample = flex1500_tx_dsp_process(
            &am, cosf(2.0f * 3.14159265358979323846f * 1000.0f * n /
                      48000.0f));
        float magnitude = hypotf(sample.i, sample.q);
        CHECK(magnitude >= 0.0f && magnitude <= 1.0001f);
    }

    float dc = tone_response(FLEX1500_TX_USB, 0.0f, 0.0f);
    float below_passband = tone_response(FLEX1500_TX_USB, 50.0f, -50.0f);
    float above_passband = tone_response(FLEX1500_TX_USB, 5000.0f, -5000.0f);
    CHECK(usb_negative > dc * 100.0f);
    CHECK(usb_negative > below_passband * 6.0f);
    CHECK(usb_negative > above_passband * 20.0f);
    puts("TX DSP SSB/AM spectral tests passed");
    return EXIT_SUCCESS;
}
