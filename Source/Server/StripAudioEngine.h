#pragma once

#include <atomic>
#include <memory>

#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

/// Owns the independent post-instrument audio path for one mixer strip.
///
/// Phase 4A starts with the existing strip fader as the only processing node.
/// Ordered insert nodes can be added between the graph input and fader without
/// changing MixerStrip's render contract or the shared Master path.
class StripAudioEngine {
public:
  StripAudioEngine();
  ~StripAudioEngine();

  StripAudioEngine(const StripAudioEngine &) = delete;
  StripAudioEngine &operator=(const StripAudioEngine &) = delete;

  void prepareToPlay(double sampleRate, int blockSize);
  void releaseResources();

  /// Process one strip's stereo output in place. effectiveGain already
  /// includes its fader and the current active/mute/solo decision.
  void processBlock(juce::AudioBuffer<float> &audio, float effectiveGain);

  [[nodiscard]] int latencySamples() const noexcept;
  [[nodiscard]] double latencyMs() const noexcept;

private:
  void rebuildGraph();

  juce::AudioProcessorGraph graph_;
  std::shared_ptr<std::atomic<float>> gainLinear_;
  std::atomic<int> latencySamples_{0};
  std::atomic<bool> prepared_{false};
  double sampleRate_ = 44100.0;
  int blockSize_ = 512;
  juce::MidiBuffer midiScratch_;
};

} // namespace fiddle
