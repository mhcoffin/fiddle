#pragma once

#include <juce_core/juce_core.h>
#include <deque>
#include <mutex>

namespace fiddle {
// Best-effort diagnostic display only, never MIDI routing or capture. Producers
// enqueue directly rather than posting one message-thread callback per event.
// The message thread takes at most one bounded batch per timer interval, and
// waits for WebKit's completion before sending another.
class DebugUiFeed {
public:
  static constexpr size_t capacity = 512, batchSize = 128;
  static bool accepts(const juce::String &type) {
    return type == "pushMidiEvent" || type == "updateNoteState" ||
           type == "addLogMessage" || type == "metronomeBeat" || type == "setTempo";
  }

  void setActive(bool active) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (active_ == active) return;
    active_ = active;
    pending_.clear();
    dropped_ = 0;
    reset_ = true;
    if (active_ && !lastTempo_.isVoid()) pending_.push_back(lastTempo_);
    // Do not release an outstanding WebKit call when hiding the window.
  }

  void push(const juce::String &type, const juce::var &data) {
    if (!accepts(type)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ && type != "setTempo") return;
    auto *message = new juce::DynamicObject();
    message->setProperty("type", type);
    message->setProperty("data", data);
    juce::var value(message);
    if (type == "setTempo") lastTempo_ = value;
    if (!active_) return;
    if (pending_.size() == capacity) {
      pending_.pop_front();
      ++dropped_;
    }
    pending_.push_back(std::move(value));
  }

  juce::var takeBatch() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!active_ || inFlight_ || (pending_.empty() && !reset_)) return {};
    // Prefer current data to replaying a backlog. Missing note transitions
    // reset the live view, so a dropped note-off cannot leave stuck notes.
    while (pending_.size() > batchSize) {
      pending_.pop_front();
      ++dropped_;
    }
    juce::Array<juce::var> messages;
    for (auto &message : pending_) messages.add(std::move(message));
    pending_.clear();
    auto *batch = new juce::DynamicObject();
    batch->setProperty("messages", juce::var(messages));
    batch->setProperty("reset", reset_);
    batch->setProperty("dropped", static_cast<juce::int64>(dropped_));
    dropped_ = 0;
    reset_ = false;
    inFlight_ = true;
    return juce::var(batch);
  }

  void complete(bool succeeded = true) {
    std::lock_guard<std::mutex> lock(mutex_);
    inFlight_ = false;
    if (!succeeded) reset_ = true;
  }

private:
  std::mutex mutex_;
  std::deque<juce::var> pending_;
  juce::var lastTempo_;
  uint64_t dropped_ = 0;
  bool active_ = false, reset_ = true, inFlight_ = false;
};
} // namespace fiddle
