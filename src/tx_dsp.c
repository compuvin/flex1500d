// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 48000.0f
#define LOW_CUT_HZ 300.0f
#define HIGH_CUT_HZ 3000.0f

static const float PI_F = 3.14159265358979323846f;

static float lowpass_coefficient(float cutoff, int offset)
{
    if (offset == 0) return 2.0f * cutoff / SAMPLE_RATE;
    return sinf(2.0f * PI_F * cutoff * offset / SAMPLE_RATE) /
           (PI_F * offset);
}

void flex1500_tx_dsp_init(flex1500_tx_dsp *dsp,
                          flex1500_tx_sideband sideband)
{
    memset(dsp, 0, sizeof(*dsp));
    dsp->sideband = sideband;
    const int middle = (FLEX1500_TX_FIR_TAPS - 1) / 2;
    for (int tap = 0; tap < FLEX1500_TX_FIR_TAPS; ++tap) {
        int offset = tap - middle;
        float window = 0.54f - 0.46f *
            cosf(2.0f * PI_F * tap / (FLEX1500_TX_FIR_TAPS - 1));
        dsp->bandpass[tap] =
            (lowpass_coefficient(HIGH_CUT_HZ, offset) -
             lowpass_coefficient(LOW_CUT_HZ, offset)) * window;
        if (offset != 0 && (abs(offset) & 1) != 0) {
            dsp->hilbert[tap] = 2.0f * window / (PI_F * offset);
        }
    }
}

static float fir(const float *coefficients, const float *history,
                 size_t newest)
{
    float output = 0.0f;
    size_t position = newest;
    for (size_t tap = 0; tap < FLEX1500_TX_FIR_TAPS; ++tap) {
        output += coefficients[tap] * history[position];
        position = position == 0 ? FLEX1500_TX_FIR_TAPS - 1 : position - 1;
    }
    return output;
}

flex1500_iq_sample flex1500_tx_dsp_process(flex1500_tx_dsp *dsp,
                                           float microphone_sample)
{
    float highpass = microphone_sample - dsp->previous_input +
                     0.995f * dsp->previous_highpass;
    dsp->previous_input = microphone_sample;
    dsp->previous_highpass = highpass;

    dsp->audio_history[dsp->audio_index] = highpass;
    float filtered = fir(dsp->bandpass, dsp->audio_history, dsp->audio_index);
    dsp->audio_index = (dsp->audio_index + 1) % FLEX1500_TX_FIR_TAPS;

    dsp->filtered_history[dsp->filtered_index] = filtered;
    size_t delayed = (dsp->filtered_index + FLEX1500_TX_FIR_TAPS -
                      (FLEX1500_TX_FIR_TAPS - 1) / 2) %
                     FLEX1500_TX_FIR_TAPS;
    float quadrature = fir(dsp->hilbert, dsp->filtered_history,
                           dsp->filtered_index);
    flex1500_iq_sample output = {dsp->filtered_history[delayed], quadrature};
    if (dsp->sideband == FLEX1500_TX_USB) output.q = -output.q;
    dsp->filtered_index = (dsp->filtered_index + 1) % FLEX1500_TX_FIR_TAPS;
    return output;
}
