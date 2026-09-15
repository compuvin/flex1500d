// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_TX_AUDIO_STREAM_H
#define FLEX1500_TX_AUDIO_STREAM_H

#include "flex1500/tx_dsp.h"
#include "flex1500/tx_limits.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    FLEX1500_TX_AUDIO_CAPACITY = 65536,
    FLEX1500_TX_FULL_DRIVE_PEAK = 24890,
};

typedef struct flex1500_tx_audio_stats {
    uint64_t microphone_frames;
    uint64_t output_frames;
    uint64_t dropped_microphone_frames;
    uint64_t underrun_frames;
    uint64_t clipped_frames;
    uint64_t limited_frames;
    uint64_t peak_queued_frames;
    uint64_t stop_requested_frames;
    uint64_t graceful_drained_frames;
    uint64_t graceful_discarded_frames;
    uint64_t graceful_drain_ms;
    float input_peak;
    float input_rms;
    float post_gain_peak;
    float post_gain_rms;
    float output_peak;
    float output_rms;
    float peak_magnitude;
} flex1500_tx_audio_stats;

typedef struct flex1500_tx_audio_stream {
    flex1500_tx_dsp dsp;
    float microphone[FLEX1500_TX_AUDIO_CAPACITY];
    flex1500_iq_sample network_iq[FLEX1500_TX_AUDIO_CAPACITY];
    bool raw_iq;
    size_t read_index;
    size_t write_index;
    size_t count;
    unsigned int drive_percent;
    float microphone_gain;
    bool compressor_enabled;
    double input_square_sum;
    double post_gain_square_sum;
    double output_square_sum;
    uint64_t post_gain_frames;
    size_t fade_position;
    flex1500_tx_audio_stats stats;
} flex1500_tx_audio_stream;

bool flex1500_tx_audio_stream_init(flex1500_tx_audio_stream *stream,
                                   flex1500_tx_sideband sideband,
                                   unsigned int drive_percent,
                                   float microphone_gain);
void flex1500_tx_audio_stream_reset(flex1500_tx_audio_stream *stream,
                                    flex1500_tx_sideband sideband);
void flex1500_tx_audio_stream_set_compressor(flex1500_tx_audio_stream *stream,
                                             bool enabled);
size_t flex1500_tx_audio_stream_push_iq16le(
    flex1500_tx_audio_stream *stream, const uint8_t *input, size_t bytes);
size_t flex1500_tx_audio_stream_push_pcm16le(
    flex1500_tx_audio_stream *stream, const uint8_t *input, size_t bytes);
void flex1500_tx_audio_stream_set_raw_iq(flex1500_tx_audio_stream *stream,
                                         bool enabled);
size_t flex1500_tx_audio_stream_render_iq16le(
    flex1500_tx_audio_stream *stream, uint8_t *output, size_t frames);
size_t flex1500_tx_audio_stream_available(
    const flex1500_tx_audio_stream *stream);
size_t flex1500_tx_audio_stream_queued(
    const flex1500_tx_audio_stream *stream);
const flex1500_tx_audio_stats *flex1500_tx_audio_stream_stats(
    const flex1500_tx_audio_stream *stream);

#endif
