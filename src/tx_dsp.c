// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_dsp.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SAMPLE_RATE 48000.0f
#define AM_MODULATION_INDEX 0.8f
static const float PI_F = 3.14159265358979323846f;

static float lowpass_coefficient(float cutoff, int offset)
{
    if (offset == 0) return 2.0f * cutoff / SAMPLE_RATE;
    return sinf(2.0f * PI_F * cutoff * offset / SAMPLE_RATE) /
           (PI_F * offset);
}

void flex1500_tx_dsp_init(flex1500_tx_dsp *dsp,
                          flex1500_tx_mode mode)
{
    (void)flex1500_tx_dsp_init_passband(
        dsp, mode, FLEX1500_TX_DEFAULT_LOW_CUT_HZ,
        FLEX1500_TX_DEFAULT_HIGH_CUT_HZ);
}

bool flex1500_tx_dsp_init_passband(flex1500_tx_dsp *dsp,
                                   flex1500_tx_mode mode,
                                   unsigned int low_cut_hz,
                                   unsigned int high_cut_hz)
{
    if (dsp == NULL ||
        (mode != FLEX1500_TX_USB && mode != FLEX1500_TX_LSB &&
         mode != FLEX1500_TX_AM) ||
        low_cut_hz < FLEX1500_TX_MIN_LOW_CUT_HZ ||
        high_cut_hz > FLEX1500_TX_MAX_HIGH_CUT_HZ ||
        high_cut_hz < low_cut_hz + FLEX1500_TX_MIN_PASSBAND_HZ) {
        return false;
    }
    memset(dsp, 0, sizeof(*dsp));
    dsp->mode = mode;
    dsp->low_cut_hz = low_cut_hz;
    dsp->high_cut_hz = high_cut_hz;
    const int middle = (FLEX1500_TX_FIR_TAPS - 1) / 2;
    for (int tap = 0; tap < FLEX1500_TX_FIR_TAPS; ++tap) {
        int offset = tap - middle;
        float window = 0.54f - 0.46f *
            cosf(2.0f * PI_F * tap / (FLEX1500_TX_FIR_TAPS - 1));
        dsp->bandpass[tap] =
            (lowpass_coefficient((float)high_cut_hz, offset) -
             lowpass_coefficient((float)low_cut_hz, offset)) * window;
        if (offset != 0 && (abs(offset) & 1) != 0) {
            dsp->hilbert[tap] = 2.0f * window / (PI_F * offset);
        }
    }
    return true;
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
    float in_phase = dsp->filtered_history[delayed];
    flex1500_iq_sample output;
    if (dsp->mode == FLEX1500_TX_AM) {
        /* PowerSDR places AM at -11.025 kHz rather than complex DC. */
        float bounded = fmaxf(-1.0f, fminf(1.0f, in_phase));
        float carrier = 1.0f / (1.0f + AM_MODULATION_INDEX);
        float envelope = carrier * (1.0f + AM_MODULATION_INDEX * bounded);
        output = (flex1500_iq_sample){
            envelope * cosf(dsp->carrier_phase),
            -envelope * sinf(dsp->carrier_phase)};
        dsp->carrier_phase +=
            2.0f * PI_F * FLEX1500_TX_AM_IF_HZ / SAMPLE_RATE;
        if (dsp->carrier_phase >= 2.0f * PI_F) {
            dsp->carrier_phase -= 2.0f * PI_F;
        }
    } else {
        float quadrature = fir(dsp->hilbert, dsp->filtered_history,
                               dsp->filtered_index);
        output = (flex1500_iq_sample){in_phase, quadrature};
        if (dsp->mode == FLEX1500_TX_USB) output.q = -output.q;
    }
    dsp->filtered_index = (dsp->filtered_index + 1) % FLEX1500_TX_FIR_TAPS;
    return output;
}
