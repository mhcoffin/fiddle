#include "AudioStreamRing.h"

#include <array>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__)); } while (false)
using Ring = fiddle::AudioStreamRing;

// Freeze the pre-SIMD shared layout. Existing server/plugin pairs must still
// agree even when only one of the binaries has been updated.
struct OriginalLayout {
  std::atomic<uint64_t> magic{0};
  uint64_t streamId = 0;
  double sampleRate = 0;
  uint32_t blockSize = 0, targetFrames = 0;
  std::atomic<uint32_t> active{0};
  std::atomic<uint64_t> writeFrame{0}, readFrame{0};
  std::atomic<uint64_t> clockSequence{0}, clockFrame{0}, clockTimeUs{0};
  float samples[Ring::capacity * 2]{};
};
static_assert(sizeof(Ring) == sizeof(OriginalLayout));
static_assert(alignof(Ring) == alignof(OriginalLayout));
static_assert(offsetof(Ring, samples) == offsetof(OriginalLayout, samples));
static_assert(offsetof(Ring, writeFrame) == offsetof(OriginalLayout, writeFrame));
static_assert(offsetof(Ring, clockSequence) == offsetof(OriginalLayout, clockSequence));
static_assert(Ring::version == 2 && Ring::magicValue == 0xF1DD1E00A0D20000);

constexpr uint32_t sentinel = 0x42abc123;
void setBits(float &dest, uint32_t bits) { std::memcpy(&dest, &bits, sizeof(bits)); }
uint32_t bits(float value) { uint32_t result; std::memcpy(&result, &value, sizeof(result)); return result; }
uint32_t pattern(std::size_t i) {
  constexpr uint32_t special[]{0, 0x80000000, 1, 0x80000001, 0x7f800000,
      0xff800000, 0x7fc12345, 0x7f812345, 0xffc67890, 0x3f800000};
  return i % 3 == 0 ? special[(i / 3) % 10] : uint32_t(i * 2654435761u);
}
void fill(std::vector<float> &v, uint32_t value = sentinel) {
  for (auto &sample : v) setBits(sample, value);
}
void prime(Ring &ring, uint64_t start) {
  ring.magic.store(Ring::magicValue); ring.active.store(1);
  ring.sampleRate = 48000; ring.readFrame.store(start); ring.writeFrame.store(start);
}
void checkGuard(const std::vector<float> &v, std::size_t begin, std::size_t count) {
  for (std::size_t i = 0; i < begin; ++i) CHECK(bits(v[i]) == sentinel);
  for (std::size_t i = begin + count; i < v.size(); ++i) CHECK(bits(v[i]) == sentinel);
}

void exactTransfers() {
  const std::size_t lengths[]{1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 31, 63, 64, 65,
      127, 255, 256, 511, 512, 513, 1023, 1024, 4096, Ring::capacity - 1, Ring::capacity};
  const uint64_t starts[]{0, 1, 2, 3, 4, Ring::capacity - 1, Ring::capacity - 2,
      Ring::capacity - 3, Ring::capacity - 4, Ring::capacity - 7,
      Ring::capacity * 9 + 3, (uint64_t(1) << 40) + Ring::capacity - 3};
  for (const auto n : lengths) for (const auto start : starts) for (std::size_t alignment = 0; alignment < 4; ++alignment) {
    auto ring = std::make_unique<Ring>();
    prime(*ring, start);
    for (auto &sample : ring->samples) setBits(sample, sentinel);
    std::vector<float> expected(Ring::capacity * 2); fill(expected);
    // Inputs end exactly at their allocation boundary, so ASan catches even
    // read-only overrun of the final vector. Outputs also have bitwise guards.
    const auto lOffset = alignment, rOffset = (alignment + 1) % 4;
    std::vector<float> left(n + lOffset), right(n + rOffset);
    for (std::size_t i = 0; i < n; ++i) {
      setBits(left[i + lOffset], pattern(i));
      setBits(right[i + rOffset], pattern(i + 29));
      const auto position = ((start + i) % Ring::capacity) * 2;
      setBits(expected[position], pattern(i));
      setBits(expected[position + 1], pattern(i + 29));
    }
    CHECK(ring->push(start, left.data() + lOffset, right.data() + rOffset, uint32_t(n)));
    CHECK(ring->writeFrame.load() == start + n);
    CHECK(std::memcmp(ring->samples, expected.data(), expected.size() * sizeof(float)) == 0);
    const auto aOffset = 4 + (alignment + 2) % 4, bOffset = 4 + (alignment + 3) % 4;
    std::vector<float> a(n + aOffset + 4), b(n + bOffset + 4); fill(a); fill(b);
    float *output[]{a.data() + aOffset, b.data() + bOffset};
    CHECK(ring->pull(output, 2, int(n), 48000, 1000) == Ring::PullResult::audio);
    CHECK(ring->readFrame.load() == start + n);
    CHECK(std::memcmp(output[0], left.data() + lOffset, n * sizeof(float)) == 0);
    CHECK(std::memcmp(output[1], right.data() + rOffset, n * sizeof(float)) == 0);
    checkGuard(a, aOffset, n); checkGuard(b, bOffset, n);
  }
}

void optionalAndAliasedChannels() {
  for (const int n : {1, 3, 4, 7, 16, 63, 65, 513}) for (const uint64_t start : {uint64_t(0), Ring::capacity - 3}) {
    auto ring = std::make_unique<Ring>(); prime(*ring, start);
    std::vector<float> left(n), right(n);
    for (int i = 0; i < n; ++i) { setBits(left[i], pattern(i)); setBits(right[i], pattern(i + 29)); }
    CHECK(ring->push(start, left.data(), right.data(), uint32_t(n)));
    for (int mode = 0; mode < 5; ++mode) {
      std::vector<float> a(n + 8), b(n + 8), extra(n + 8); fill(a); fill(b); fill(extra);
      float *output[]{mode == 1 || mode == 2 ? nullptr : a.data() + 3,
                      mode == 0 || mode == 2 ? nullptr : b.data() + 1, nullptr, extra.data() + 2};
      const int channels = mode == 3 ? 0 : mode == 0 ? 1 : 4;
      ring->readFrame.store(start);
      CHECK(ring->pull(output, channels, n, 48000, 1000) == Ring::PullResult::audio);
      for (int i = 0; i < n; ++i) {
        CHECK(bits(a[i + 3]) == (channels > 0 && output[0] ? pattern(i) : sentinel));
        CHECK(bits(b[i + 1]) == (channels > 1 && output[1] ? pattern(i + 29) : sentinel));
        CHECK(bits(extra[i + 2]) == (channels > 3 ? 0 : sentinel));
      }
      checkGuard(a, 3, n); checkGuard(b, 1, n); checkGuard(extra, 2, n);
    }
    // Original implementation completes left before right, including across
    // wraparound. Preserve that order for exact and partially aliased planes.
    for (const int displacement : {-5, -1, 0, 1, 5}) {
      std::vector<float> out(n + 24), expected(n + 24); fill(out); fill(expected);
      float *output[]{out.data() + 8, out.data() + 8 + displacement};
      std::memcpy(expected.data() + 8, left.data(), n * sizeof(float));
      std::memcpy(expected.data() + 8 + displacement, right.data(), n * sizeof(float));
      ring->readFrame.store(start);
      CHECK(ring->pull(output, 2, n, 48000, 1000) == Ring::PullResult::audio);
      CHECK(std::memcmp(out.data(), expected.data(), out.size() * sizeof(float)) == 0);
    }
  }
}

void rejectedAndSilentTransfers() {
  auto ring = std::make_unique<Ring>(); prime(*ring, 0);
  CHECK(!ring->push(0, nullptr, nullptr, 0));
  CHECK(!ring->push(0, nullptr, nullptr, uint32_t(Ring::capacity + 1)));
  CHECK(!ring->push(Ring::capacity, nullptr, nullptr, 1));
  CHECK(ring->writeFrame.load() == 0);
  CHECK(ring->pull(nullptr, 2, 0, 48000, 0) == Ring::PullResult::audio);
  CHECK(ring->pull(nullptr, 2, -1, 48000, 0) == Ring::PullResult::audio);
  CHECK(ring->readFrame.load() == 0);
  std::array<float, 9> a; a.fill(123); float *out[]{a.data() + 1, nullptr};
  CHECK(ring->pull(out, 2, 7, 48000, 1000) == Ring::PullResult::underrun);
  CHECK(ring->readFrame.load() == 7 && a.front() == 123 && a.back() == 123);
  for (int i = 1; i < 8; ++i) CHECK(bits(a[i]) == 0);
  a.fill(123);
  CHECK(ring->pull(out, 2, 7, 44100, 1000) == Ring::PullResult::unavailable);
  CHECK(ring->readFrame.load() == 7);
  for (int i = 1; i < 8; ++i) CHECK(bits(a[i]) == 0);
  // Source planes may be the same (mono duplicated to stereo).
  std::array<float, 7> mono{}; mono.fill(42);
  CHECK(ring->push(7, mono.data(), mono.data(), 7));
  CHECK(ring->pull(out, 2, 7, 48000, 1000) == Ring::PullResult::audio);
  for (int i = 1; i < 8; ++i) CHECK(a[i] == 42);
}

int main() {
  try {
    exactTransfers(); optionalAndAliasedChannels(); rejectedAndSilentTransfers();
    std::cout << "Stereo transfers (" << fiddle::stereo_buffer::implementation()
              << "): bit-exact, unaligned, tails, wraparound, null/aliased channels and ABI passed\n";
    return 0;
  } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
