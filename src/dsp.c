// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/dsp.h"

#include <math.h>
#include <string.h>

static const float PI_F = 3.14159265358979323846f;

static void design_filter(flex1500_dsp *dsp)
{
    float cutoff = 6000.0f;
    float center = 0.0f;
    if (dsp->config.mode == FLEX1500_DEMOD_FM) cutoff = 8000.0f;
    if (dsp->config.mode == FLEX1500_DEMOD_USB ||
        dsp->config.mode == FLEX1500_DEMOD_LSB) {
        cutoff = 1350.0f;
        center = dsp->config.mode == FLEX1500_DEMOD_USB ? 1650.0f : -1650.0f;
    }

    float sum = 0.0f;
    const int middle = (FLEX1500_DSP_FIR_TAPS - 1) / 2;
    for (int tap = 0; tap < FLEX1500_DSP_FIR_TAPS; ++tap) {
        int offset = tap - middle;
        float lowpass = offset == 0
            ? 2.0f * cutoff / dsp->config.sample_rate
            : sinf(2.0f * PI_F * cutoff * offset / dsp->config.sample_rate) /
                  (PI_F * offset);
        float window = 0.54f - 0.46f *
            cosf(2.0f * PI_F * tap / (FLEX1500_DSP_FIR_TAPS - 1));
        lowpass *= window;
        float phase = 2.0f * PI_F * center * offset /
                      dsp->config.sample_rate;
        dsp->coefficients[tap].i = lowpass * cosf(phase);
        dsp->coefficients[tap].q = lowpass * sinf(phase);
        sum += lowpass;
    }
    if (center == 0.0f && sum != 0.0f) {
        for (size_t tap = 0; tap < FLEX1500_DSP_FIR_TAPS; ++tap) {
            dsp->coefficients[tap].i /= sum;
        }
    }
}

bool flex1500_dsp_init(flex1500_dsp *dsp,
                       const flex1500_dsp_config *config)
{
    if (dsp == NULL || config == NULL || config->sample_rate <= 0.0f ||
        fabsf(config->tuning_offset_hz) >= config->sample_rate * 0.5f) {
        return false;
    }
    memset(dsp, 0, sizeof(*dsp));
    dsp->config = *config;
    dsp->agc_envelope = 1.0f;
    design_filter(dsp);
    return true;
}

static flex1500_iq_sample filter_sample(flex1500_dsp *dsp,
                                        flex1500_iq_sample sample)
{
    dsp->history[dsp->history_index] = sample;
    flex1500_iq_sample output = {0};
    size_t position = dsp->history_index;
    for (size_t tap = 0; tap < FLEX1500_DSP_FIR_TAPS; ++tap) {
        flex1500_iq_sample x = dsp->history[position];
        flex1500_iq_sample h = dsp->coefficients[tap];
        output.i += x.i * h.i - x.q * h.q;
        output.q += x.i * h.q + x.q * h.i;
        position = position == 0 ? FLEX1500_DSP_FIR_TAPS - 1 : position - 1;
    }
    dsp->history_index = (dsp->history_index + 1) % FLEX1500_DSP_FIR_TAPS;
    return output;
}

static float demodulate(flex1500_dsp *dsp, flex1500_iq_sample sample)
{
    switch (dsp->config.mode) {
    case FLEX1500_DEMOD_AM:
        return hypotf(sample.i, sample.q);
    case FLEX1500_DEMOD_FM: {
        if (!dsp->have_previous_fm) {
            dsp->previous_fm = sample;
            dsp->have_previous_fm = true;
            return 0.0f;
        }
        float cross = dsp->previous_fm.i * sample.q -
                      dsp->previous_fm.q * sample.i;
        float dot = dsp->previous_fm.i * sample.i +
                    dsp->previous_fm.q * sample.q;
        dsp->previous_fm = sample;
        return atan2f(cross, dot) / PI_F;
    }
    case FLEX1500_DEMOD_USB:
    case FLEX1500_DEMOD_LSB:
        return sample.i;
    }
    return 0.0f;
}

size_t flex1500_dsp_process(flex1500_dsp *dsp,
                            const flex1500_iq_sample *input, size_t count,
                            float *audio, size_t audio_capacity)
{
    if (count > audio_capacity) count = audio_capacity;
    float phase_step = -2.0f * PI_F * dsp->config.tuning_offset_hz /
                       dsp->config.sample_rate;
    for (size_t index = 0; index < count; ++index) {
        float cosine = cosf(dsp->nco_phase);
        float sine = sinf(dsp->nco_phase);
        flex1500_iq_sample shifted = {
            input[index].i * cosine - input[index].q * sine,
            input[index].i * sine + input[index].q * cosine,
        };
        dsp->nco_phase += phase_step;
        if (dsp->nco_phase > PI_F) dsp->nco_phase -= 2.0f * PI_F;
        if (dsp->nco_phase < -PI_F) dsp->nco_phase += 2.0f * PI_F;

        float raw = demodulate(dsp, filter_sample(dsp, shifted));
        float highpass = raw - dsp->audio_previous_input +
                         0.995f * dsp->audio_previous_output;
        dsp->audio_previous_input = raw;
        dsp->audio_previous_output = highpass;
        if (dsp->config.agc_enabled) {
            float magnitude = fabsf(highpass);
            float coefficient = magnitude > dsp->agc_envelope ? 0.01f : 0.0001f;
            dsp->agc_envelope += coefficient * (magnitude - dsp->agc_envelope);
            audio[index] = highpass * (0.7f / fmaxf(dsp->agc_envelope, 1.0e-6f));
        } else {
            audio[index] = highpass;
        }
    }
    return count;
}

const char *flex1500_demod_mode_name(flex1500_demod_mode mode)
{
    static const char *names[] = {"am", "fm", "usb", "lsb"};
    return mode <= FLEX1500_DEMOD_LSB ? names[mode] : "unknown";
}

bool flex1500_parse_demod_mode(const char *text, flex1500_demod_mode *mode)
{
    for (int value = FLEX1500_DEMOD_AM; value <= FLEX1500_DEMOD_LSB; ++value) {
        if (strcmp(text, flex1500_demod_mode_name(value)) == 0) {
            *mode = (flex1500_demod_mode)value;
            return true;
        }
    }
    return false;
}
