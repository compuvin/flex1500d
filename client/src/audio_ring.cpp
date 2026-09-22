// SPDX-License-Identifier: GPL-3.0-only

#include "flex1500/client/audio_ring.hpp"

#include <algorithm>

namespace flex1500::client {

AudioRing::AudioRing(std::size_t capacity) : samples_(capacity + 1)
{
}

std::size_t AudioRing::push(const float *samples, std::size_t count)
{
    const std::size_t read = read_.load(std::memory_order_acquire);
    std::size_t write = write_.load(std::memory_order_relaxed);
    std::size_t accepted = 0;
    while (accepted < count) {
        const std::size_t next = (write + 1) % samples_.size();
        if (next == read) break;
        samples_[write] = samples[accepted++];
        write = next;
    }
    write_.store(write, std::memory_order_release);
    dropped_.fetch_add(count - accepted, std::memory_order_relaxed);
    return accepted;
}

std::size_t AudioRing::pop(float *samples, std::size_t count)
{
    std::size_t read = read_.load(std::memory_order_relaxed);
    const std::size_t write = write_.load(std::memory_order_acquire);
    std::size_t produced = 0;
    while (produced < count && read != write) {
        samples[produced++] = samples_[read];
        read = (read + 1) % samples_.size();
    }
    read_.store(read, std::memory_order_release);
    if (produced < count)
        underruns_.fetch_add(count - produced, std::memory_order_relaxed);
    return produced;
}

} // namespace flex1500::client
