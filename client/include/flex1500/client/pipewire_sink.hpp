// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_PIPEWIRE_SINK_HPP
#define FLEX1500_CLIENT_PIPEWIRE_SINK_HPP

#include "flex1500/client/audio_ring.hpp"
#include <thread>
#include <pipewire/stream.h>

struct pw_main_loop;

namespace flex1500::client {

class PipeWireSink {
public:
    explicit PipeWireSink(AudioRing &audio);
    ~PipeWireSink();
    PipeWireSink(const PipeWireSink &) = delete;
    PipeWireSink &operator=(const PipeWireSink &) = delete;
    void start();
    void stop() noexcept;

private:
    static void process(void *data);
    AudioRing &audio_;
    pw_main_loop *loop_ = nullptr;
    pw_stream *stream_ = nullptr;
    std::thread thread_;
};

} // namespace flex1500::client

#endif
