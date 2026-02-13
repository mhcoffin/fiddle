#pragma once

#include "ExpressionMap.h"
#include "midi_event.pb.h"
#include <algorithm> // Added for std::find
#include <array>
#include <iostream>
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
    std::function<void(const fiddle::Note &)> onNoteUpdated;
    std::function<void(const fiddle::MidiEvent &, uint64_t absoluteSamples,
                       int oldCCVal)>
        onMidiEvent;
  };

  std::function<void(const juce::String &)> uiLogger;

  NoteStreamTracker() {
    for (auto &chan : currentCCs)
      chan.fill(0);
  }

  void setCallbacks(Callbacks cbs) { callbacks = std::move(cbs); }

  void setExpressionMap(const ExpressionMap *map) { expMap = map; }

  void resetSessionStartTime() {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    sessionStartTime = -1.0;
    activeNotes.clear();
    if (uiLogger)
      uiLogger("<b>[Tracker]</b> Session reset via Transport Start");
  }

  void processEvent(const fiddle::MidiEvent &event) {
    if (uiLogger)
      uiLogger("[Tracker] processEvent: Case=" +
               juce::String((int)event.event_case()));

    std::lock_guard<std::recursive_mutex> lock(mutex);
    std::cerr << "[NoteStreamTracker] processEvent: Case="
              << (int)event.event_case() << " Ch=" << event.channel()
              << std::endl;

    uint64_t sessionTimestamp = getSessionSamples();
    uint64_t absoluteSamples =
        event.has_host_sample_position()
            ? event.host_sample_position()
            : (sessionTimestamp + event.timestamp_samples());

    if (event.has_note_on()) {
      int vel = event.note_on().velocity();
      if (uiLogger)
        uiLogger("[Tracker] NoteOn: Note=" +
                 juce::String(event.note_on().note_number()) +
                 " Vel=" + juce::String(vel));

      if (vel > 0) {
        handleNoteOn(event, absoluteSamples);
      } else {
        handleNoteOff(event, absoluteSamples);
      }
      if (callbacks.onMidiEvent)
        callbacks.onMidiEvent(event, absoluteSamples, -1);
    } else if (event.has_note_off()) {
      if (uiLogger)
        uiLogger("[Tracker] NoteOff: Note=" +
                 juce::String(event.note_off().note_number()));
      handleNoteOff(event, absoluteSamples);
      if (callbacks.onMidiEvent)
        callbacks.onMidiEvent(event, absoluteSamples, -1);
    } else if (event.has_cc()) {
      uint32_t chan = event.channel();
      uint32_t ccNum = event.cc().controller_number();
      uint8_t newVal = (uint8_t)event.cc().controller_value();

      if (chan < 16) {
        uint8_t oldVal = currentCCs[chan][ccNum % 128];
        currentCCs[chan][ccNum % 128] = newVal;

        if (oldVal != newVal) {
          if (uiLogger) {
            uiLogger("<b>[CC]</b> Ch " + juce::String(chan + 1) + " | CC " +
                     juce::String(ccNum) + " -> " + juce::String(newVal));
          }

          // Append CC change to automation envelope of ALL active notes
          // on this channel. This captures the full CC history per-note
          // for later playback.
          if (ccNum < 128) {
            uint64_t currentSamples =
                sessionTimestamp + event.timestamp_samples();
            for (auto &note : activeNotes) {
              if (note.channel() == chan) {
                auto &lane = (*note.mutable_cc_automation())[ccNum];
                auto *pt = lane.add_points();
                uint64_t offset = (currentSamples > note.start_sample())
                                      ? (currentSamples - note.start_sample())
                                      : 0;
                pt->set_offset_samples(offset);
                pt->set_value(newVal);
              }
            }
          }

          // Expression map enrichment: update notation dimensions/techniques
          // on the most recently started note (30ms jitter window).
          {
            uint64_t currentSamples =
                sessionTimestamp + event.timestamp_samples();
            for (int i = (int)activeNotes.size() - 1; i >= 0; --i) {
              auto &note = activeNotes[i];
              if (note.channel() == chan) {
                uint64_t age = (currentSamples > note.start_sample())
                                   ? (currentSamples - note.start_sample())
                                   : 0;

                if (age < 1323) { // 30ms window
                  if (enrichNoteWithCC(note, (int)ccNum, (int)newVal)) {
                    if (callbacks.onNoteUpdated)
                      callbacks.onNoteUpdated(note);
                  }
                }
                break; // Only the latest one
              }
            }
          }

          if (callbacks.onMidiEvent)
            callbacks.onMidiEvent(event, absoluteSamples, (int)oldVal);
        }
      }
    } else if (event.has_transport()) {
      if (event.transport().type() ==
          fiddle::MidiEvent_TransportEvent_Type_START) {
        resetSessionStartTime();
        if (callbacks.onMidiEvent)
          callbacks.onMidiEvent(event, 0, -1);
      }
    } else {
      // Forward all other events (ProgramChange, ContextUpdate, PitchBend,
      // etc.)
      if (callbacks.onMidiEvent)
        callbacks.onMidiEvent(event, absoluteSamples, -1);
    }
  }

  std::vector<fiddle::Note> getActiveNotes() const {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    return activeNotes;
  }

  uint64_t getSessionSamples() const {
    double now = juce::Time::getMillisecondCounterHiRes();
    if (sessionStartTime < 0) {
      sessionStartTime = now;
    }
    return (uint64_t)((now - sessionStartTime) * 44.1);
  }

private:
  mutable double sessionStartTime = -1.0;
  mutable std::recursive_mutex mutex;
  Callbacks callbacks;
  const ExpressionMap *expMap = nullptr;

  std::vector<fiddle::Note> activeNotes;
  std::array<std::array<uint8_t, 128>, 16> currentCCs;
  uint64_t nextNoteId = 1;
  bool enrichNoteWithCC(fiddle::Note &note, int ccNum, int val) {
    if (expMap == nullptr)
      return false;

    const auto *dim = expMap->getDimensionForCC(ccNum);
    if (dim != nullptr) {
      (*note.mutable_notation_dimensions())[dim->name.toStdString()] =
          (float)val;

      auto techIt = dim->techniques.find(val);
      if (techIt != dim->techniques.end()) {
        (*note.mutable_notation_techniques())[dim->name.toStdString()] =
            techIt->second.toStdString();
      } else {
        note.mutable_notation_techniques()->erase(dim->name.toStdString());
      }

      bool isDefault =
          (std::find(dim->defaultValues.begin(), dim->defaultValues.end(),
                     val) != dim->defaultValues.end());
      (*note.mutable_notation_is_default())[dim->name.toStdString()] =
          isDefault;
      return true;
    }
    return false;
  }

  void handleNoteOn(const fiddle::MidiEvent &event, uint64_t absoluteSamples) {
    const auto &noteOn = event.note_on();
    uint32_t chan = event.channel();

    fiddle::Note note;
    note.set_id(nextNoteId++);
    note.set_note_number(noteOn.note_number());
    note.set_channel(chan);
    note.set_start_velocity(noteOn.velocity());
    note.set_start_sample(absoluteSamples);

    // Enrich note with notation dimensions from ExpressionMap
    if (expMap != nullptr && chan < 16) {
      for (const auto &dim : expMap->getDimensions()) {
        if (dim.ccNumber >= 0 && dim.ccNumber < 128) {
          uint8_t val = currentCCs[chan][dim.ccNumber % 128];
          enrichNoteWithCC(note, dim.ccNumber, val);
        }
      }

      // Set dynamics mode based on current CC102 (base switch) value
      uint8_t cc102Val = currentCCs[chan][102];
      if (expMap->dynamicsUsesCC1((int)cc102Val)) {
        note.set_dynamics_mode(fiddle::Note::CC);
      } else {
        note.set_dynamics_mode(fiddle::Note::VELOCITY);
      }
    }

    // Seed all CC automation lanes with current values at offset 0
    if (chan < 16) {
      for (int cc = 0; cc < 128; ++cc) {
        uint8_t val = currentCCs[chan][cc];
        if (val != 0) { // Only seed non-zero CCs to keep notes compact
          auto &lane = (*note.mutable_cc_automation())[cc];
          auto *pt = lane.add_points();
          pt->set_offset_samples(0);
          pt->set_value(val);
        }
      }
    }

    activeNotes.push_back(note);

    if (callbacks.onNoteStarted) {
      std::cerr << "[NoteStreamTracker] Triggering onNoteStarted: ID="
                << note.id() << std::endl;
      callbacks.onNoteStarted(note);
    }
  }

  void handleNoteOff(const fiddle::MidiEvent &event, uint64_t absoluteSamples) {
    uint32_t noteNum = 0;
    uint32_t velocity = 0;

    if (event.has_note_off()) {
      noteNum = event.note_off().note_number();
      velocity = event.note_off().velocity();
    } else {
      noteNum = event.note_on().note_number();
      velocity = 0;
    }

    uint32_t chan = event.channel();

    for (auto it = activeNotes.begin(); it != activeNotes.end(); ++it) {
      if (it->channel() == chan && it->note_number() == noteNum) {
        it->set_end_velocity(velocity);
        uint64_t endSample = absoluteSamples;
        if (endSample < it->start_sample()) {
          // This NoteOff likely belongs to a previous instance of this pitch
          // that was already ended by a newer NoteOn. Ignore it.
          continue;
        }

        it->set_duration_samples(endSample - it->start_sample());

        if (callbacks.onNoteEnded) {
          std::cerr << "[NoteStreamTracker] Triggering onNoteEnded: ID="
                    << it->id() << std::endl;
          callbacks.onNoteEnded(*it);
        }
        activeNotes.erase(it);
        return;
      }
    }
  }
};

} // namespace fiddle
