// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TX_DSP_H
#define FLEX1500_TX_DSP_H

#include "flex1500/iq.h"

#include <stdbool.h>
#include <stddef.h>

enum {
    FLEX1500_TX_FIR_TAPS = 129,
    FLEX1500_TX_DEFAULT_LOW_CUT_HZ = 300,
    FLEX1500_TX_DEFAULT_HIGH_CUT_HZ = 3000,
    FLEX1500_TX_MIN_LOW_CUT_HZ = 50,
    FLEX1500_TX_MAX_HIGH_CUT_HZ = 12000,
    FLEX1500_TX_MIN_PASSBAND_HZ = 100,
    /* Capture-matched PowerSDR AM complex carrier offset. */
    FLEX1500_TX_AM_IF_HZ = 11025,
};

typedef enum flex1500_tx_mode {
    FLEX1500_TX_USB,
    FLEX1500_TX_LSB,
    FLEX1500_TX_AM,
} flex1500_tx_mode;

/* Compatibility name retained for source users of the original SSB API. */
typedef flex1500_tx_mode flex1500_tx_sideband;

typedef struct flex1500_tx_dsp {
    float bandpass[FLEX1500_TX_FIR_TAPS];
    float hilbert[FLEX1500_TX_FIR_TAPS];
    float audio_history[FLEX1500_TX_FIR_TAPS];
    float filtered_history[FLEX1500_TX_FIR_TAPS];
    size_t audio_index;
    size_t filtered_index;
    float previous_input;
    float previous_highpass;
    float carrier_phase;
    unsigned int low_cut_hz;
    unsigned int high_cut_hz;
    flex1500_tx_mode mode;
} flex1500_tx_dsp;

void flex1500_tx_dsp_init(flex1500_tx_dsp *dsp,
                          flex1500_tx_mode mode);
bool flex1500_tx_dsp_init_passband(flex1500_tx_dsp *dsp,
                                   flex1500_tx_mode mode,
                                   unsigned int low_cut_hz,
                                   unsigned int high_cut_hz);
flex1500_iq_sample flex1500_tx_dsp_process(flex1500_tx_dsp *dsp,
                                           float microphone_sample);

#endif
