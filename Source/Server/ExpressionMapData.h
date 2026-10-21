#pragma once



#include "DoricoPlaybackOptions.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace fiddle {

/// Scoring info for a single candidate base switch (used in Tier 3 reporting).
struct CandidateInfo {
  std::string name;
  int score = 0;
  std::set<std::string> techniqueIDs;
  bool isSubset = false; // true when candidate's PTs ⊆ note's PTs
  std::string conditionString; // length condition e.g. "NoteLength < 0.375"
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
  enum class Logic { All, Any };
  Logic logic = Logic::All;
  std::vector<NoteCondition> terms;
  bool valid = true;

  bool empty() const { return valid && op == Operator::None && terms.empty(); }

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
    if (!valid)
      return false;
    if (!terms.empty()) {
      for (const auto &term : terms) {
        const bool matches = term.evaluate(durationSeconds);
        if (logic == Logic::All && !matches) return false;
        if (logic == Logic::Any && matches) return true;
      }
      return logic == Logic::All;
    }
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

/// Parse whitespace-free length comparisons with AND/OR (AND binds tighter).
/// Unsupported/malformed length expressions fail closed, never becoming an
/// unconditional switch or borrowing a threshold from another clause.
inline NoteCondition parseCompactLengthCondition(const std::string &s) {
  NoteCondition nc;
  for (const std::string separator : {"OR", "AND"}) {
    if (s.find(separator) == std::string::npos) continue;
    nc.logic = separator == "OR" ? NoteCondition::Logic::Any : NoteCondition::Logic::All;
    size_t start = 0;
    while (true) {
      const auto end = s.find(separator, start);
      auto term = parseCompactLengthCondition(s.substr(start, end - start));
      nc.valid = nc.valid && term.valid;
      nc.terms.push_back(std::move(term));
      if (end == std::string::npos) break;
      start = end + separator.size();
    }
    return nc;
  }
  if (s.compare(0, 10, "NoteLength") != 0) {
    nc.valid = false;
    return nc;
  }
  auto comparison = s.substr(10);
  size_t operatorSize = 2;
  if      (comparison.compare(0, 2, ">=") == 0) nc.op = NoteCondition::Operator::GE;
  else if (comparison.compare(0, 2, "<=") == 0) nc.op = NoteCondition::Operator::LE;
  else if (comparison.compare(0, 2, "==") == 0) nc.op = NoteCondition::Operator::EQ;
  else {
    operatorSize = 1;
    if      (comparison.compare(0, 1, ">") == 0) nc.op = NoteCondition::Operator::GT;
    else if (comparison.compare(0, 1, "<") == 0) nc.op = NoteCondition::Operator::LT;
    else { nc.valid = false; return nc; }
  }
  const auto threshold = comparison.substr(operatorSize);
  if      (threshold == "kVeryShort") nc.threshold = NoteCondition::Threshold::VeryShort;
  else if (threshold == "kVeryLong") nc.threshold = NoteCondition::Threshold::VeryLong;
  else if (threshold == "kShort") nc.threshold = NoteCondition::Threshold::Short;
  else if (threshold == "kMedium") nc.threshold = NoteCondition::Threshold::Medium;
  else if (threshold == "kLong") nc.threshold = NoteCondition::Threshold::Long;
  else nc.valid = false;
  return nc;
}

inline NoteCondition parseConditionString(const std::string &s) {
  // Preserve the existing treatment of empty/non-length conditions.
  if (s.find("NoteLength") == std::string::npos) return {};
  std::string compact;
  for (unsigned char c : s)
    if (!std::isspace(c)) compact += static_cast<char>(c);
  return parseCompactLengthCondition(compact);
}

/// Note-duration percentages from the expression map's
/// <playbackOptionsOverrides>. A category without an override uses the
/// project's Dorico playback option.
struct TimingOptions {
  std::array<std::optional<int>, kDurationCategoryCount> overrides;
  /// The map's timingOptions.numTicksBeforeForSwitches, if it overrides it.
  std::optional<int> switchLeadTicks;

  std::optional<int> &operator[](DurationCategory c) { return overrides[size_t(c)]; }
  const std::optional<int> &operator[](DurationCategory c) const { return overrides[size_t(c)]; }

  /// This map's percent of written length for a category.
  int percent(DurationCategory c, const DurationPercents &project) const {
    return (*this)[c].value_or(project[size_t(c)]);
  }

  /// Factor from the received duration to the one Dorico would play with
  /// this map, for the category that applies to `techniques`.
  ///  - writtenDuration (bridge pins Dorico's options to 100%): the received
  ///    duration is the written length, so apply this map's percent.
  ///  - otherwise Dorico already applied the project's option: only an
  ///    override changes it, by override / project option.
  double durationCorrection(const std::set<std::string> &techniques,
                            const DurationPercents &project,
                            bool writtenDuration) const {
    const auto c = durationCategory(techniques);
    const double target = percent(c, project);
    if (writtenDuration)
      return target / 100.0;
    return target / std::max(1, project[size_t(c)]);
  }
};

/// The dynamics options this map's <playbackOptionsOverrides> set; the rest
/// come from the project's copy of Dorico's options.
struct DynamicsOverrides {
  std::optional<std::array<double, 3>> curve; // midpoint, lower, upper power
  std::optional<int> minLevel, maxLevel;
  std::optional<double> accent, marcato, tenuto, ghost;

  DoricoDynamics over(DoricoDynamics project) const {
    if (curve) {
      project.midpoint = (*curve)[0];
      project.lowerPower = (*curve)[1];
      project.upperPower = (*curve)[2];
    }
    project.minLevel = minLevel.value_or(project.minLevel);
    project.maxLevel = maxLevel.value_or(project.maxLevel);
    project.accent = accent.value_or(project.accent);
    project.marcato = marcato.value_or(project.marcato);
    project.tenuto = tenuto.value_or(project.tenuto);
    project.ghost = ghost.value_or(project.ghost);
    return project;
  }
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

  // Timing. millisecondsBefore: how much earlier than Dorico's position the
  // notes this switch plays (with its switch actions) are sent; the end of
  // the note and live controllers move only when the corresponding flag is
  // set. ticksBefore: how much earlier the switch actions alone are sent
  // (Dorico's per-switch form of timingOptions.numTicksBeforeForSwitches).
  int ticksBefore = 0;
  int millisecondsBefore = 0;
  bool applyMillisecondsBeforeToEndOffsets = false;
  bool applyMillisecondsBeforeToControllers = false;

  // Conditions (stored but not evaluated yet)
  std::string conditionString;
  NoteCondition condition; // parsed from conditionString

  // Modifiers
  float velocityFactor = 1.0f;
  float lengthFactor = 1.0f;
  // Dorico's monophonic legato handling is not emulated: Fiddle plays the
  // overlaps Dorico sent through the bridge unchanged.
  bool monophonic = false;

  // Ranges: a base switch only applies to notes inside both.
  int velocityMin = 1, velocityMax = 127;
  int pitchMin = 0, pitchMax = 127;
  int transpose = 0;

  /// Negative values mean "not known", which every range admits.
  bool admits(int velocity, int pitch) const {
    return (velocity < 0 || (velocity >= velocityMin && velocity <= velocityMax)) &&
           (pitch < 0 || (pitch >= pitchMin && pitch <= pitchMax));
  }
  bool hasRangeLimits() const {
    return velocityMin > 1 || velocityMax < 127 || pitchMin > 0 || pitchMax < 127;
  }

  // Dynamics: how the target VST receives volume for this technique
  //   volumeType  = primary dynamics (attack / velocity)
  //   volumeType2 = secondary dynamics (sustain / CC1)
  VolumeType volumeType = VolumeType::kNoteVelocity;
  int volumeCC = 1; // CC number when volumeType == kCC (usually 1)
  VolumeType volumeType2 = VolumeType::kNoteVelocity; // secondary dynamics
  int volumeCC2 = 1; // CC number when volumeType2 == kCC
  bool hasVolumeType2 = false; // the map declares a <volumeType2>
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

  /// The base followed by its add-ons, in action order.
  std::vector<const TechniqueCombination *> components() const {
    std::vector<const TechniqueCombination *> result;
    if (base)
      result.push_back(base);
    result.insert(result.end(), addOns.begin(), addOns.end());
    return result;
  }

  /// 1-based MIDI channel selected by a kChannelSwitch action (the last one
  /// wins), or 0 when the note stays on its input channel.
  int switchChannel() const {
    int channel = 0;
    for (auto *c : components())
      for (const auto &a : c->switchOnActions)
        if (a.type == SwitchActionType::ChannelSwitch && a.param1 >= 1 && a.param1 <= 16)
          channel = a.param1;
    return channel;
  }

  /// Dorico's tick resolution for ticksBefore (assumed: 480 per quarter).
  static constexpr double kTicksPerQuarter = 480.0;

  /// How much earlier (ms) than Dorico's position the note starts: the
  /// largest millisecondsBefore of the selected switches.
  double noteLeadMs() const {
    int lead = 0;
    for (auto *c : components())
      lead = std::max(lead, c->millisecondsBefore);
    return lead;
  }

  /// How much earlier (ms) than the note its switch actions go, at
  /// `tempoBpm`: each selected switch's own ticksBefore, else `defaultTicks`
  /// (the map's or project's "switches before notes"); the largest wins.
  double switchLeadMs(double tempoBpm, int defaultTicks) const {
    int ticks = 0;
    for (auto *c : components())
      ticks = std::max(ticks, c->ticksBefore > 0 ? c->ticksBefore : defaultTicks);
    return ticks > 0 && tempoBpm > 0.0 ? ticks * 60000.0 / (tempoBpm * kTicksPerQuarter) : 0.0;
  }

  /// Whether the lead also moves the note's end / live controllers.
  bool leadAppliesToEnd() const {
    for (auto *c : components())
      if (c->applyMillisecondsBeforeToEndOffsets) return true;
    return false;
  }
  bool leadAppliesToControllers() const {
    for (auto *c : components())
      if (c->applyMillisecondsBeforeToControllers) return true;
    return false;
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

  /// Dynamics playback options this map overrides.
  DynamicsOverrides dynamicsOverrides;

  /// Actions from an enabled <initSwitchData>, sent when playback starts.
  std::vector<SwitchAction> initActions;

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
  /// Canonical built-in IDs (docs/dorico-built-in-technique-groups.md) plus
  /// the lowercase spellings older Fiddle maps used.
  static int techniquePriority(const std::string &pt) {
    static const std::set<std::string> highest{
        // Harmonics
        "pt.naturalHarmonic1", "pt.naturalHarmonic2", "pt.artificialHarmonic",
        "pt.halfHarmonic", "pt.harpHarmonic", "pt.pinchHarmonic",
        // Pizzicato
        "pt.pizzicato", "pt.bartoksnapPizzicato", "pt.buzzPizzicato",
        "pt.guitarPizzicato", "pt.nailPizzicato", "pt.pizzicatoAllaChitarra",
        "pt.pizzicatoSecco", "pt.lhPizzicato",
        // Legacy spellings
        "pt.harmonic", "pt.naturalharmonic", "pt.artificialharmonic",
        "pt.bartokpizzicato", "pt.snappizzicato"};
    static const std::set<std::string> mutes{
        "pt.muted", "pt.halfMuted", "pt.bellMute", "pt.bucketMute", "pt.cupMute",
        "pt.harmonMuteStemExtended", "pt.harmonMuteStemIn", "pt.harmonMuteStemOut",
        "pt.plungerMute", "pt.straightMute",
        // Legacy spellings
        "pt.cupmute", "pt.straightmute", "pt.harmonicmute", "pt.wawamute",
        "pt.harmonmute", "pt.bucketmute", "pt.plungermute"};
    if (highest.count(pt))
      return 100;
    if (mutes.count(pt))
      return 50;
    return 10; // everything else (legato, staccato, vibrato, etc.)
  }

  /// Resolve a note's playback techniques to a base switch + add-ons.
  /// Uses 6-tier cascading resolution on the **raw** technique set (no
  /// MEG default expansion).
  /// `lengthCat` filters bases with length conditions; `velocity` and
  /// `pitch` (when not negative) filter bases by their velocity/pitch range.
  /// When `outCandidates` is non-null, receives all base candidates considered.
  MatchResult resolveMatch(const std::set<std::string> &techniques,
                           LengthCategory lengthCat = LengthCategory::VeryLong,
                           std::vector<CandidateInfo> *outCandidates = nullptr,
                           int velocity = -1, int pitch = -1) const {
    double repDuration = lengthCategoryToSeconds(lengthCat);
    const auto eligible = [&](const TechniqueCombination &c) {
      return (c.condition.empty() || c.condition.evaluate(repDuration)) &&
             c.admits(velocity, pitch);
    };

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
      if (c.isAddOn || !eligible(c))
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
            {c.name, overlap, c.techniqueIDs, sub, c.conditionString});
    }

    // ── Tiers 1–2: exact rendition, base alone or base + add-ons ──
    if (auto exact = exactMatch(techniques, repDuration, velocity, pitch); !exact.empty())
      return exact;
    // Natural is Dorico's implicit default, not a sound to preserve when a
    // more specific switch exists: a bridge sends {natural, legato} where a
    // library maps Legato as its own base. Retry the exact tiers without it.
    if (techniques.count("pt.natural") && techniques.size() > 1) {
      auto specific = techniques;
      specific.erase("pt.natural");
      if (auto exact = exactMatch(specific, repDuration, velocity, pitch); !exact.empty())
        return exact;
    }

    // ── Tier 3: Partial match (≥3 PTs, covering ≥2) ──────────────
    if (techniques.size() >= 3) {
      const TechniqueCombination *bestBase = nullptr;
      int bestOverlap = -1;
      int bestSize = -1;
      int bestSpecific = -1;
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
          // Ties prefer the base that renders more non-Natural intent.
          const int specific = totalCovered - (int)bc.combo->techniqueIDs.count("pt.natural");
          if (totalCovered >= 2 && (totalCovered > bestOverlap
                                    || (totalCovered == bestOverlap && specific > bestSpecific))) {
            bestOverlap = totalCovered;
            bestSpecific = specific;
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
    // Natural is implicit in incoming notes. Falling back to it must not
    // discard independently supported techniques such as Con Sordino.
    const auto withAddOns = [&](const TechniqueCombination *base) -> MatchResult {
      auto remaining = techniques;
      for (const auto &t : base->techniqueIDs) remaining.erase(t);
      return {base, findAddOnsCovering(remaining)};
    };
    // First look for pt.natural among condition-filtered bases
    for (auto &bc : bases) {
      if (bc.combo->techniqueIDs.count("pt.natural"))
        return withAddOns(bc.combo);
    }
    // Then look for an unconditional pt.natural in full list
    for (auto &c : combinations) {
      if (!c.isAddOn && c.techniqueIDs.count("pt.natural") &&
          c.condition.empty())
        return withAddOns(&c);
    }
    // Last resort: first base switch with empty technique set (Natural)
    for (auto &c : combinations) {
      if (!c.isAddOn && c.techniqueIDs.empty() && c.condition.empty())
        return withAddOns(&c);
    }
    return {}; // no match at all
  }

  /// Tiers 1–2 for one request: an exact base, else the most specific subset
  /// base whose remainder add-ons cover exactly. Every subset base is tried,
  /// so an equal-size base earlier in the file cannot hide an exact one.
  MatchResult exactMatch(const std::set<std::string> &request, double repDuration,
                         int velocity = -1, int pitch = -1) const {
    std::vector<const TechniqueCombination *> subsets;
    for (auto &c : combinations) {
      if (c.isAddOn || (!c.condition.empty() && !c.condition.evaluate(repDuration)) ||
          !c.admits(velocity, pitch))
        continue;
      if (!isSubset(c.techniqueIDs, request))
        continue;
      if (c.techniqueIDs.size() == request.size())
        return {&c, {}};
      subsets.push_back(&c);
    }
    std::stable_sort(subsets.begin(), subsets.end(), [](auto *a, auto *b) {
      return a->techniqueIDs.size() > b->techniqueIDs.size();
    });
    for (auto *base : subsets) {
      std::set<std::string> remaining;
      for (auto &t : request)
        if (!base->techniqueIDs.count(t)) remaining.insert(t);
      auto addOns = findAddOnsCovering(remaining);
      std::set<std::string> covered;
      for (auto *a : addOns) covered.insert(a->techniqueIDs.begin(), a->techniqueIDs.end());
      if (isSubset(remaining, covered))
        return {base, addOns};
    }
    return {};
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
