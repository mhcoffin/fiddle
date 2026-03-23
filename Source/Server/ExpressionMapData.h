#pragma once



#include <set>
#include <string>
#include <vector>

namespace fiddle {

/// Scoring info for a single candidate base switch (used in Tier 3 reporting).
struct CandidateInfo {
  std::string name;
  int score = 0;
  std::set<std::string> techniqueIDs;
};

/// How dynamics (volume) should be expressed for a given technique combination.
enum class VolumeType {
  kNoteVelocity, // dynamics via note velocity (typical for short notes)
  kCC,           // dynamics via CC (typically CC1 for sustained notes)
};

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

/// Parsed note-length condition from conditionString.
/// Dorico thresholds at 120 BPM (seconds):
///   kVeryShort ≤ 0.1875, kShort ≤ 0.375, kMedium ≤ 0.75,
///   kLong ≤ 1.5, kVeryLong > 1.5
struct NoteCondition {
  enum class Operator { None, LT, LE, GT, GE, EQ };
  enum class Threshold { VeryShort, Short, Medium, Long, VeryLong };
  Operator op = Operator::None;
  Threshold threshold = Threshold::Medium;

  bool empty() const { return op == Operator::None; }

  /// Return the threshold in seconds (at 120 BPM reference).
  static double thresholdSeconds(Threshold t) {
    switch (t) {
    case Threshold::VeryShort: return 0.1875;
    case Threshold::Short:     return 0.375;
    case Threshold::Medium:    return 0.75;
    case Threshold::Long:      return 1.5;
    case Threshold::VeryLong:  return 1e9;
    }
    return 0.75;
  }

  /// Return the lower bound of the bucket for a given threshold.
  static double bucketLower(Threshold t) {
    switch (t) {
    case Threshold::VeryShort: return 0.0;
    case Threshold::Short:     return 0.1875;
    case Threshold::Medium:    return 0.375;
    case Threshold::Long:      return 0.75;
    case Threshold::VeryLong:  return 1.5;
    }
    return 0.0;
  }

  /// Evaluate: does the given duration (in seconds) satisfy this condition?
  /// For EQ, checks if the duration falls in the named bucket
  /// (e.g. `== kShort` means 0.1875 < d ≤ 0.375).
  bool evaluate(double durationSeconds) const {
    if (op == Operator::None)
      return true;
    double upper = thresholdSeconds(threshold);
    switch (op) {
    case Operator::LT: return durationSeconds < upper;
    case Operator::LE: return durationSeconds <= upper;
    case Operator::GT: return durationSeconds > upper;
    case Operator::GE: return durationSeconds >= upper;
    case Operator::EQ: {
      double lower = bucketLower(threshold);
      return durationSeconds > lower && durationSeconds <= upper;
    }
    default: return true;
    }
  }
};

/// Parse "NoteLength > kShort" etc. into a NoteCondition.
inline NoteCondition parseConditionString(const std::string &s) {
  NoteCondition nc;
  if (s.empty() || s.find("NoteLength") == std::string::npos)
    return nc;
  // Extract operator (check two-char operators first)
  if      (s.find(">=") != std::string::npos) nc.op = NoteCondition::Operator::GE;
  else if (s.find("<=") != std::string::npos) nc.op = NoteCondition::Operator::LE;
  else if (s.find("==") != std::string::npos) nc.op = NoteCondition::Operator::EQ;
  else if (s.find(">")  != std::string::npos) nc.op = NoteCondition::Operator::GT;
  else if (s.find("<")  != std::string::npos) nc.op = NoteCondition::Operator::LT;
  else return nc;
  // Extract threshold (check longer names first to avoid prefix collisions)
  if      (s.find("kVeryShort") != std::string::npos) nc.threshold = NoteCondition::Threshold::VeryShort;
  else if (s.find("kVeryLong")  != std::string::npos) nc.threshold = NoteCondition::Threshold::VeryLong;
  else if (s.find("kShort")     != std::string::npos) nc.threshold = NoteCondition::Threshold::Short;
  else if (s.find("kMedium")    != std::string::npos) nc.threshold = NoteCondition::Threshold::Medium;
  else if (s.find("kLong")      != std::string::npos) nc.threshold = NoteCondition::Threshold::Long;
  return nc;
}

/// Duration percentage overrides from the expression map's
/// <playbackOptionsOverrides> section.  Dorico defaults apply when the
/// expression map doesn't override a particular category.
struct TimingOptions {
  int noteDurationPercent = 90;         // default (no articulation marking)
  int staccatoDurationPercent = 50;     // Dorico default for staccato
  int staccatissimoDurationPercent = 25; // Dorico default for staccatissimo
  int legatoDurationPercent = 105;      // Dorico default for legato
  int tenutoDurationPercent = 100;      // Dorico default for tenuto
  int marcatoDurationPercent = 75;      // Dorico default for marcato
};

/// A mutual exclusion group: a set of PTs where only one can be active.
/// The defaultID is used when a note has no PT from this group.
struct MutualExclusionGroup {
  std::string name;
  std::vector<std::string> techniqueIDs;
  std::string defaultID; // first entry is default unless pt.natural is present
};

/// Note length classification (Dorico categories).
enum class LengthCategory {
  VeryShort, // ≤ 0.1875s at 120 BPM
  Short,     // ≤ 0.375s
  Medium,    // ≤ 0.75s
  Long,      // ≤ 1.5s
  VeryLong,  // > 1.5s
};

/// Classify a note duration (in seconds) into a LengthCategory.
inline LengthCategory classifyLength(double durationSeconds) {
  if (durationSeconds <= 0.1875) return LengthCategory::VeryShort;
  if (durationSeconds <= 0.375)  return LengthCategory::Short;
  if (durationSeconds <= 0.75)   return LengthCategory::Medium;
  if (durationSeconds <= 1.5)    return LengthCategory::Long;
  return LengthCategory::VeryLong;
}

/// Hardcoded default MEGs (used when autoMutualExclusion is true and
/// the expression map doesn't define its own MEGs).
inline std::vector<MutualExclusionGroup> getDefaultMEGs() {
  return {
    {"Bowing",       {"pt.arco", "pt.pizzicato", "pt.collegno", "pt.spiccato"}, "pt.arco"},
    {"Mute",         {"pt.open", "pt.muted", "pt.harmonicmute", "pt.cupmute", "pt.straightmute"}, "pt.open"},
    {"Vibrato",      {"pt.vibrato", "pt.senzavibrato", "pt.moltovibrato"}, "pt.vibrato"},
    {"Legato",       {"pt.nonlegato", "pt.legato"}, "pt.nonlegato"},
    {"Snares",       {"pt.snareson", "pt.snaresoff"}, "pt.snareson"},
    {"Articulation", {"pt.natural", "pt.accented", "pt.tenuto"}, "pt.natural"},
  };
}

/// One entry in a .doricolib expression map: a combination of playback
/// techniques mapped to a set of MIDI actions.
struct TechniqueCombination {
  std::string name;                   // Human-readable: "Legato + Marcato"
  std::set<std::string> techniqueIDs; // {"pt.legato", "pt.marcato"}
  int baseSwitchID = 0;
  bool isAddOn = false; // true for add-on switches, false for base switches

  std::vector<SwitchAction> switchOnActions;
  std::vector<SwitchAction> switchOffActions;

  // Timing: how far before the note should switch actions be sent
  int ticksBefore = 0;
  int millisecondsBefore = 0;

  // Conditions (stored but not evaluated yet)
  std::string conditionString;
  NoteCondition condition; // parsed from conditionString

  // Modifiers
  float velocityFactor = 1.0f;
  float lengthFactor = 1.0f;
  bool monophonic = false;

  // Ranges
  int velocityMin = 1, velocityMax = 127;
  int pitchMin = 0, pitchMax = 127;
  int transpose = 0;

  // Dynamics: how the target VST receives volume for this technique
  //   volumeType  = primary dynamics (attack / velocity)
  //   volumeType2 = secondary dynamics (sustain / CC1)
  VolumeType volumeType = VolumeType::kNoteVelocity;
  int volumeCC = 1; // CC number when volumeType == kCC (usually 1)
  VolumeType volumeType2 = VolumeType::kNoteVelocity; // secondary dynamics
  int volumeCC2 = 1; // CC number when volumeType2 == kCC
};

/// Result of resolveMatch(): the base switch plus zero or more add-ons
/// whose combined MIDI actions should be sent for this note.
struct MatchResult {
  const TechniqueCombination *base = nullptr;
  std::vector<const TechniqueCombination *> addOns;

  bool empty() const { return base == nullptr; }

  /// Collect all switchOnActions from the base and add-ons.
  std::vector<SwitchAction> allSwitchOnActions() const {
    std::vector<SwitchAction> result;
    if (base)
      result.insert(result.end(), base->switchOnActions.begin(),
                    base->switchOnActions.end());
    for (auto *a : addOns)
      result.insert(result.end(), a->switchOnActions.begin(),
                    a->switchOnActions.end());
    return result;
  }

  /// Collect all switchOffActions from the base and add-ons.
  std::vector<SwitchAction> allSwitchOffActions() const {
    std::vector<SwitchAction> result;
    if (base)
      result.insert(result.end(), base->switchOffActions.begin(),
                    base->switchOffActions.end());
    for (auto *a : addOns)
      result.insert(result.end(), a->switchOffActions.begin(),
                    a->switchOffActions.end());
    return result;
  }

  /// Equality for redundancy tracking.
  bool operator==(const MatchResult &o) const {
    return base == o.base && addOns == o.addOns;
  }
  bool operator!=(const MatchResult &o) const { return !(*this == o); }
};

/// Parsed representation of a .doricolib expression map file.
struct ExpressionMapData {
  std::string name;     // e.g. "VSYSW 01 Piccolo Flute sus"
  std::string entityID; // e.g. "xmap.user.babylonwaves.vsysw_01_..."
  std::string creator;
  std::string description;
  int version = 0; // for deduplication: keep highest version

  /// All technique combinations (base switches + add-ons in one list).
  /// The parser sets isAddOn=true for add-on switches.
  std::vector<TechniqueCombination> combinations;

  /// Mutual exclusion groups parsed from the expression map (or defaults).
  std::vector<MutualExclusionGroup> megs;

  /// Playback timing options (category-level duration percentages)
  TimingOptions timingOptions;

  // Metadata
  bool autoMutualExclusion = true;
  int pitchBendRange = 2;

  /// Expand a note's technique set by adding the default PT for each MEG
  /// where the note has no member. Modifies `techniques` in place.
  void expandWithDefaults(std::set<std::string> &techniques) const {
    for (auto &meg : megs) {
      bool found = false;
      for (auto &id : meg.techniqueIDs) {
        if (techniques.count(id)) {
          found = true;
          break;
        }
      }
      if (!found && !meg.defaultID.empty()) {
        techniques.insert(meg.defaultID);
      }
    }
  }

  // -----------------------------------------------------------------
  // 6-tier cascading resolution (see docs/Interpreting Expression Maps.md)
  // Based on Daniel Spreadbury's description of Dorico's algorithm.
  // -----------------------------------------------------------------

  /// Hardcoded technique priority for Tier 4 fallback matching.
  /// Higher value = higher priority.  Dorico priorities:
  ///   harmonic, pizzicato → highest;  mute variants → high;  rest → normal.
  static int techniquePriority(const std::string &pt) {
    if (pt == "pt.harmonic" || pt == "pt.naturalharmonic" ||
        pt == "pt.artificialharmonic")
      return 100;
    if (pt == "pt.pizzicato" || pt == "pt.bartokpizzicato" ||
        pt == "pt.snappizzicato")
      return 100;
    if (pt == "pt.muted" || pt == "pt.cupmute" || pt == "pt.straightmute" ||
        pt == "pt.harmonicmute" || pt == "pt.wawamute" ||
        pt == "pt.harmonmute" || pt == "pt.bucketmute" ||
        pt == "pt.plungermute")
      return 50;
    return 10; // everything else (legato, staccato, vibrato, etc.)
  }

  /// Resolve a note's playback techniques to a base switch + add-ons.
  /// Uses 6-tier cascading resolution on the **raw** technique set (no
  /// MEG default expansion).
  /// `lengthCat` filters bases with length conditions.
  /// When `outCandidates` is non-null, receives all base candidates considered.
  MatchResult resolveMatch(const std::set<std::string> &techniques,
                           LengthCategory lengthCat = LengthCategory::VeryLong,
                           std::vector<CandidateInfo> *outCandidates = nullptr) const {
    double repDuration = lengthCategoryToSeconds(lengthCat);

    // Collect eligible base switches (pass length condition)
    struct BaseCandidate {
      const TechniqueCombination *combo;
      int exactOverlap;   // |base ∩ note|
      int baseSize;       // |base|
      bool isExact;       // base PTs == note PTs
      bool isSubsetOfNote; // base ⊆ note
    };
    std::vector<BaseCandidate> bases;
    for (auto &c : combinations) {
      if (c.isAddOn)
        continue;
      if (!c.condition.empty() && !c.condition.evaluate(repDuration))
        continue;

      int overlap = 0;
      for (auto &t : c.techniqueIDs) {
        if (techniques.count(t))
          ++overlap;
      }
      bool sub = isSubset(c.techniqueIDs, techniques);
      bool exact = sub && (c.techniqueIDs.size() == techniques.size());
      bases.push_back({&c, overlap, (int)c.techniqueIDs.size(), exact, sub});

      if (outCandidates)
        outCandidates->push_back(
            {c.name, overlap, c.techniqueIDs});
    }

    // ── Tier 1: Exact base switch match ───────────────────────────
    // A base whose PTs exactly equal the note's PTs.
    for (auto &bc : bases) {
      if (bc.isExact)
        return {bc.combo, {}};
    }

    // ── Tier 2: Base + add-ons = exact match ──────────────────────
    // Find a base that is a subset, then see if add-ons can cover
    // the remainder exactly.
    {
      const TechniqueCombination *bestBase = nullptr;
      int bestSize = -1;
      for (auto &bc : bases) {
        if (!bc.isSubsetOfNote)
          continue;
        if (bc.baseSize > bestSize) {
          bestSize = bc.baseSize;
          bestBase = bc.combo;
        }
      }
      if (bestBase) {
        std::set<std::string> remaining;
        for (auto &t : techniques) {
          if (!bestBase->techniqueIDs.count(t))
            remaining.insert(t);
        }
        auto addOns = findAddOnsCovering(remaining);
        // Check if all remaining PTs are now covered
        std::set<std::string> covered;
        for (auto *a : addOns)
          for (auto &t : a->techniqueIDs)
            covered.insert(t);
        if (isSubset(remaining, covered))
          return {bestBase, addOns};
        // Even if not exact, if the base is a subset, use it
        // (partial coverage is better than nothing — Tier 2.5)
        // Fall through to Tier 3 only if we have ≥3 PTs
      }
    }

    // ── Tier 3: Partial match (≥3 PTs, covering ≥2) ──────────────
    if (techniques.size() >= 3) {
      const TechniqueCombination *bestBase = nullptr;
      int bestOverlap = -1;
      int bestSize = -1;
      for (auto &bc : bases) {
        if (!bc.isSubsetOfNote)
          continue;
        // Must cover ≥2 of the desired PTs
        if (bc.baseSize >= 2 && bc.baseSize > bestOverlap) {
          bestOverlap = bc.baseSize;
          bestSize = bc.baseSize;
          bestBase = bc.combo;
        }
      }
      // Also try base + add-ons for ≥2 coverage
      if (!bestBase) {
        for (auto &bc : bases) {
          if (!bc.isSubsetOfNote)
            continue;
          std::set<std::string> remaining;
          for (auto &t : techniques) {
            if (!bc.combo->techniqueIDs.count(t))
              remaining.insert(t);
          }
          auto addOns = findAddOnsCovering(remaining);
          int totalCovered = bc.baseSize;
          for (auto *a : addOns)
            totalCovered += (int)a->techniqueIDs.size();
          if (totalCovered >= 2 && totalCovered > bestOverlap) {
            bestOverlap = totalCovered;
            bestBase = bc.combo;
          }
        }
      }
      if (bestBase) {
        std::set<std::string> remaining;
        for (auto &t : techniques) {
          if (!bestBase->techniqueIDs.count(t))
            remaining.insert(t);
        }
        return {bestBase, findAddOnsCovering(remaining)};
      }
    }

    // ── Tier 4: Priority-based single-technique fallback ──────────
    // Pick the highest-priority technique from the note's set and
    // find the best base switch containing just that technique.
    {
      std::string bestPT;
      int bestPrio = -1;
      for (auto &t : techniques) {
        int p = techniquePriority(t);
        if (p > bestPrio) {
          bestPrio = p;
          bestPT = t;
        }
      }
      if (!bestPT.empty()) {
        const TechniqueCombination *bestBase = nullptr;
        int bestSize = -1;
        for (auto &bc : bases) {
          if (!bc.isSubsetOfNote)
            continue;
          if (bc.combo->techniqueIDs.count(bestPT) && bc.baseSize > bestSize) {
            bestSize = bc.baseSize;
            bestBase = bc.combo;
          }
        }
        if (bestBase) {
          std::set<std::string> remaining;
          for (auto &t : techniques) {
            if (!bestBase->techniqueIDs.count(t))
              remaining.insert(t);
          }
          return {bestBase, findAddOnsCovering(remaining)};
        }
      }
    }

    // ── Tier 5: Any subset base (most specific) ───────────────────
    {
      const TechniqueCombination *bestBase = nullptr;
      int bestSize = -1;
      for (auto &bc : bases) {
        if (!bc.isSubsetOfNote && bc.baseSize > bestSize) {
          // Not a subset — skip
        }
        if (bc.isSubsetOfNote && bc.baseSize > bestSize) {
          bestSize = bc.baseSize;
          bestBase = bc.combo;
        }
      }
      if (bestBase) {
        std::set<std::string> remaining;
        for (auto &t : techniques) {
          if (!bestBase->techniqueIDs.count(t))
            remaining.insert(t);
        }
        return {bestBase, findAddOnsCovering(remaining)};
      }
    }

    // ── Tier 6: Fall back to pt.natural ───────────────────────────
    // First look for pt.natural among condition-filtered bases
    for (auto &bc : bases) {
      if (bc.combo->techniqueIDs.count("pt.natural"))
        return {bc.combo, {}};
    }
    // Then look for an unconditional pt.natural in full list
    for (auto &c : combinations) {
      if (!c.isAddOn && c.techniqueIDs.count("pt.natural") &&
          c.condition.empty())
        return {&c, {}};
    }
    // Last resort: first base switch with empty technique set (Natural)
    for (auto &c : combinations) {
      if (!c.isAddOn && c.techniqueIDs.empty())
        return {&c, {}};
    }
    return {}; // no match at all
  }

  /// Convert a LengthCategory to a representative duration in seconds
  /// for evaluating NoteCondition thresholds.
  static double lengthCategoryToSeconds(LengthCategory cat) {
    switch (cat) {
    case LengthCategory::VeryShort: return 0.09;   // well below 0.1875
    case LengthCategory::Short:     return 0.28;   // between 0.1875 and 0.375
    case LengthCategory::Medium:    return 0.55;   // between 0.375 and 0.75
    case LengthCategory::Long:      return 1.1;    // between 0.75 and 1.5
    case LengthCategory::VeryLong:  return 10.0;   // well above 1.5
    }
    return 10.0;
  }

  // -----------------------------------------------------------------
  // Legacy helpers (kept for backward compat)
  // -----------------------------------------------------------------

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

private:
  /// Check if a is a subset of b.
  static bool isSubset(const std::set<std::string> &a,
                       const std::set<std::string> &b) {
    for (auto &x : a) {
      if (!b.count(x))
        return false;
    }
    return true;
  }

  /// Greedily find add-on switches whose PTs are subsets of the target set.
  std::vector<const TechniqueCombination *>
  findAddOnsCovering(const std::set<std::string> &target) const {
    std::vector<const TechniqueCombination *> result;
    std::set<std::string> remaining = target;
    // Greedy: pick the add-on that covers the most remaining PTs each round
    bool progress = true;
    while (!remaining.empty() && progress) {
      progress = false;
      const TechniqueCombination *bestAddOn = nullptr;
      int bestCover = 0;
      for (auto &c : combinations) {
        if (!c.isAddOn)
          continue;
        // Skip if already selected
        bool alreadyUsed = false;
        for (auto *r : result) {
          if (r == &c) {
            alreadyUsed = true;
            break;
          }
        }
        if (alreadyUsed)
          continue;
        // Count how many remaining PTs this add-on covers
        if (!isSubset(c.techniqueIDs, target))
          continue; // add-on must only contain PTs from the note
        int cover = 0;
        for (auto &t : c.techniqueIDs) {
          if (remaining.count(t))
            ++cover;
        }
        if (cover > bestCover) {
          bestCover = cover;
          bestAddOn = &c;
        }
      }
      if (bestAddOn) {
        result.push_back(bestAddOn);
        for (auto &t : bestAddOn->techniqueIDs)
          remaining.erase(t);
        progress = true;
      }
    }
    return result;
  }
};

} // namespace fiddle
