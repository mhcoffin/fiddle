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
                const juce::MidiMessage &message, uint64_t tag = 0) noexcept;

  /// A new non-zero tag for schedule(), unique to this scheduler.
  uint64_t nextTag() noexcept { return nextTag_.fetch_add(1, std::memory_order_relaxed); }

  /// Withdraws a pending message scheduled with @p tag. A message that has
  /// already been rendered is unaffected. Call from the scheduling thread.
  bool cancel(uint64_t tag) noexcept;
  void requestClear() noexcept;
  void requestPanic() noexcept;

  /// Appends all events due in this audio block to @p destination. Future
  /// events remain pending. Clear and panic discard only messages submitted
  /// before the request, allowing transport-stop release messages queued
  /// immediately afterward to survive. Panic emits the three standard reset
  /// messages on all 16 channels and takes precedence over a simultaneous
  /// clear request.
  void renderBlock(double blockStartMs, double sampleRate, int numSamples,
                   juce::MidiBuffer &destination) noexcept;

  [[nodiscard]] uint64_t droppedMessageCount() const noexcept;

private:
  RealtimeMpscQueue<RealtimeMidiMessage, kCapacity> incoming_;
  static constexpr size_t kCancelCapacity = 1024;
  RealtimeMpscQueue<uint64_t, kCancelCapacity> cancels_;
  std::atomic<uint64_t> nextTag_{1};
  std::array<uint64_t, kCancelCapacity> cancelled_{}; // audio thread only
  std::array<RealtimeMidiMessage, kCapacity> pending_{};
  size_t pendingCount_ = 0; // audio thread only
  std::atomic<uint64_t> droppedMessages_{0};
  std::atomic<uint64_t> currentGeneration_{0};
  std::atomic<bool> clearRequested_{false};
  std::atomic<bool> panicRequested_{false};
};

} // namespace fiddle
