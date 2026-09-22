// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_PIPEWIRE_SOURCE_HPP
#define FLEX1500_CLIENT_PIPEWIRE_SOURCE_HPP

#include "flex1500/client/audio_ring.hpp"

#include <atomic>
#include <thread>

#include <pipewire/stream.h>

struct pw_main_loop;

namespace flex1500::client {

class PipeWireSource {
public:
    explicit PipeWireSource(AudioRing &audio);
    ~PipeWireSource();
    PipeWireSource(const PipeWireSource &) = delete;
    PipeWireSource &operator=(const PipeWireSource &) = delete;

    void start();
    void stop();

private:
    static void process(void *data);
    static void state_changed(void *data, pw_stream_state old_state,
                              pw_stream_state state, const char *error);

    AudioRing &audio_;
    pw_main_loop *loop_ = nullptr;
    pw_stream *stream_ = nullptr;
    std::thread thread_;
    std::atomic_bool ready_{false};
};

} // namespace flex1500::client

#endif
