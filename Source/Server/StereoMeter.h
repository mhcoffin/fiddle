#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>

namespace fiddle {
// One render-thread writer; message-thread readers/reset. No locks or allocation.
class StereoMeter {
public:
  void process(const juce::AudioBuffer<float> *audio, int frames, double rate) noexcept {
    const float decay = static_cast<float>(20.0 * frames / rate);
    const float holdDecay = decay * 0.15f;
    unsigned clipped = 0;
    for (int ch = 0; ch < 2; ++ch) {
      const float amplitude = audio && ch < audio->getNumChannels() ? audio->getMagnitude(ch, 0, frames) : 0;
      if (amplitude >= 1.0f || !std::isfinite(amplitude)) clipped |= 1u << ch;
      const float db = std::isfinite(amplitude) ? juce::Decibels::gainToDecibels(amplitude, -120.0f) : 12.0f;
      peak_[ch].store(juce::jmax(db, peak_[ch].load(std::memory_order_relaxed) - decay), std::memory_order_relaxed);
      hold_[ch].store(juce::jmax(db, hold_[ch].load(std::memory_order_relaxed) - holdDecay), std::memory_order_relaxed);
    }
    if (clipped) clips_.fetch_or(clipped, std::memory_order_relaxed);
  }
  float peak(int ch) const noexcept { return peak_[ch].load(std::memory_order_relaxed); }
  float hold(int ch) const noexcept { return hold_[ch].load(std::memory_order_relaxed); }
  float peak() const noexcept { return juce::jmax(peak(0), peak(1)); }
  float hold() const noexcept { return juce::jmax(hold(0), hold(1)); }
  unsigned clips() const noexcept { return clips_.load(std::memory_order_relaxed); }
  void resetClips() noexcept { clips_.store(0, std::memory_order_relaxed); }
  juce::var toJson() const {
    const auto flags = clips();
    return juce::var(juce::Array<juce::var>{peak(0), hold(0), peak(1), hold(1), bool(flags & 1), bool(flags & 2)});
  }
private:
  std::array<std::atomic<float>, 2> peak_{{-120.0f, -120.0f}}, hold_{{-120.0f, -120.0f}};
  std::atomic<unsigned> clips_{0};
};
} // namespace fiddle
