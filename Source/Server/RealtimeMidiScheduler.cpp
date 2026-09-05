#include "RealtimeMidiScheduler.h"

#include <algorithm>

namespace fiddle {

bool RealtimeMidiScheduler::schedule(
    double triggerTimeMs, const juce::MidiMessage &message) noexcept {
  const auto size = message.getRawDataSize();
  if (size <= 0 || size > 3) {
    droppedMessages_.fetch_add(1, std::memory_order_relaxed);
    return false;
  }

  RealtimeMidiMessage queued;
  queued.triggerTimeMs = triggerTimeMs;
  queued.generation = currentGeneration_.load(std::memory_order_acquire);
  queued.size = static_cast<uint8_t>(size);
  std::copy_n(message.getRawData(), size, queued.data);
  if (incoming_.tryPush(queued))
    return true;

  droppedMessages_.fetch_add(1, std::memory_order_relaxed);
  return false;
}

void RealtimeMidiScheduler::requestClear() noexcept {
  currentGeneration_.fetch_add(1, std::memory_order_acq_rel);
  clearRequested_.store(true, std::memory_order_release);
}

void RealtimeMidiScheduler::requestPanic() noexcept {
  currentGeneration_.fetch_add(1, std::memory_order_acq_rel);
  panicRequested_.store(true, std::memory_order_release);
}

void RealtimeMidiScheduler::renderBlock(
    double blockStartMs, double sampleRate, int numSamples,
    juce::MidiBuffer &destination) noexcept {
  RealtimeMidiMessage queued;
  while (incoming_.tryPop(queued)) {
    if (pendingCount_ < pending_.size())
      pending_[pendingCount_++] = queued;
    else
      droppedMessages_.fetch_add(1, std::memory_order_relaxed);
  }

  const bool panic = panicRequested_.exchange(false, std::memory_order_acquire);
  const bool clear = clearRequested_.exchange(false, std::memory_order_acquire);
  if (panic || clear) {
    const auto keepGeneration =
        currentGeneration_.load(std::memory_order_acquire);
    size_t retained = 0;
    for (size_t i = 0; i < pendingCount_; ++i) {
      if (pending_[i].generation >= keepGeneration)
        pending_[retained++] = pending_[i];
    }
    pendingCount_ = retained;
  }

  if (panic) {
    for (int channel = 1; channel <= 16; ++channel) {
      destination.addEvent(juce::MidiMessage::allSoundOff(channel), 0);
      destination.addEvent(juce::MidiMessage::allNotesOff(channel), 0);
      destination.addEvent(juce::MidiMessage::allControllersOff(channel), 0);
    }
  }

  if (sampleRate <= 0.0 || numSamples <= 0)
    return;

  const double blockEndMs = blockStartMs + (1000.0 * numSamples / sampleRate);
  size_t retained = 0;
  for (size_t i = 0; i < pendingCount_; ++i) {
    const auto &event = pending_[i];
    if (event.triggerTimeMs <= blockEndMs) {
      const auto offset = static_cast<int>(
          std::clamp((event.triggerTimeMs - blockStartMs) * sampleRate / 1000.0,
                     0.0, static_cast<double>(juce::jmax(0, numSamples - 1))));
      destination.addEvent(event.data, event.size, offset);
    } else {
      pending_[retained++] = event;
    }
  }
  pendingCount_ = retained;
}

uint64_t RealtimeMidiScheduler::droppedMessageCount() const noexcept {
  return droppedMessages_.load(std::memory_order_relaxed);
}

} // namespace fiddle
