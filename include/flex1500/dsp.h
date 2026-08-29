// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_DSP_H
#define FLEX1500_DSP_H

#include "flex1500/iq.h"

#include <stdbool.h>
#include <stddef.h>

enum { FLEX1500_DSP_FIR_TAPS = 129 };

typedef enum flex1500_demod_mode {
    FLEX1500_DEMOD_AM,
    FLEX1500_DEMOD_FM,
    FLEX1500_DEMOD_USB,
    FLEX1500_DEMOD_LSB,
} flex1500_demod_mode;

typedef struct flex1500_dsp_config {
    flex1500_demod_mode mode;
    float sample_rate;
    float tuning_offset_hz;
    bool agc_enabled;
} flex1500_dsp_config;

typedef struct flex1500_dsp {
    flex1500_dsp_config config;
    flex1500_iq_sample coefficients[FLEX1500_DSP_FIR_TAPS];
    flex1500_iq_sample history[FLEX1500_DSP_FIR_TAPS];
    size_t history_index;
    float nco_phase;
    flex1500_iq_sample previous_fm;
    bool have_previous_fm;
    float audio_previous_input;
    float audio_previous_output;
    float agc_envelope;
} flex1500_dsp;

bool flex1500_dsp_init(flex1500_dsp *dsp,
                       const flex1500_dsp_config *config);
size_t flex1500_dsp_process(flex1500_dsp *dsp,
                            const flex1500_iq_sample *input, size_t count,
                            float *audio, size_t audio_capacity);
const char *flex1500_demod_mode_name(flex1500_demod_mode mode);
bool flex1500_parse_demod_mode(const char *text, flex1500_demod_mode *mode);

#endif
