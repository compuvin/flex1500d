// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_dsp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CHECK failed: %s\n", #x); exit(EXIT_FAILURE); } } while (0)

static float tone_response(flex1500_tx_sideband sideband, float frequency)
{
    const float pi = 3.14159265358979323846f;
    flex1500_tx_dsp dsp;
    flex1500_tx_dsp_init(&dsp, sideband);
    float real = 0.0f, imag = 0.0f;
    for (int n = 0; n < 12000; ++n) {
        float phase = 2.0f * pi * 1000.0f * n / 48000.0f;
        flex1500_iq_sample sample = flex1500_tx_dsp_process(&dsp, cosf(phase));
        if (n > 1000) {
            float probe = 2.0f * pi * frequency * n / 48000.0f;
            real += sample.i * cosf(probe) + sample.q * sinf(probe);
            imag += sample.q * cosf(probe) - sample.i * sinf(probe);
        }
    }
    return hypotf(real, imag);
}

int main(void)
{
    float usb_negative = tone_response(FLEX1500_TX_USB, -1000.0f);
    float usb_positive = tone_response(FLEX1500_TX_USB, 1000.0f);
    float lsb_positive = tone_response(FLEX1500_TX_LSB, 1000.0f);
    float lsb_negative = tone_response(FLEX1500_TX_LSB, -1000.0f);
    CHECK(usb_negative > usb_positive * 20.0f);
    CHECK(lsb_positive > lsb_negative * 20.0f);
    puts("TX DSP sideband tests passed");
    return EXIT_SUCCESS;
}
