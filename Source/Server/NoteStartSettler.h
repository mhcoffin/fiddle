#pragma once

#include "DoricoPlaybackOptions.h"
#include "midi_event.pb.h"
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>

namespace fiddle {

/// Holds the start of a note briefly so that the dynamic Dorico sends just
/// after it counts as the note's starting dynamic. At a change of dynamic,
/// Dorico sends the new CC1 0-7 ms (host time) after the note-on it belongs
/// to (experiments/dynamics-curve/results-01.md); routed at once, the note
/// would take the previous dynamic. Only notes from a pinned bridge whose
/// dynamics are on CC1 are held. A held note keeps the trigger time it was
/// given on arrival, so its timing does not change.
class NoteStartSettler {
public:
  using Route = std::function<void(fiddle::Note &, double triggerTimeMs)>;
  static constexpr double kSettleMs = 10.0;

  explicit NoteStartSettler(Route route) : route_(std::move(route)) {}

  void setHostSampleRate(double sampleRate) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sampleRate > 0)
      sampleRate_ = sampleRate;
  }

  /// Route the note now, or hold it (see class comment). `triggerTimeMs` <= 0
  /// means "now" (stopped transport): never held.
  void noteStarted(fiddle::Note note, double triggerTimeMs, double receivedMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto dimension = note.notation_dimensions().find(kPinnedPlaybackDimension);
    const bool pinned = dimension != note.notation_dimensions().end() && dimension->second > 0.5f;
    if (!pinned || note.dynamics_mode() != fiddle::Note::CC || triggerTimeMs <= 0) {
      route_(note, triggerTimeMs);
      return;
    }
    held_.push_back({std::move(note), triggerTimeMs, receivedMs});
  }

  /// Call before routing a note's end: its start (and any held before it)
  /// goes first.
  void noteEnding(uint64_t noteId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < held_.size(); ++i)
      if (held_[i].note.id() == noteId) {
        releaseFront(i + 1);
        return;
      }
  }

  /// Every incoming event, with its host position: a CC1 within a held
  /// note's window becomes that note's starting dynamic; notes whose window
  /// has passed are routed.
  void observe(const fiddle::MidiEvent &event, uint64_t hostSamples) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto window = static_cast<uint64_t>(kSettleMs * sampleRate_ / 1000.0);
    if (event.has_cc() && event.cc().controller_number() == 1) {
      for (auto &h : held_) {
        auto &note = h.note;
        if (note.port() != event.port() || note.channel() != event.channel() ||
            hostSamples < note.start_sample() || hostSamples > note.start_sample() + window)
          continue;
        auto &lane = (*note.mutable_cc_automation())[1];
        lane.clear_points();
        auto *point = lane.add_points();
        point->set_offset_samples(0);
        point->set_value(event.cc().controller_value());
      }
    }
    size_t due = 0;
    while (due < held_.size() && held_[due].note.start_sample() + window < hostSamples)
      ++due;
    releaseFront(due);
  }

  /// Route notes held at least kSettleMs (wall clock): no later event came.
  void releaseOlderThan(double nowMs) {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t due = 0;
    while (due < held_.size() && nowMs - held_[due].receivedMs >= kSettleMs)
      ++due;
    releaseFront(due);
  }

  void releaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    releaseFront(held_.size());
  }

  size_t heldCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return held_.size();
  }

private:
  struct Held {
    fiddle::Note note;
    double triggerTimeMs, receivedMs;
  };

  // Routes in arrival order, under the lock, so a note's start always
  // precedes its end.
  void releaseFront(size_t count) {
    for (size_t i = 0; i < count && !held_.empty(); ++i) {
      auto held = std::move(held_.front());
      held_.pop_front();
      route_(held.note, held.triggerTimeMs);
    }
  }

  mutable std::mutex mutex_;
  std::deque<Held> held_;
  Route route_;
  double sampleRate_ = 44100.0;
};

} // namespace fiddle
