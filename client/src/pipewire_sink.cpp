// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/pipewire_sink.hpp"

#include <cstdint>
#include <stdexcept>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

namespace flex1500::client {

PipeWireSink::PipeWireSink(AudioRing &audio) : audio_(audio) {}
PipeWireSink::~PipeWireSink() { stop(); }

void PipeWireSink::process(void *data)
{
    auto *sink = static_cast<PipeWireSink *>(data);
    pw_buffer *pw_buffer = pw_stream_dequeue_buffer(sink->stream_);
    if (pw_buffer == nullptr) return;
    spa_buffer *buffer = pw_buffer->buffer;
    spa_data &plane = buffer->datas[0];
    if (plane.data != nullptr && plane.chunk != nullptr) {
        const auto *input = static_cast<const float *>(plane.data);
        const std::size_t offset = plane.chunk->offset / sizeof(float);
        const std::size_t count = plane.chunk->size / sizeof(float);
        sink->audio_.push(input + offset, count);
    }
    pw_stream_queue_buffer(sink->stream_, pw_buffer);
}

void PipeWireSink::start()
{
    if (loop_ != nullptr) return;
    pw_init(nullptr, nullptr);
    loop_ = pw_main_loop_new(nullptr);
    if (loop_ == nullptr) throw std::runtime_error("create PipeWire loop failed");
    static const pw_stream_events events = [] {
        pw_stream_events value{};
        value.version = PW_VERSION_STREAM_EVENTS;
        value.process = process;
        return value;
    }();
    stream_ = pw_stream_new_simple(
        pw_main_loop_get_loop(loop_), "FLEX-1500 TX",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio",
                          PW_KEY_MEDIA_CATEGORY, "Playback",
                          PW_KEY_MEDIA_CLASS, "Audio/Sink",
                          PW_KEY_NODE_NAME, "flex1500-client-tx",
                          PW_KEY_NODE_DESCRIPTION, "FLEX-1500 TX", nullptr),
        &events, this);
    if (stream_ == nullptr) throw std::runtime_error("create PipeWire sink failed");
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
        stream_, PW_DIRECTION_INPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT |
                                     PW_STREAM_FLAG_MAP_BUFFERS |
                                     PW_STREAM_FLAG_RT_PROCESS),
        parameters, 1);
    if (result < 0) throw std::runtime_error("connect PipeWire sink failed");
    thread_ = std::thread([this] { pw_main_loop_run(loop_); });
}

void PipeWireSink::stop() noexcept
{
    if (loop_ == nullptr) return;
    pw_main_loop_quit(loop_);
    if (thread_.joinable()) thread_.join();
    if (stream_ != nullptr) pw_stream_destroy(stream_);
    pw_main_loop_destroy(loop_);
    stream_ = nullptr;
    loop_ = nullptr;
    pw_deinit();
}

} // namespace flex1500::client

