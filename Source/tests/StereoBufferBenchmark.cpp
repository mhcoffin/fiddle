// Opt-in microbenchmark, not a timing-gated regression test. Compare the
// production ring against its pre-SIMD transfer loops, with the same atomic
// publication/clock work. No audio device, mmap file, or installed plugin.
#include "AudioStreamRing.h"

#include <array>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

using Ring = fiddle::AudioStreamRing;
struct Buffers { const float *left, *right; float *a, *b; };
using Transfer = bool (*)(Ring &, const Buffers &, uint64_t, uint32_t);
#if defined(_MSC_VER)
 #define NOINLINE __declspec(noinline)
#else
 #define NOINLINE __attribute__((noinline))
#endif

NOINLINE bool oldPush(Ring &ring, const Buffers &b, uint64_t start, uint32_t frames) {
  const auto read = ring.readFrame.load(std::memory_order_acquire);
  if (frames == 0 || frames > Ring::capacity || start + frames > read + Ring::capacity) return false;
  for (uint32_t i = 0; i < frames; ++i) {
    const auto offset = ((start + i) % Ring::capacity) * 2;
    ring.samples[offset] = b.left[i]; ring.samples[offset + 1] = b.right[i];
  }
  ring.writeFrame.store(start + frames, std::memory_order_release);
  return true;
}

NOINLINE bool oldPull(Ring &ring, const Buffers &b, uint64_t, uint32_t frames) {
  float *output[]{b.a, b.b};
  const auto silence = [&] {
    for (int ch = 0; ch < 2; ++ch)
      if (output[ch]) std::memset(output[ch], 0, size_t(frames) * sizeof(float));
  };
  if (ring.magic.load(std::memory_order_acquire) != Ring::magicValue ||
      !ring.active.load(std::memory_order_acquire) ||
      std::abs(48000 - ring.sampleRate) > 1.0 || frames > Ring::capacity) {
    silence(); return false;
  }
  const auto read = ring.readFrame.load(std::memory_order_relaxed);
  ring.publishClock(read, 1000);
  const auto written = ring.writeFrame.load(std::memory_order_acquire);
  const bool enough = written >= read + frames && written - read <= Ring::capacity;
  if (!enough) silence();
  else for (int ch = 0; ch < 2; ++ch) {
    if (!output[ch]) continue;
    for (uint32_t i = 0; i < frames; ++i)
      output[ch][i] = ring.samples[((read + i) % Ring::capacity) * 2 + uint64_t(ch)];
  }
  ring.readFrame.store(read + frames, std::memory_order_release);
  return enough;
}

NOINLINE bool newPush(Ring &ring, const Buffers &b, uint64_t start, uint32_t frames) {
  return ring.push(start, b.left, b.right, frames);
}
NOINLINE bool newPull(Ring &ring, const Buffers &b, uint64_t, uint32_t frames) {
  float *output[]{b.a, b.b};
  return ring.pull(output, 2, int(frames), 48000, 1000) == Ring::PullResult::audio;
}

// Keep both the transfer calls and their resulting bytes observable. A
// noinline function-pointer harness prevents constant-folding the whole run.
volatile float checksum = 0;
NOINLINE double measure(Transfer transfer, Ring &ring, const Buffers &buffers,
                         uint64_t offset, uint32_t frames, int iterations) {
  const auto begin = std::chrono::steady_clock::now();
  bool success = true;
  for (int i = 0; i < iterations; ++i) {
    const auto start = offset + uint64_t(i) * Ring::capacity;
    ring.readFrame.store(start, std::memory_order_relaxed);
    ring.writeFrame.store(start + frames, std::memory_order_relaxed);
    success = transfer(ring, buffers, start, frames) && success;
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
  const auto elapsed = std::chrono::steady_clock::now() - begin;
  if (!success) throw std::runtime_error("benchmark transfer failed");
  checksum = checksum + ring.samples[offset * 2] + buffers.a[frames - 1] + buffers.b[frames - 1];
  return std::chrono::duration<double, std::nano>(elapsed).count() / iterations;
}

int main() {
  auto ring = std::make_unique<Ring>();
  ring->magic.store(Ring::magicValue); ring->active.store(1); ring->sampleRate = 48000;
  std::vector<float> left(1032), right(1032), a(1032), b(1032);
  for (std::size_t i = 0; i < left.size(); ++i) { left[i] = float(i) * 0.001f; right[i] = -float(i) * 0.002f; }
  // Intentionally not SIMD-aligned, as host buffers need not be aligned.
  const Buffers buffers{left.data() + 1, right.data() + 2, a.data() + 3, b.data() + 1};
  const std::array<Transfer, 4> functions{oldPush, newPush, oldPull, newPull};
  std::cout << "Backend: " << fiddle::stereo_buffer::implementation()
            << "; median of 7 runs; ns/block; warm buffers; includes ring atomics\n"
            << "frames,wrap,old_push,new_push,push_speedup,old_pull,new_pull,pull_speedup\n";
  for (const uint32_t frames : {16, 64, 128, 256, 511, 512, 1024}) for (const bool wrap : {false, true}) {
    const uint64_t offset = wrap ? Ring::capacity - 3 : 0;
    const int iterations = std::max(20000, int(2000000 / frames));
    std::array<std::array<double, 7>, 4> timings{};
    // Seed and check outputs before measuring a pull.
    ring->readFrame.store(offset);
    if (!newPush(*ring, buffers, offset, frames) || !newPull(*ring, buffers, offset, frames) ||
        std::memcmp(buffers.left, buffers.a, frames * sizeof(float)) != 0 ||
        std::memcmp(buffers.right, buffers.b, frames * sizeof(float)) != 0)
      throw std::runtime_error("benchmark correctness check failed");
    for (const auto fn : functions) measure(fn, *ring, buffers, offset, frames, 1000);
    for (int trial = 0; trial < 7; ++trial) {
      for (int index = 0; index < 4; ++index) {
        const int which = trial % 2 ? 3 - index : index;
        timings[which][trial] = measure(functions[which], *ring, buffers, offset, frames, iterations);
      }
    }
    for (auto &timing : timings) std::sort(timing.begin(), timing.end());
    const auto op = timings[0][3], np = timings[1][3], og = timings[2][3], ng = timings[3][3];
    std::cout << frames << ',' << wrap << ',' << std::fixed << std::setprecision(2)
              << op << ',' << np << ',' << op / np << ',' << og << ',' << ng << ',' << og / ng << '\n';
  }
}
