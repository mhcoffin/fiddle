#pragma once

#include "ExpressionMapData.h"
#include <set>
#include <string>
#include <vector>

namespace fiddle {

/// Structured decision record capturing the full chain of reasoning for one
/// note processed by ExpressionMapAnnotator.  Always produced (not a debug
/// mode), but only consumed by the Note Inspector UI.
struct AnnotationRecord {
  // ── Input ──
  int noteNumber = -1;                    // MIDI note number (0-127)
  std::set<std::string> inputTechniques; // pt.xxx IDs (after MEG expansion)

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
