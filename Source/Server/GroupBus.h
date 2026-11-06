#pragma once

#include "AudioSend.h"
#include "StripAudioEngine.h"
#include "StereoMeter.h"

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace fiddle {

/// A stereo submix: an audio bus (the Audio Buses bank) or a chair bus.
///
/// Every chair owns a chair bus that all its layers feed (docs/chair-buses.md).
/// A chair bus has an output (Master or an audio bus) and sends to audio
/// buses; an audio bus always feeds Master.
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

  /// Chair buses only: the owning chair's ID (or "strip:<id>" for a layer
  /// without a chair), its output (empty = Master) and its sends. Message
  /// thread state; the renderer uses the published graph.
  juce::String chairKey;
  juce::String directOutputBusId;
  std::vector<AudioSend> sends;
  [[nodiscard]] bool isChairBus() const noexcept { return chairKey.isNotEmpty(); }
  /// Chair faders top out at +6 dB like layer faders; audio buses at +12 dB.
  [[nodiscard]] float maximumGainDb() const noexcept { return isChairBus() ? 6.0f : 12.0f; }
  /// True when nothing about the bus differs from a new chair bus.
  [[nodiscard]] bool hasDefaultChairSettings() const;

  [[nodiscard]] float gainDb() const noexcept;
  void setGainDb(float gain) noexcept;
  [[nodiscard]] bool isMuted() const noexcept;
  void setMuted(bool muted) noexcept;
  [[nodiscard]] bool isSoloed() const noexcept;
  void setSoloed(bool soloed) noexcept;
  [[nodiscard]] float peakDb() const noexcept;
  [[nodiscard]] float peakHoldDb() const noexcept;
  StereoMeter &meter() noexcept { return meter_; }
  const StereoMeter &meter() const noexcept { return meter_; }

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
  void processTo(juce::AudioBuffer<float> &destination, bool audible,
                 juce::AudioBuffer<float> *printOutput = nullptr) noexcept;

  /// Process the summed input in place (inserts, fader, balance, meter) and
  /// leave the result in inputBuffer() for the caller to route. The optional
  /// pre-fader tap is cleared, then filled after pre-fader inserts when the
  /// bus is audible. Returns whether the fader passes audio.
  bool processInPlace(int numSamples, bool audible,
                      juce::AudioBuffer<float> *preFaderTap = nullptr) noexcept;

  StripAudioEngine &audioEngine() noexcept { return audioEngine_; }
  const StripAudioEngine &audioEngine() const noexcept { return audioEngine_; }

  [[nodiscard]] juce::var toJson() const;

private:
  std::atomic<float> gainDb_{0.0f};
  std::atomic<bool> muted_{false};
  std::atomic<bool> soloed_{false};
  StereoMeter meter_;
  std::atomic<double> sampleRate_{44100.0};
  juce::AudioBuffer<float> inputBuffer_;
  juce::AudioBuffer<float> inputStorage_;
  StripAudioEngine audioEngine_;
};

} // namespace fiddle
