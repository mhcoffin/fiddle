#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace fiddle {

// Preallocated per-edge delay. A graph owns references to these objects so
// unrelated routing/level edits do not flush audio already in flight. Only
// the render coordinator touches the samples/cursor; no per-block allocation.
class RouteDelay {
public:
  explicit RouteDelay(int samples) : samples_(samples), history_(2, samples) {
    history_.clear();
  }
  int samples() const noexcept { return samples_; }
  void addTo(juce::AudioBuffer<float> &destination,
             const juce::AudioBuffer<float> &source, int frames, float gain) {
    if (samples_ == 0) {
      for (int ch = 0; ch < std::min(2, destination.getNumChannels()); ++ch)
        destination.addFrom(ch, 0, source, ch, 0, frames, gain);
      return;
    }
    // Copy/add contiguous spans so JUCE's vectorized buffer operations handle
    // the samples, including delays shorter than a block and ring wraparound.
    for (int offset = 0; offset < frames;) {
      const int count = std::min(frames - offset, samples_ - cursor_);
      for (int ch = 0; ch < 2; ++ch) {
        if (ch < destination.getNumChannels())
          destination.addFrom(ch, offset, history_, ch, cursor_, count, gain);
        history_.copyFrom(ch, cursor_, source, ch, offset, count);
      }
      offset += count;
      cursor_ = (cursor_ + count) % samples_;
    }
  }
private:
  const int samples_;
  juce::AudioBuffer<float> history_;
  int cursor_ = 0;
};

} // namespace fiddle
