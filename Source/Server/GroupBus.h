#pragma once

#include "StripAudioEngine.h"

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace fiddle {

/// A stereo submix destination for instrument-strip direct outputs.
///
/// GroupBus owns its effect rack and preallocated summing buffer. The message
/// thread changes identity and routing; the audio thread reads only atomics and
/// the published bus pointer.
class GroupBus {
public:
  GroupBus();
  ~GroupBus();

  GroupBus(const GroupBus &) = delete;
  GroupBus &operator=(const GroupBus &) = delete;

  juce::String id;
  juce::String name;

  [[nodiscard]] float gainDb() const noexcept;
  void setGainDb(float gain) noexcept;
  [[nodiscard]] bool isMuted() const noexcept;
  void setMuted(bool muted) noexcept;
  [[nodiscard]] bool isSoloed() const noexcept;
  void setSoloed(bool soloed) noexcept;
  [[nodiscard]] float peakDb() const noexcept;
  [[nodiscard]] float peakHoldDb() const noexcept;

  void prepareToPlay(double sampleRate, int blockSize);
  void prepareIfNeeded(double sampleRate, int blockSize) {
    if (sampleRate_.load() != sampleRate || inputStorage_.getNumSamples() != blockSize)
      prepareToPlay(sampleRate, blockSize);
  }

  /// Clear the preallocated input sum for a new device block.
  void beginBlock(int numSamples) noexcept;

  /// Destination used while rendering strips assigned to this bus.
  [[nodiscard]] juce::AudioBuffer<float> &inputBuffer() noexcept {
    return inputBuffer_;
  }

  /// Process the summed input through inserts and the bus fader, meter it, and
  /// add it to Master. A silent block is still processed so effect tails decay.
  void processTo(juce::AudioBuffer<float> &destination, bool audible) noexcept;

  StripAudioEngine &audioEngine() noexcept { return audioEngine_; }
  const StripAudioEngine &audioEngine() const noexcept { return audioEngine_; }

  [[nodiscard]] juce::var toJson() const;

private:
  std::atomic<float> gainDb_{0.0f};
  std::atomic<bool> muted_{false};
  std::atomic<bool> soloed_{false};
  std::atomic<float> peakDb_{-120.0f};
  std::atomic<float> peakHoldDb_{-120.0f};
  std::atomic<double> sampleRate_{44100.0};
  juce::AudioBuffer<float> inputBuffer_;
  juce::AudioBuffer<float> inputStorage_;
  StripAudioEngine audioEngine_;
};

} // namespace fiddle
