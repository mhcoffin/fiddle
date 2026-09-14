#pragma once

#include <cstddef>

// This transfer code is shared with the JUCE-free native VST3. Use baseline
// ISA intrinsics, not optional AVX/CPU flags or a change to the mmap layout.
// The scalar build is also exercised by the test suite.
#if !defined(FIDDLE_DISABLE_AUDIO_SIMD) && (defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64))
 #include <arm_neon.h>
 #define FIDDLE_STEREO_NEON 1
#elif !defined(FIDDLE_DISABLE_AUDIO_SIMD) && (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
 #include <emmintrin.h>
 #define FIDDLE_STEREO_SSE2 1
#endif

namespace fiddle::stereo_buffer {

inline constexpr const char *implementation() noexcept {
#if defined(FIDDLE_STEREO_NEON)
  return "NEON";
#elif defined(FIDDLE_STEREO_SSE2)
  return "SSE2";
#else
  return "scalar";
#endif
}

// All pointers need only normal float alignment, not SIMD alignment. The
// source and destination ranges must not overlap. No padding is read/written:
// vectors handle complete frames, followed by a 0–3 frame tail. Eight-frame
// batches expose paired loads/stores and amortize loop overhead.
inline void interleave(float *dest, const float *left, const float *right,
                       std::size_t frames) noexcept {
  std::size_t i = 0;
#if defined(FIDDLE_STEREO_NEON)
  for (; i + 8 <= frames; i += 8) {
    const float32x4x2_t a{{vld1q_f32(left + i), vld1q_f32(right + i)}};
    const float32x4x2_t b{{vld1q_f32(left + i + 4), vld1q_f32(right + i + 4)}};
    vst2q_f32(dest + i * 2, a);
    vst2q_f32(dest + i * 2 + 8, b);
  }
  if (i + 4 <= frames) {
    const float32x4x2_t pair{{vld1q_f32(left + i), vld1q_f32(right + i)}};
    vst2q_f32(dest + i * 2, pair);
    i += 4;
  }
#elif defined(FIDDLE_STEREO_SSE2)
  for (; i + 8 <= frames; i += 8) {
    const auto l = _mm_loadu_ps(left + i), r = _mm_loadu_ps(right + i);
    const auto l2 = _mm_loadu_ps(left + i + 4), r2 = _mm_loadu_ps(right + i + 4);
    _mm_storeu_ps(dest + i * 2, _mm_unpacklo_ps(l, r));
    _mm_storeu_ps(dest + i * 2 + 4, _mm_unpackhi_ps(l, r));
    _mm_storeu_ps(dest + i * 2 + 8, _mm_unpacklo_ps(l2, r2));
    _mm_storeu_ps(dest + i * 2 + 12, _mm_unpackhi_ps(l2, r2));
  }
  if (i + 4 <= frames) {
    const auto l = _mm_loadu_ps(left + i), r = _mm_loadu_ps(right + i);
    _mm_storeu_ps(dest + i * 2, _mm_unpacklo_ps(l, r));
    _mm_storeu_ps(dest + i * 2 + 4, _mm_unpackhi_ps(l, r));
    i += 4;
  }
#endif
  for (; i < frames; ++i) {
    dest[i * 2] = left[i];
    dest[i * 2 + 1] = right[i];
  }
}

template <int channel>
inline void deinterleaveChannel(float *dest, const float *source,
                                std::size_t frames) noexcept {
  static_assert(channel == 0 || channel == 1);
  std::size_t i = 0;
#if defined(FIDDLE_STEREO_NEON)
  for (; i + 8 <= frames; i += 8) {
    const auto a = vld2q_f32(source + i * 2), b = vld2q_f32(source + i * 2 + 8);
    vst1q_f32(dest + i, a.val[channel]);
    vst1q_f32(dest + i + 4, b.val[channel]);
  }
  if (i + 4 <= frames) {
    vst1q_f32(dest + i, vld2q_f32(source + i * 2).val[channel]);
    i += 4;
  }
#elif defined(FIDDLE_STEREO_SSE2)
  for (; i + 8 <= frames; i += 8) {
    const auto a = _mm_loadu_ps(source + i * 2), b = _mm_loadu_ps(source + i * 2 + 4);
    const auto c = _mm_loadu_ps(source + i * 2 + 8), d = _mm_loadu_ps(source + i * 2 + 12);
    _mm_storeu_ps(dest + i, _mm_shuffle_ps(a, b,
        _MM_SHUFFLE(channel + 2, channel, channel + 2, channel)));
    _mm_storeu_ps(dest + i + 4, _mm_shuffle_ps(c, d,
        _MM_SHUFFLE(channel + 2, channel, channel + 2, channel)));
  }
  if (i + 4 <= frames) {
    const auto a = _mm_loadu_ps(source + i * 2), b = _mm_loadu_ps(source + i * 2 + 4);
    _mm_storeu_ps(dest + i, _mm_shuffle_ps(a, b,
        _MM_SHUFFLE(channel + 2, channel, channel + 2, channel)));
    i += 4;
  }
#endif
  for (; i < frames; ++i) dest[i] = source[i * 2 + channel];
}

// Output planes must be disjoint. Either plane may be null (mono/disabled
// channel); absent channels are not touched and need no temporary buffer.
inline void deinterleave(float *left, float *right, const float *source,
                         std::size_t frames) noexcept {
  if (!left) {
    if (right) deinterleaveChannel<1>(right, source, frames);
    return;
  }
  if (!right) { deinterleaveChannel<0>(left, source, frames); return; }
  std::size_t i = 0;
#if defined(FIDDLE_STEREO_NEON)
  for (; i + 8 <= frames; i += 8) {
    const auto a = vld2q_f32(source + i * 2), b = vld2q_f32(source + i * 2 + 8);
    vst1q_f32(left + i, a.val[0]);
    vst1q_f32(left + i + 4, b.val[0]);
    vst1q_f32(right + i, a.val[1]);
    vst1q_f32(right + i + 4, b.val[1]);
  }
  if (i + 4 <= frames) {
    const auto pair = vld2q_f32(source + i * 2);
    vst1q_f32(left + i, pair.val[0]);
    vst1q_f32(right + i, pair.val[1]);
    i += 4;
  }
#elif defined(FIDDLE_STEREO_SSE2)
  for (; i + 8 <= frames; i += 8) {
    const auto a = _mm_loadu_ps(source + i * 2), b = _mm_loadu_ps(source + i * 2 + 4);
    const auto c = _mm_loadu_ps(source + i * 2 + 8), d = _mm_loadu_ps(source + i * 2 + 12);
    _mm_storeu_ps(left + i, _mm_shuffle_ps(a, b, _MM_SHUFFLE(2, 0, 2, 0)));
    _mm_storeu_ps(left + i + 4, _mm_shuffle_ps(c, d, _MM_SHUFFLE(2, 0, 2, 0)));
    _mm_storeu_ps(right + i, _mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 1, 3, 1)));
    _mm_storeu_ps(right + i + 4, _mm_shuffle_ps(c, d, _MM_SHUFFLE(3, 1, 3, 1)));
  }
  if (i + 4 <= frames) {
    const auto a = _mm_loadu_ps(source + i * 2), b = _mm_loadu_ps(source + i * 2 + 4);
    _mm_storeu_ps(left + i, _mm_shuffle_ps(a, b, _MM_SHUFFLE(2, 0, 2, 0)));
    _mm_storeu_ps(right + i, _mm_shuffle_ps(a, b, _MM_SHUFFLE(3, 1, 3, 1)));
    i += 4;
  }
#endif
  for (; i < frames; ++i) {
    left[i] = source[i * 2];
    right[i] = source[i * 2 + 1];
  }
}

} // namespace fiddle::stereo_buffer

#undef FIDDLE_STEREO_NEON
#undef FIDDLE_STEREO_SSE2
