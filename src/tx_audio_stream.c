// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/tx_audio_stream.h"

#include <math.h>
#include <string.h>

#define COMPRESSOR_THRESHOLD 0.25f
#define COMPRESSOR_RATIO 3.0f

static float raised_cosine(float position)
{
    const float pi = 3.14159265358979323846f;
    if (position <= 0.0f) return 0.0f;
    if (position >= 1.0f) return 1.0f;
    return 0.5f - 0.5f * cosf(pi * position);
}

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
    if (stream == NULL ||
        drive_percent < FLEX1500_TX_MIN_DRIVE_PERCENT ||
        drive_percent > FLEX1500_TX_MAX_DRIVE_PERCENT ||
        !isfinite(microphone_gain) || microphone_gain <= 0.0f) {
        return false;
    }
    memset(stream, 0, sizeof(*stream));
    stream->drive_percent = drive_percent;
    stream->microphone_gain = microphone_gain;
    return flex1500_tx_dsp_init_passband(
        &stream->dsp, sideband, FLEX1500_TX_DEFAULT_LOW_CUT_HZ,
        FLEX1500_TX_DEFAULT_HIGH_CUT_HZ);
}

void flex1500_tx_audio_stream_reset(flex1500_tx_audio_stream *stream,
                                    flex1500_tx_sideband sideband)
{
    if (stream == NULL) return;
    stream->read_index = 0;
    stream->write_index = 0;
    stream->count = 0;
    stream->fade_position = 0;
    stream->graceful_stop_requested = false;
    stream->previous_output_valid = false;
    stream->previous_output_i = 0.0f;
    stream->previous_output_q = 0.0f;
    flex1500_tx_dsp_init(&stream->dsp, sideband);
}

void flex1500_tx_audio_stream_set_compressor(flex1500_tx_audio_stream *stream,
                                             bool enabled)
{
    if (stream != NULL) stream->compressor_enabled = enabled;
}

void flex1500_tx_audio_stream_begin_graceful_stop(
    flex1500_tx_audio_stream *stream)
{
    if (stream != NULL && !stream->raw_iq) {
        stream->graceful_stop_requested = true;
    }
}

void flex1500_tx_audio_stream_set_raw_iq(flex1500_tx_audio_stream *stream,
                                         bool enabled)
{
    if (stream != NULL) stream->raw_iq = enabled;
}

size_t flex1500_tx_audio_stream_push_pcm16le(
    flex1500_tx_audio_stream *stream, const uint8_t *input, size_t bytes)
{
    if (stream == NULL || input == NULL || stream->raw_iq ||
        stream->graceful_stop_requested) return 0;
    size_t frames = bytes / 2, accepted = 0;
    for (size_t frame = 0; frame < frames; ++frame) {
        int16_t sample = (int16_t)((uint16_t)input[frame * 2] |
            ((uint16_t)input[frame * 2 + 1] << 8));
        if (stream->count == FLEX1500_TX_AUDIO_CAPACITY) {
            ++stream->stats.dropped_microphone_frames;
            continue;
        }
        float normalized = (float)sample / 32768.0f;
        stream->microphone[stream->write_index] = normalized;
        stream->write_index = (stream->write_index + 1) % FLEX1500_TX_AUDIO_CAPACITY;
        ++stream->count; ++accepted;
        if (stream->count > stream->stats.peak_queued_frames) {
            stream->stats.peak_queued_frames = stream->count;
        }
        float absolute = fabsf(normalized);
        if (absolute > stream->stats.input_peak) stream->stats.input_peak = absolute;
        stream->input_square_sum += (double)normalized * normalized;
    }
    stream->stats.microphone_frames += frames;
    if (stream->stats.microphone_frames != 0) stream->stats.input_rms =
        (float)sqrt(stream->input_square_sum / stream->stats.microphone_frames);
    return accepted;
}

size_t flex1500_tx_audio_stream_push_iq16le(
    flex1500_tx_audio_stream *stream, const uint8_t *input, size_t bytes)
{
    if (stream == NULL || input == NULL ||
        stream->graceful_stop_requested) return 0;
    size_t frames = bytes / 4;
    size_t accepted = 0;
    for (size_t frame = 0; frame < frames; ++frame) {
        int16_t microphone = (int16_t)((uint16_t)input[frame * 4] |
            ((uint16_t)input[frame * 4 + 1] << 8));
        if (stream->count == FLEX1500_TX_AUDIO_CAPACITY) {
            ++stream->stats.dropped_microphone_frames;
            continue;
        }
        float normalized = (float)microphone / 32768.0f;
        if (stream->raw_iq) {
            int16_t q = (int16_t)((uint16_t)input[frame * 4 + 2] |
                ((uint16_t)input[frame * 4 + 3] << 8));
            stream->network_iq[stream->write_index] = (flex1500_iq_sample){
                normalized, (float)q / 32768.0f};
        } else {
            stream->microphone[stream->write_index] = normalized;
        }
        float absolute = fabsf(normalized);
        if (absolute > stream->stats.input_peak) {
            stream->stats.input_peak = absolute;
        }
        stream->input_square_sum += (double)normalized * normalized;
        stream->write_index =
            (stream->write_index + 1) % FLEX1500_TX_AUDIO_CAPACITY;
        ++stream->count;
        ++accepted;
        if (stream->count > stream->stats.peak_queued_frames) {
            stream->stats.peak_queued_frames = stream->count;
        }
    }
    stream->stats.microphone_frames += frames;
    if (stream->stats.microphone_frames != 0) {
        stream->stats.input_rms = (float)sqrt(
            stream->input_square_sum / stream->stats.microphone_frames);
    }
    return accepted;
}

size_t flex1500_tx_audio_stream_render_iq16le(
    flex1500_tx_audio_stream *stream, uint8_t *output, size_t frames)
{
    if (stream == NULL || output == NULL) return 0;
    const float drive_limit = (float)FLEX1500_TX_FULL_DRIVE_PEAK *
        (float)stream->drive_percent / 100.0f;
    for (size_t frame = 0; frame < frames; ++frame) {
        flex1500_iq_sample iq = {0};
        if (stream->count > 0) {
            float microphone = stream->microphone[stream->read_index];
            if (stream->raw_iq) iq = stream->network_iq[stream->read_index];
            stream->read_index =
                (stream->read_index + 1) % FLEX1500_TX_AUDIO_CAPACITY;
            --stream->count;
            microphone *= stream->microphone_gain;
            float absolute = fabsf(microphone);
            if (absolute > stream->stats.post_gain_peak) {
                stream->stats.post_gain_peak = absolute;
            }
            stream->post_gain_square_sum += (double)microphone * microphone;
            ++stream->post_gain_frames;
            stream->stats.post_gain_rms = (float)sqrt(
                stream->post_gain_square_sum / stream->post_gain_frames);
            if (!stream->raw_iq && stream->compressor_enabled &&
                absolute > COMPRESSOR_THRESHOLD) {
                float compressed = COMPRESSOR_THRESHOLD * powf(
                    absolute / COMPRESSOR_THRESHOLD,
                    1.0f / COMPRESSOR_RATIO);
                microphone = copysignf(compressed, microphone);
            }
            if (!stream->raw_iq) iq = flex1500_tx_dsp_process(&stream->dsp, microphone);
        } else {
            ++stream->stats.underrun_frames;
            /* Advance with silence so filter state decays continuously. */
            iq = flex1500_tx_dsp_process(&stream->dsp, 0.0f);
        }
        float fade = stream->fade_position < FLEX1500_TX_ENVELOPE_FRAMES
            ? raised_cosine((float)stream->fade_position /
                            FLEX1500_TX_ENVELOPE_FRAMES)
            : 1.0f;
        if (stream->fade_position < FLEX1500_TX_ENVELOPE_FRAMES) {
            ++stream->fade_position;
        }
        if (stream->graceful_stop_requested &&
            stream->count < FLEX1500_TX_ENVELOPE_FRAMES) {
            fade *= raised_cosine((float)stream->count /
                                  FLEX1500_TX_ENVELOPE_FRAMES);
        }
        float output_i = iq.i * drive_limit * fade;
        float output_q = iq.q * drive_limit * fade;
        float requested_magnitude = hypotf(output_i, output_q);
        if (requested_magnitude > drive_limit && requested_magnitude > 0.0f) {
            float limiter_gain = drive_limit / requested_magnitude;
            output_i *= limiter_gain;
            output_q *= limiter_gain;
            ++stream->stats.limited_frames;
        }
        bool clipped = false;
        int16_t sample_i = clamp_i16(output_i, &clipped);
        int16_t sample_q = clamp_i16(output_q, &clipped);
        if (clipped) ++stream->stats.clipped_frames;
        if (stream->previous_output_valid) {
            float step = hypotf((float)sample_i - stream->previous_output_i,
                                (float)sample_q - stream->previous_output_q);
            if (step > stream->stats.maximum_output_step) {
                stream->stats.maximum_output_step = step;
            }
        }
        stream->previous_output_i = sample_i;
        stream->previous_output_q = sample_q;
        stream->previous_output_valid = true;
        float magnitude = hypotf((float)sample_i, (float)sample_q);
        float normalized_output = drive_limit > 0.0f
            ? magnitude / (float)FLEX1500_TX_FULL_DRIVE_PEAK : 0.0f;
        if (normalized_output > stream->stats.output_peak) {
            stream->stats.output_peak = normalized_output;
        }
        stream->output_square_sum +=
            (double)normalized_output * normalized_output;
        if (magnitude > stream->stats.peak_magnitude) {
            stream->stats.peak_magnitude = magnitude;
        }
        output[frame * 4] = (uint8_t)sample_i;
        output[frame * 4 + 1] = (uint8_t)((uint16_t)sample_i >> 8);
        output[frame * 4 + 2] = (uint8_t)sample_q;
        output[frame * 4 + 3] = (uint8_t)((uint16_t)sample_q >> 8);
    }
    stream->stats.output_frames += frames;
    if (stream->stats.output_frames != 0) {
        stream->stats.output_rms = (float)sqrt(
            stream->output_square_sum / stream->stats.output_frames);
    }
    return frames;
}

size_t flex1500_tx_audio_stream_available(
    const flex1500_tx_audio_stream *stream)
{
    return stream != NULL && stream->count <= FLEX1500_TX_AUDIO_CAPACITY
        ? FLEX1500_TX_AUDIO_CAPACITY - stream->count : 0;
}

size_t flex1500_tx_audio_stream_queued(
    const flex1500_tx_audio_stream *stream)
{
    return stream != NULL ? stream->count : 0;
}

const flex1500_tx_audio_stats *flex1500_tx_audio_stream_stats(
    const flex1500_tx_audio_stream *stream)
{
    return stream != NULL ? &stream->stats : NULL;
}
