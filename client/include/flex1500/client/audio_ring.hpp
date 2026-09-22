// SPDX-License-Identifier: GPL-3.0-only

#ifndef FLEX1500_CLIENT_AUDIO_RING_HPP
#define FLEX1500_CLIENT_AUDIO_RING_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace flex1500::client {

class AudioRing {
public:
    explicit AudioRing(std::size_t capacity);

    std::size_t push(const float *samples, std::size_t count);
    std::size_t pop(float *samples, std::size_t count);
    std::size_t capacity() const { return samples_.size(); }
    std::uint64_t dropped() const { return dropped_.load(); }
    std::uint64_t underruns() const { return underruns_.load(); }

private:
    std::vector<float> samples_;
    std::atomic<std::size_t> read_{0};
    std::atomic<std::size_t> write_{0};
    std::atomic<std::uint64_t> dropped_{0};
    std::atomic<std::uint64_t> underruns_{0};
};

} // namespace flex1500::client

#endif
