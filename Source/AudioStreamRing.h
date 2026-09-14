#pragma once

#include "StereoBufferOps.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <chrono>
#if defined(__APPLE__)
#include <mach/mach_time.h>
#endif

namespace fiddle {
// Shared by the native plugin and server. No JUCE dependency, pointers, locks,
// process-local handles, or dynamic storage in the shared layout.
inline double audioStreamTimeMs() noexcept {
#if defined(__APPLE__)
  static const auto scale = [] {
    mach_timebase_info_data_t info{};
    mach_timebase_info(&info);
    return double(info.numer) / double(info.denom) / 1000000.0;
  }();
  return double(mach_absolute_time()) * scale;
#else
  return std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
#endif
}

struct AudioStreamRing {
  static constexpr uint64_t magicValue = 0xF1DD1E00A0D20000;
  static constexpr uint32_t version = 2;
  static constexpr uint64_t capacity = 16384;
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  static_assert(std::atomic<uint32_t>::is_always_lock_free);

  std::atomic<uint64_t> magic{0};
  uint64_t streamId = 0;
  double sampleRate = 0;
  uint32_t blockSize = 0, targetFrames = 0;
  std::atomic<uint32_t> active{0};
  std::atomic<uint64_t> writeFrame{0}; // producer-owned; publication after copying
  std::atomic<uint64_t> readFrame{0};  // consumer-owned; advances even through silence
  // Consumer clock snapshot. Odd sequence while updating; never waited on.
  std::atomic<uint64_t> clockSequence{0}, clockFrame{0}, clockTimeUs{0};
  float samples[capacity * 2]{};

  struct Clock { uint64_t frame = 0; double timeMs = 0; };
  bool readClock(Clock &clock) const noexcept {
    const auto before = clockSequence.load(std::memory_order_seq_cst);
    if (before == 0 || (before & 1)) return false;
    clock.frame = clockFrame.load(std::memory_order_seq_cst);
    clock.timeMs = double(clockTimeUs.load(std::memory_order_seq_cst)) / 1000.0;
    return clockSequence.load(std::memory_order_seq_cst) == before;
  }
  void publishClock(uint64_t frame, double timeMs) noexcept {
    clockSequence.fetch_add(1, std::memory_order_seq_cst);
    clockFrame.store(frame, std::memory_order_seq_cst);
    clockTimeUs.store(static_cast<uint64_t>(timeMs * 1000.0), std::memory_order_seq_cst);
    clockSequence.fetch_add(1, std::memory_order_seq_cst);
  }
  static uint32_t reserveFrames(double rate, uint32_t block) noexcept {
    if (!(rate > 0) || block == 0 || block > capacity / 4) return 0;
    // Approximately 60–90 ms at normal settings, at least two complete blocks.
    const auto blocks = std::max(2.0, std::ceil(0.060 * rate / block));
    return std::min(uint32_t(capacity / 2 / block), uint32_t(blocks)) * block;
  }

  enum class PullResult { audio, underrun, unavailable };
  PullResult pull(float *const *output, int channels, int frames, double hostRate,
                  double hostTimeMs) noexcept {
    if (frames <= 0) return PullResult::audio;
    const auto silence = [&] {
      for (int ch = 0; ch < channels; ++ch)
        if (output[ch]) std::memset(output[ch], 0, size_t(frames) * sizeof(float));
    };
    if (magic.load(std::memory_order_acquire) != magicValue ||
        !active.load(std::memory_order_acquire) ||
        (hostRate > 0 && std::abs(hostRate - sampleRate) > 1.0) ||
        uint64_t(frames) > capacity) {
      silence(); return PullResult::unavailable;
    }
    const auto read = readFrame.load(std::memory_order_relaxed);
    publishClock(read, hostTimeMs);
    const auto written = writeFrame.load(std::memory_order_acquire);
    // A late producer may be behind readFrame. Never interpret that as unsigned
    // "lots of available audio", and never replay audio missed by the host.
    const bool enough = written >= read + uint64_t(frames) && written - read <= capacity;
    if (!enough) silence();
    else {
      const auto offset = read % capacity;
      const auto first = std::min(uint64_t(frames), capacity - offset);
      const auto remaining = uint64_t(frames) - first;
      auto *left = channels > 0 ? output[0] : nullptr;
      auto *right = channels > 1 ? output[1] : nullptr;
      // Preserve the original channel-order semantics even if a host supplies
      // aliased output planes. The usual disjoint stereo case reads each span
      // once, extracting both channels together.
      const auto l = reinterpret_cast<uintptr_t>(left), r = reinterpret_cast<uintptr_t>(right);
      const bool overlapping = left && right &&
          (l < r ? r - l : l - r) < size_t(frames) * sizeof(float);
      const auto copy = [&](float *a, float *b) {
        stereo_buffer::deinterleave(a, b, samples + offset * 2, first);
        if (remaining)
          stereo_buffer::deinterleave(a ? a + first : nullptr, b ? b + first : nullptr,
                                      samples, remaining);
      };
      if (overlapping) { copy(left, nullptr); copy(nullptr, right); }
      else copy(left, right);
      for (int ch = 2; ch < channels; ++ch) {
        if (output[ch]) std::memset(output[ch], 0, size_t(frames) * sizeof(float));
      }
    }
    readFrame.store(read + uint64_t(frames), std::memory_order_release);
    return enough ? PullResult::audio : PullResult::underrun;
  }

  // The worker renders into private scratch storage before publishing. The
  // consumer frees storage only after its copy, so ring access never overlaps.
  bool push(uint64_t start, const float *left, const float *right, uint32_t frames) noexcept {
    const auto read = readFrame.load(std::memory_order_acquire);
    if (frames == 0 || frames > capacity || start + frames > read + capacity) return false;
    const auto offset = start % capacity;
    const auto first = std::min(uint64_t(frames), capacity - offset);
    stereo_buffer::interleave(samples + offset * 2, left, right, first);
    if (first < frames)
      stereo_buffer::interleave(samples, left + first, right + first, frames - first);
    writeFrame.store(start + frames, std::memory_order_release);
    return true;
  }
};

// Pure scheduling policy, exercised with virtual host time in tests.
struct RenderAheadPlan {
  uint64_t startFrame = 0, skippedFrames = 0;
  double presentationTimeMs = 0;
  bool render = false;
  static RenderAheadPlan next(const AudioStreamRing &ring) noexcept {
    // No state is derived from the worker's wake-up time.
    AudioStreamRing::Clock clock;
    if (!ring.readClock(clock)) return {};
    const auto read = ring.readFrame.load(std::memory_order_acquire);
    const auto write = ring.writeFrame.load(std::memory_order_relaxed);
    const auto start = std::max(read, write);
    if (start + ring.blockSize > read + ring.targetFrames || start + ring.blockSize > read + AudioStreamRing::capacity)
      return {};
    return {start, start - write,
            clock.timeMs + (double(start) - double(clock.frame)) * 1000.0 / ring.sampleRate, true};
  }
};
} // namespace fiddle
