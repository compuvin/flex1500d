// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/dsp.h"

#include "test_assert.h"
#include <math.h>
#include <string.h>

enum { SAMPLE_RATE = 48000, SAMPLE_COUNT = 12000 };
static const float PI_F = 3.14159265358979323846f;

static float tone_rms(flex1500_demod_mode mode, float frequency)
{
    flex1500_dsp dsp;
    flex1500_dsp_config config = {
        .mode = mode,
        .sample_rate = SAMPLE_RATE,
        .agc_enabled = false,
    };
    CHECK(flex1500_dsp_init(&dsp, &config));
    double energy = 0.0;
    for (int index = 0; index < SAMPLE_COUNT; ++index) {
        float phase = 2.0f * PI_F * frequency * index / SAMPLE_RATE;
        flex1500_iq_sample input = {cosf(phase), sinf(phase)};
        float output;
        CHECK(flex1500_dsp_process(&dsp, &input, 1, &output, 1) == 1);
        if (index > 1000) energy += output * output;
    }
    return sqrtf((float)(energy / (SAMPLE_COUNT - 1001)));
}

static void test_sideband_selection(void)
{
    float usb_wanted = tone_rms(FLEX1500_DEMOD_USB, 1000.0f);
    float usb_rejected = tone_rms(FLEX1500_DEMOD_USB, -1000.0f);
    float lsb_wanted = tone_rms(FLEX1500_DEMOD_LSB, -1000.0f);
    float lsb_rejected = tone_rms(FLEX1500_DEMOD_LSB, 1000.0f);
    CHECK(usb_wanted > 0.3f);
    CHECK(lsb_wanted > 0.3f);
    CHECK(usb_rejected < usb_wanted * 0.02f);
    CHECK(lsb_rejected < lsb_wanted * 0.02f);
}

static void test_am(void)
{
    flex1500_dsp dsp;
    flex1500_dsp_config config = {
        .mode = FLEX1500_DEMOD_AM,
        .sample_rate = SAMPLE_RATE,
        .agc_enabled = false,
    };
    CHECK(flex1500_dsp_init(&dsp, &config));
    double correlation_cos = 0.0;
    double correlation_sin = 0.0;
    for (int index = 0; index < SAMPLE_COUNT; ++index) {
        float reference = cosf(2.0f * PI_F * 1000.0f * index / SAMPLE_RATE);
        flex1500_iq_sample input = {1.0f + 0.5f * reference, 0.0f};
        float output;
        flex1500_dsp_process(&dsp, &input, 1, &output, 1);
        if (index > 1000) {
            correlation_cos += output * reference;
            correlation_sin += output *
                sinf(2.0f * PI_F * 1000.0f * index / SAMPLE_RATE);
        }
    }
    CHECK(hypot(correlation_cos, correlation_sin) > 1000.0);
}

static void test_fm_and_tuning(void)
{
    flex1500_dsp dsp;
    flex1500_dsp_config config = {
        .mode = FLEX1500_DEMOD_FM,
        .sample_rate = SAMPLE_RATE,
        .tuning_offset_hz = 4000.0f,
        .agc_enabled = false,
    };
    CHECK(flex1500_dsp_init(&dsp, &config));
    double correlation_cos = 0.0;
    double correlation_sin = 0.0;
    for (int index = 0; index < SAMPLE_COUNT; ++index) {
        float audio_phase = 2.0f * PI_F * 1000.0f * index / SAMPLE_RATE;
        float phase = 2.0f * PI_F * 4000.0f * index / SAMPLE_RATE +
                      sinf(audio_phase);
        flex1500_iq_sample input = {cosf(phase), sinf(phase)};
        float output;
        flex1500_dsp_process(&dsp, &input, 1, &output, 1);
        if (index > 1000) {
            correlation_cos += output * cosf(audio_phase);
            correlation_sin += output * sinf(audio_phase);
        }
    }
    CHECK(hypot(correlation_cos, correlation_sin) > 100.0);
}

int main(void)
{
    flex1500_demod_mode mode;
    CHECK(flex1500_parse_demod_mode("am", &mode));
    CHECK(mode == FLEX1500_DEMOD_AM);
    CHECK(flex1500_parse_demod_mode("lsb", &mode));
    CHECK(mode == FLEX1500_DEMOD_LSB);
    CHECK(!flex1500_parse_demod_mode("cw", &mode));
    test_sideband_selection();
    test_am();
    test_fm_and_tuning();
    return 0;
}
