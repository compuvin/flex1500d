// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TX_DSP_H
#define FLEX1500_TX_DSP_H

#include "flex1500/iq.h"

#include <stddef.h>

enum { FLEX1500_TX_FIR_TAPS = 129 };

typedef enum flex1500_tx_sideband {
    FLEX1500_TX_USB,
    FLEX1500_TX_LSB,
} flex1500_tx_sideband;

typedef struct flex1500_tx_dsp {
    float bandpass[FLEX1500_TX_FIR_TAPS];
    float hilbert[FLEX1500_TX_FIR_TAPS];
    float audio_history[FLEX1500_TX_FIR_TAPS];
    float filtered_history[FLEX1500_TX_FIR_TAPS];
    size_t audio_index;
    size_t filtered_index;
    float previous_input;
    float previous_highpass;
    flex1500_tx_sideband sideband;
} flex1500_tx_dsp;

void flex1500_tx_dsp_init(flex1500_tx_dsp *dsp,
                          flex1500_tx_sideband sideband);
flex1500_iq_sample flex1500_tx_dsp_process(flex1500_tx_dsp *dsp,
                                           float microphone_sample);

#endif
