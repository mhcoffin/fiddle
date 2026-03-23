#pragma once

#include "ExpressionMapData.h"
#include "MidiCaptureLog.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

namespace fiddle {

/**
 * Tracks incoming MIDI switch state (keyswitches, CCs, PCs) and
 * reverse-matches against an expression map's switchOnActions to
 * identify which technique combination is active.
 *
 * Labels are resolved at NoteOff time so that note-length conditions
 * can be evaluated using the actual duration.
 */
class IncomingSwitchTracker {
public:
  explicit IncomingSwitchTracker(std::shared_ptr<ExpressionMapData> em)
      : em_(std::move(em)) {
    if (!em_)
      return;
    // Build the set of note numbers used as keyswitches in any switch.
    for (const auto &combo : em_->combinations) {
      if (combo.isAddOn)
        continue;
      for (const auto &action : combo.switchOnActions) {
        if (action.type == SwitchActionType::KeySwitch)
          keyswitchNotes_.insert(action.param1);
      }
    }
  }

  /// Is this note number used as a keyswitch in the expression map?
  bool isKeyswitch(int noteNumber) const {
    return keyswitchNotes_.count(noteNumber) > 0;
  }

  /// Feed an incoming MIDI event to update the switch state.
  /// Call this for every event (CC, NoteOn, NoteOff, PC).
  void onMidi(CapturedMidiEvent::Type type, int p1, int p2, double timeMs) {
    switch (type) {
    case CapturedMidiEvent::NoteOn:
      if (isKeyswitch(p1)) {
        // Dorico sends a complete fresh set of keyswitches before each
        // note group.  If a real note has been played since the last
        // keyswitch batch, this is a new batch — clear the old set.
        if (realNoteSeen_) {
          activeKeyswitches_.clear();
          realNoteSeen_ = false;
        }
        activeKeyswitches_.insert(p1);
      } else {
        // Real note — stash switch state and timestamp for NoteOff resolution
        realNoteSeen_ = true;
        PendingNote pn;
        pn.keyswitches = activeKeyswitches_;
        pn.ccValues = ccValues_;
        pn.programChange = programChange_;
        pn.noteOnTimeMs = timeMs;
        pendingNotes_[p1] = pn;
      }
      break;

    case CapturedMidiEvent::NoteOff:
      if (isKeyswitch(p1)) {
        // Keyswitches in Dorico are typically latched (NoteOff doesn't
        // deactivate them). But we remove them here in case a different
        // set of keyswitches replaces it.  Dorico sends new keyswitches
        // before each note group, so this is fine.
        // Actually, keep latched — don't remove on NoteOff.
      }
      // For real notes, NoteOff is handled via resolveNoteOff().
      break;

    case CapturedMidiEvent::CC:
      ccValues_[p1] = p2;
      break;

    case CapturedMidiEvent::ProgramChange:
      programChange_ = p1;
      break;
    }
  }

  /// Resolve all matching switch names for a note that just ended.
  /// Returns all switches whose switchOnActions match the stashed state
  /// and whose conditions are satisfied by the actual duration.
  /// Multiple switches may share the same keyswitches/CCs/PCs.
  std::vector<std::string> resolveNoteOff(int noteNumber, double noteOffTimeMs) {
    if (!em_)
      return {};

    auto it = pendingNotes_.find(noteNumber);
    if (it == pendingNotes_.end())
      return {};

    auto &pn = it->second;
    double durationSec = (noteOffTimeMs - pn.noteOnTimeMs) / 1000.0;

    std::vector<std::string> matches;

    for (const auto &combo : em_->combinations) {
      if (combo.isAddOn)
        continue;

      // Check all switchOnActions against the stashed state
      bool allMatch = true;

      for (const auto &action : combo.switchOnActions) {
        switch (action.type) {
        case SwitchActionType::KeySwitch: {
          if (pn.keyswitches.count(action.param1) == 0)
            allMatch = false;
          break;
        }
        case SwitchActionType::CC:
        case SwitchActionType::ControlChange: {
          auto ccIt = pn.ccValues.find(action.param1);
          if (ccIt == pn.ccValues.end() || ccIt->second != action.param2)
            allMatch = false;
          break;
        }
        case SwitchActionType::ProgramChange: {
          if (pn.programChange != action.param1)
            allMatch = false;
          break;
        }
        default:
          break;
        }

        if (!allMatch)
          break;
      }

      if (!allMatch || combo.switchOnActions.empty())
        continue;

      // Evaluate note-length condition if present
      if (!combo.condition.empty()) {
        if (!combo.condition.evaluate(durationSec))
          continue;
      }

      matches.push_back(combo.name);
    }

    pendingNotes_.erase(it);
    return matches;
  }

  /// Reset all tracked state (useful on transport stop / panic).
  void reset() {
    activeKeyswitches_.clear();
    ccValues_.clear();
    programChange_ = -1;
    realNoteSeen_ = false;
    pendingNotes_.clear();
  }

private:
  struct PendingNote {
    std::set<int> keyswitches;
    std::map<int, int> ccValues;
    int programChange = -1;
    double noteOnTimeMs = 0.0;
  };

  std::shared_ptr<ExpressionMapData> em_;
  std::set<int> keyswitchNotes_; // note numbers used as keyswitches

  // Current incoming state
  std::set<int> activeKeyswitches_;
  std::map<int, int> ccValues_;
  int programChange_ = -1;
  bool realNoteSeen_ = false; // true after a real note, cleared on next KS batch

  // Notes waiting for NoteOff to resolve
  std::unordered_map<int, PendingNote> pendingNotes_;
};

} // namespace fiddle
