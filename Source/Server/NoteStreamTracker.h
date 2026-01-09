#pragma once

#include "midi_event.pb.h"
#include <juce_core/juce_core.h>
#include <map>
#include <mutex>
#include <vector>

namespace fiddle {

/**
 * Tracks active MIDI notes and manages their lifecycle.
 */
class NoteStreamTracker {
public:
  struct Callbacks {
    std::function<void(const fiddle::Note &)> onNoteStarted;
    std::function<void(const fiddle::Note &)> onNoteEnded;
  };

  NoteStreamTracker() = default;

  void setCallbacks(Callbacks cbs) { callbacks = std::move(cbs); }

  void processEvent(const fiddle::MidiEvent &event) {
    std::lock_guard<std::mutex> lock(mutex);

    if (event.has_note_on() && event.note_on().velocity() > 0) {
      handleNoteOn(event);
    } else if (event.has_note_off() ||
               (event.has_note_on() && event.note_on().velocity() == 0)) {
      handleNoteOff(event);
    }
  }

  std::vector<fiddle::Note> getActiveNotes() const {
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<fiddle::Note> active;
    for (auto const &[key, note] : activeNotes) {
      active.push_back(note);
    }
    return active;
  }

private:
  mutable std::mutex mutex;
  Callbacks callbacks;

  struct NoteKey {
    uint32_t channel;
    uint32_t noteNumber;
    bool operator<(const NoteKey &other) const {
      if (channel != other.channel)
        return channel < other.channel;
      return noteNumber < other.noteNumber;
    }
  };

  std::map<NoteKey, fiddle::Note> activeNotes;
  uint64_t nextNoteId = 1;

  void handleNoteOn(const fiddle::MidiEvent &event) {
    const auto &noteOn = event.note_on();
    NoteKey key{event.channel(), noteOn.note_number()};

    // If note already exists, end it first (legacy behavior for some MIDI)
    if (activeNotes.count(key)) {
      handleNoteOff(event);
    }

    fiddle::Note note;
    note.set_id(nextNoteId++);
    note.set_note_number(noteOn.note_number());
    note.set_channel(event.channel());
    note.set_start_velocity(noteOn.velocity());
    note.set_start_sample(event.timestamp_samples());

    activeNotes[key] = note;

    if (callbacks.onNoteStarted) {
      callbacks.onNoteStarted(note);
    }
  }

  void handleNoteOff(const fiddle::MidiEvent &event) {
    uint32_t noteNum = 0;
    float velocity = 0;

    if (event.has_note_off()) {
      noteNum = event.note_off().note_number();
      velocity = event.note_off().velocity();
    } else {
      noteNum = event.note_on().note_number();
      velocity = 0;
    }

    NoteKey key{event.channel(), noteNum};
    auto it = activeNotes.find(key);
    if (it != activeNotes.end()) {
      it->second.set_end_velocity(velocity);
      it->second.set_duration_samples(event.timestamp_samples() -
                                      it->second.start_sample());

      if (callbacks.onNoteEnded) {
        callbacks.onNoteEnded(it->second);
      }
      activeNotes.erase(it);
    }
  }
};

} // namespace fiddle
