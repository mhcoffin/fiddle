#pragma once

#include "../RealtimeMpscQueue.h"
#include "RealtimeMidiMessage.h"
#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace fiddle {

/// Bounded, allocation-free handoff from MIDI producer threads to the audio
/// thread. Only renderBlock() may be called by the audio thread.
class RealtimeMidiScheduler {
public:
  static constexpr size_t kCapacity = 8192;

  bool schedule(double triggerTimeMs,
                const juce::MidiMessage &message) noexcept;
  void requestClear() noexcept;
  void requestPanic() noexcept;

  /// Appends all events due in this audio block to @p destination. Future
  /// events remain pending. Panic emits the three standard reset messages on
  /// all 16 channels and takes precedence over a simultaneous clear request.
  void renderBlock(double blockStartMs, double sampleRate, int numSamples,
                   juce::MidiBuffer &destination) noexcept;

  [[nodiscard]] uint64_t droppedMessageCount() const noexcept;

private:
  RealtimeMpscQueue<RealtimeMidiMessage, kCapacity> incoming_;
  std::array<RealtimeMidiMessage, kCapacity> pending_{};
  size_t pendingCount_ = 0; // audio thread only
  std::atomic<uint64_t> droppedMessages_{0};
  std::atomic<bool> clearRequested_{false};
  std::atomic<bool> panicRequested_{false};
};

} // namespace fiddle
