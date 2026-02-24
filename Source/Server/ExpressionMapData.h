#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>

namespace fiddle {

/// Type of MIDI action in a switch-on or switch-off action list.
enum class SwitchActionType {
  KeySwitch,     // param1=note, param2=velocity → note-on before the note
  NoteVelocity,  // param1=note, param2=velocity → velocity-layer selection
  CC,            // param1=CC#, param2=CC value
  ControlChange, // alias for CC (some .doricolib files use this name)
  ChannelSwitch, // param1=channel (1-based), param2=0
  ProgramChange, // param1=program#, param2=0 (for future use)
  Unknown,
};

/// A single MIDI action (e.g., "send CC 102 value 3" or "send keyswitch C1").
struct SwitchAction {
  SwitchActionType type = SwitchActionType::Unknown;
  int param1 = 0;
  int param2 = 0;

  bool operator==(const SwitchAction &o) const {
    return type == o.type && param1 == o.param1 && param2 == o.param2;
  }
};

/// One entry in a .doricolib expression map: a combination of playback
/// techniques mapped to a set of MIDI actions.
struct TechniqueCombination {
  std::string name;                   // Human-readable: "Legato + Marcato"
  std::set<std::string> techniqueIDs; // {"pt.legato", "pt.marcato"}
  int baseSwitchID = 0;

  std::vector<SwitchAction> switchOnActions;
  std::vector<SwitchAction> switchOffActions;

  // Timing: how far before the note should switch actions be sent
  int ticksBefore = 0;
  int millisecondsBefore = 0;

  // Conditions (stored but not evaluated yet)
  std::string conditionString;

  // Modifiers
  float velocityFactor = 1.0f;
  float lengthFactor = 1.0f;
  bool monophonic = false;

  // Ranges
  int velocityMin = 1, velocityMax = 127;
  int pitchMin = 0, pitchMax = 127;
  int transpose = 0;
};

/// Parsed representation of a .doricolib expression map file.
struct ExpressionMapData {
  std::string name;     // e.g. "VSYSW 01 Piccolo Flute sus"
  std::string entityID; // e.g. "xmap.user.babylonwaves.vsysw_01_..."
  std::string creator;
  std::string description;

  std::vector<TechniqueCombination> combinations;

  // Metadata
  bool autoMutualExclusion = true;
  int pitchBendRange = 2;

  /// Find all combinations whose techniqueIDs exactly match the given set.
  std::vector<const TechniqueCombination *>
  findExact(const std::set<std::string> &techniques) const {
    std::vector<const TechniqueCombination *> result;
    for (auto &c : combinations) {
      if (c.techniqueIDs == techniques)
        result.push_back(&c);
    }
    return result;
  }

  /// Find the combination with the most overlap with the given techniques.
  /// Returns nullptr if no combination has any overlap.
  const TechniqueCombination *
  findBestMatch(const std::set<std::string> &techniques) const {
    const TechniqueCombination *best = nullptr;
    int bestScore = 0;
    for (auto &c : combinations) {
      int score = 0;
      for (auto &t : c.techniqueIDs) {
        if (techniques.count(t))
          ++score;
      }
      if (score > bestScore) {
        bestScore = score;
        best = &c;
      }
    }
    return best;
  }
};

} // namespace fiddle
