// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/pipewire_source.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

namespace flex1500::client {

PipeWireSource::PipeWireSource(AudioRing &audio) : audio_(audio)
{
}

PipeWireSource::~PipeWireSource()
{
    stop();
}

void PipeWireSource::process(void *data)
{
    auto *source = static_cast<PipeWireSource *>(data);
    pw_buffer *pw_buffer = pw_stream_dequeue_buffer(source->stream_);
    if (pw_buffer == nullptr) return;
    spa_buffer *buffer = pw_buffer->buffer;
    spa_data &plane = buffer->datas[0];
    if (plane.data == nullptr || plane.chunk == nullptr) {
        pw_stream_queue_buffer(source->stream_, pw_buffer);
        return;
    }
    auto *output = static_cast<float *>(plane.data);
    const std::size_t capacity = plane.maxsize / sizeof(float);
    const std::size_t requested = pw_buffer->requested > 0
        ? std::min<std::size_t>(pw_buffer->requested, capacity) : capacity;
    const std::size_t produced = source->audio_.pop(output, requested);
    std::fill(output + produced, output + requested, 0.0f);
    plane.chunk->offset = 0;
    plane.chunk->stride = sizeof(float);
    plane.chunk->size = requested * sizeof(float);
    pw_stream_queue_buffer(source->stream_, pw_buffer);
}

void PipeWireSource::state_changed(void *data, pw_stream_state,
                                   pw_stream_state state, const char *)
{
    auto *source = static_cast<PipeWireSource *>(data);
    if (state == PW_STREAM_STATE_PAUSED || state == PW_STREAM_STATE_STREAMING)
        source->ready_.store(true);
}

void PipeWireSource::start()
{
    if (loop_ != nullptr) return;
    pw_init(nullptr, nullptr);
    loop_ = pw_main_loop_new(nullptr);
    if (loop_ == nullptr) throw std::runtime_error("create PipeWire loop failed");

    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.state_changed = state_changed;
        value.process = process;
        return value;
    }();
    stream_ = pw_stream_new_simple(
        pw_main_loop_get_loop(loop_), "FLEX-1500 RX",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_CLASS, "Audio/Source",
                          PW_KEY_NODE_NAME, "flex1500-client-rx",
                          PW_KEY_NODE_DESCRIPTION, "FLEX-1500 RX", nullptr),
        &events, this);
    if (stream_ == nullptr) throw std::runtime_error("create PipeWire source failed");

    std::uint8_t storage[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(storage, sizeof(storage));
    spa_audio_info_raw format{};
    format.format = SPA_AUDIO_FORMAT_F32;
    format.rate = 48000;
    format.channels = 1;
    format.position[0] = SPA_AUDIO_CHANNEL_MONO;
    const spa_pod *parameters[] = {
        spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format),
    };
    const int result = pw_stream_connect(
        stream_, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_MAP_BUFFERS |
                                     PW_STREAM_FLAG_RT_PROCESS),
        parameters, 1);
    if (result < 0) throw std::runtime_error("connect PipeWire source failed");
    thread_ = std::thread([this] { pw_main_loop_run(loop_); });
}

void PipeWireSource::stop()
{
    if (loop_ == nullptr) return;
    pw_main_loop_quit(loop_);
    if (thread_.joinable()) thread_.join();
    if (stream_ != nullptr) pw_stream_destroy(stream_);
    pw_main_loop_destroy(loop_);
    stream_ = nullptr;
    loop_ = nullptr;
    ready_.store(false);
    pw_deinit();
}

} // namespace flex1500::client
