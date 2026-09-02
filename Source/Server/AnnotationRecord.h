#pragma once

#include "ExpressionMapData.h"
#include <map>
#include <set>
#include <string>
#include <vector>

namespace fiddle {

/// Structured inspection record capturing an incoming note and, when present,
/// the expression-map decision made for it. Always produced (not a debug
/// mode), but only consumed by the Note Inspector UI.
struct AnnotationRecord {
  // ── Input ──
  int noteNumber = -1;                    // MIDI note number (0-127)
  int inputChannel = 0;                   // MIDI channel (1-based)
  std::set<std::string> inputTechniques;  // effective pt.xxx IDs
  std::map<std::string, std::string>
      receivedTechniques; // raw dimension → value
  std::map<std::string, float> receivedDimensions; // raw dimension → value
  std::set<std::string> defaultTechniqueDimensions;
  bool expressionMapAssigned = false;

  // ── Match resolution ──
  LengthCategory lengthCategory = LengthCategory::VeryLong;
  std::string baseSwitchName;             // human name of chosen base
  std::set<std::string> baseTechniqueIDs; // technique IDs of chosen base
  std::vector<std::string> addOnNames;    // names of applied add-ons
  std::set<std::string> unmatchedTechniques; // PTs the map couldn't handle
  std::vector<CandidateInfo> candidates;  // all bases considered (specificity scored)

  // ── MIDI output ──
  std::vector<SwitchAction> midiActionsEmitted; // switch-on actions sent
  bool redundancySkipped = false; // same match as previous note → no MIDI sent

  // ── Modifiers ──
  int velocityBefore = 0;
  int velocityAfter = 0;
  int transposeApplied = 0;

  // ── Dynamics ──
  std::string dynamicsRouting; // e.g. "velocity→CC1", "CC→velocity"

  // ── Duration (populated by onNoteEnd) ──
  double durationAdjustMs = 0.0;

  /// Reset all fields for reuse.
  void clear() { *this = AnnotationRecord{}; }
};

} // namespace fiddle
