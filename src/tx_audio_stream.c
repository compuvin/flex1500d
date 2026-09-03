// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_audio_stream.h"

#include <math.h>
#include <string.h>

enum { START_FADE_FRAMES = 480 };

static int16_t clamp_i16(float sample, bool *clipped)
{
    if (sample > 32767.0f) {
        *clipped = true;
        return INT16_MAX;
    }
    if (sample < -32768.0f) {
        *clipped = true;
        return INT16_MIN;
    }
    return (int16_t)lrintf(sample);
}

bool flex1500_tx_audio_stream_init(flex1500_tx_audio_stream *stream,
                                   flex1500_tx_sideband sideband,
                                   unsigned int drive_percent,
                                   float microphone_gain)
{
    if (stream == NULL || drive_percent == 0 || drive_percent > 100 ||
        !isfinite(microphone_gain) || microphone_gain <= 0.0f) {
        return false;
    }
    memset(stream, 0, sizeof(*stream));
    stream->drive_percent = drive_percent;
    stream->microphone_gain = microphone_gain;
    flex1500_tx_dsp_init(&stream->dsp, sideband);
    return true;
}

void flex1500_tx_audio_stream_reset(flex1500_tx_audio_stream *stream,
                                    flex1500_tx_sideband sideband)
{
    if (stream == NULL) return;
    stream->read_index = 0;
    stream->write_index = 0;
    stream->count = 0;
    stream->fade_position = 0;
    flex1500_tx_dsp_init(&stream->dsp, sideband);
}

size_t flex1500_tx_audio_stream_push_iq16le(
    flex1500_tx_audio_stream *stream, const uint8_t *input, size_t bytes)
{
    if (stream == NULL || input == NULL) return 0;
    size_t frames = bytes / 4;
    size_t accepted = 0;
    for (size_t frame = 0; frame < frames; ++frame) {
        int16_t microphone = (int16_t)((uint16_t)input[frame * 4] |
            ((uint16_t)input[frame * 4 + 1] << 8));
        if (stream->count == FLEX1500_TX_AUDIO_CAPACITY) {
            ++stream->stats.dropped_microphone_frames;
            continue;
        }
        stream->microphone[stream->write_index] =
            (float)microphone / 32768.0f;
        stream->write_index =
            (stream->write_index + 1) % FLEX1500_TX_AUDIO_CAPACITY;
        ++stream->count;
        ++accepted;
    }
    stream->stats.microphone_frames += frames;
    return accepted;
}

size_t flex1500_tx_audio_stream_render_iq16le(
    flex1500_tx_audio_stream *stream, uint8_t *output, size_t frames)
{
    if (stream == NULL || output == NULL) return 0;
    const float scale = (float)FLEX1500_TX_FULL_DRIVE_PEAK *
        (float)stream->drive_percent / 100.0f * stream->microphone_gain;
    for (size_t frame = 0; frame < frames; ++frame) {
        flex1500_iq_sample iq = {0};
        if (stream->count > 0) {
            float microphone = stream->microphone[stream->read_index];
            stream->read_index =
                (stream->read_index + 1) % FLEX1500_TX_AUDIO_CAPACITY;
            --stream->count;
            iq = flex1500_tx_dsp_process(&stream->dsp, microphone);
        } else {
            ++stream->stats.underrun_frames;
            /* Advance with silence so filter state decays continuously. */
            iq = flex1500_tx_dsp_process(&stream->dsp, 0.0f);
        }
        float fade = stream->fade_position < START_FADE_FRAMES
            ? (float)stream->fade_position / START_FADE_FRAMES : 1.0f;
        if (stream->fade_position < START_FADE_FRAMES) {
            ++stream->fade_position;
        }
        bool clipped = false;
        int16_t sample_i = clamp_i16(iq.i * scale * fade, &clipped);
        int16_t sample_q = clamp_i16(iq.q * scale * fade, &clipped);
        if (clipped) ++stream->stats.clipped_frames;
        float magnitude = hypotf((float)sample_i, (float)sample_q);
        if (magnitude > stream->stats.peak_magnitude) {
            stream->stats.peak_magnitude = magnitude;
        }
        output[frame * 4] = (uint8_t)sample_i;
        output[frame * 4 + 1] = (uint8_t)((uint16_t)sample_i >> 8);
        output[frame * 4 + 2] = (uint8_t)sample_q;
        output[frame * 4 + 3] = (uint8_t)((uint16_t)sample_q >> 8);
    }
    stream->stats.output_frames += frames;
    return frames;
}

const flex1500_tx_audio_stats *flex1500_tx_audio_stream_stats(
    const flex1500_tx_audio_stream *stream)
{
    return stream != NULL ? &stream->stats : NULL;
}
