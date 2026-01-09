#pragma once

#include "midi_event.pb.h"
#include <functional>
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>

namespace fiddle {

/**
 * Generates Subnotes from Notes.
 * Splits long notes into chunks of a specific duration.
 */
class SubnoteGenerator {
public:
  // Default subnote duration in seconds (configurable constant)
  static constexpr double DEFAULT_SUBNOTE_DURATION_SECONDS = 1.0;

  struct Callbacks {
    std::function<void(const fiddle::Subnote &)> onSubnoteGenerated;
  };

  SubnoteGenerator(double sampleRate = 44100.0)
      : sampleRate(sampleRate),
        subnoteDurationSamples(static_cast<uint64_t>(
            DEFAULT_SUBNOTE_DURATION_SECONDS * sampleRate)) {}

  void setCallbacks(Callbacks cbs) { callbacks = std::move(cbs); }

  void setSubnoteDuration(double seconds) {
    std::lock_guard<std::mutex> lock(mutex);
    subnoteDurationSamples = static_cast<uint64_t>(seconds * sampleRate);
  }

  void setSampleRate(double newRate) {
    std::lock_guard<std::mutex> lock(mutex);
    double durationSeconds = (double)subnoteDurationSamples / sampleRate;
    sampleRate = newRate;
    subnoteDurationSamples =
        static_cast<uint64_t>(durationSeconds * sampleRate);
  }

  /**
   * Called when a NEW note starts.
   */
  void onNoteStarted(const fiddle::Note &note) {
    std::lock_guard<std::mutex> lock(mutex);

    ActiveNoteState state;
    state.note = note;
    state.lastEmittedOffset = 0;

    // Emit the first subnote immediately
    emitSubnote(state, false);

    activeNotes[note.id()] = state;
  }

  /**
   * Called when a note ends.
   */
  void onNoteEnded(const fiddle::Note &note) {
    std::lock_guard<std::mutex> lock(mutex);

    auto it = activeNotes.find(note.id());
    if (it != activeNotes.end()) {
      it->second.note = note;        // Update with final duration
      emitSubnote(it->second, true); // Final subnote
      activeNotes.erase(it);
    }
  }

  /**
   * Updates the progress of time. This should be called regularly.
   */
  void tick(uint64_t currentSampleTime) {
    std::lock_guard<std::mutex> lock(mutex);

    for (auto &[id, state] : activeNotes) {
      uint64_t elapsed = currentSampleTime - state.note.start_sample();

      // While we have enough elapsed time to emit more subnotes
      while (elapsed >= state.lastEmittedOffset + subnoteDurationSamples) {
        emitSubnote(state, false);
        // Safety Break: Don't emit more than 100 subnotes per note per tick
        if (state.subnoteCount > 100) {
          state.lastEmittedOffset = elapsed;
          break;
        }
      }
    }
  }

private:
  struct ActiveNoteState {
    fiddle::Note note;
    uint64_t lastEmittedOffset;
    uint64_t subnoteCount = 0;
  };

  mutable std::mutex mutex;
  double sampleRate;
  uint64_t subnoteDurationSamples;
  Callbacks callbacks;

  std::map<uint64_t, ActiveNoteState> activeNotes;

  void emitSubnote(ActiveNoteState &state, bool isLast) {
    fiddle::Subnote sub;
    sub.set_id(state.note.id());
    sub.set_note_number(state.note.note_number());
    sub.set_channel(state.note.channel());
    sub.set_velocity(state.note.start_velocity()); // Simplified

    sub.set_offset_samples(state.lastEmittedOffset);

    uint64_t duration = subnoteDurationSamples;
    if (isLast) {
      duration = state.note.duration_samples() - state.lastEmittedOffset;
    }
    sub.set_duration_samples(duration);

    sub.set_is_first(state.subnoteCount == 0);
    sub.set_is_last(isLast);

    if (callbacks.onSubnoteGenerated) {
      callbacks.onSubnoteGenerated(sub);
    }

    state.lastEmittedOffset += duration;
    state.subnoteCount++;
  }
};

} // namespace fiddle
