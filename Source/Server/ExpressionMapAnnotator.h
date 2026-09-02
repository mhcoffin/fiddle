#pragma once

#include "Annotator.h"
#include "ExpressionMapData.h"
#include "NoteInspection.h"
#include <map>

namespace fiddle {

/// Annotator that uses a parsed .doricolib ExpressionMapData to translate
/// a note's playback technique set into concrete MIDI instructions.
///
/// Implements the subset + specificity algorithm from docs/em-algorithm.md:
///   - Expand note PTs with MEG defaults
///   - Find base switches that are subsets of note PTs
///   - Filter by length condition
///   - Pick most specific base; greedily add add-ons
///
/// Results are cached by (PT set, LengthCategory) and the annotator
/// tracks the current active MatchResult to avoid redundant switch actions.
class ExpressionMapAnnotator : public Annotator {
public:
  explicit ExpressionMapAnnotator(std::shared_ptr<ExpressionMapData> em)
      : emData_(std::move(em)) {}

  std::string name() const override {
    return emData_ ? emData_->name : "ExpressionMap";
  }

  const AnnotationRecord *lastAnnotationRecord() const override {
    return &lastRecord_;
  }

  /// Suppress CCs that are consumed by the Fiddle pipeline:
  ///   - CC1: dynamics → stored on the Note, re-expressed via emitDynamics()
  ///   - CC102–CC119: Dorico dimension encoding → consumed by NoteStreamTracker
  /// All other CCs are forwarded to the VST.
  bool onCC(const fiddle::MidiEvent &event,
            const AnnotatorContext &) override {
    if (!event.has_cc())
      return true;
    int ccNum = event.cc().controller_number();
    // CC1 = dynamics input — handled via Note's during_note path
    if (ccNum == 1)
      return false;
    // CC102–CC119 = Dorico dimension encoding — already consumed
    if (ccNum >= 102 && ccNum <= 119)
      return false;
    return true;
  }

  void onNoteStart(fiddle::Note &note,
                   const AnnotatorContext &ctx) override {
    lastRecord_ = makeIncomingNoteRecord(note, ctx.sampleRate,
                                         emData_ != nullptr);
    if (!emData_)
      return;

    // ── 1. Collect the note's playback techniques ──────────────────
    const auto &techniqueIDs = lastRecord_.inputTechniques;

    // NOTE: no expandWithDefaults — the 6-tier resolver works on raw PTs.
    // MEG expansion was removed because it breaks EMs where absence of
    // a technique is semantically meaningful (e.g. VSL vibrato).

    // ── 2. Determine note length category ──────────────────────────
    const auto lengthCat = lastRecord_.lengthCategory;

    // ── 3. Resolve match (cached by PT set + length) ──────────────
    std::vector<CandidateInfo> candidates;
    auto match = resolveWithCache(techniqueIDs, lengthCat, &candidates);
    if (match.empty())
      return;

    lastRecord_.candidates = std::move(candidates);

    // Ensure the winning base always appears as "eligible" in the UI,
    // even if it won via a fallback tier that bypasses the subset check
    // (e.g. Tier 6 Natural fallback when the note has no PTs).
    if (match.base) {
      for (auto &c : lastRecord_.candidates) {
        if (c.name == match.base->name &&
            c.techniqueIDs == match.base->techniqueIDs) {
          c.isSubset = true;
          break;
        }
      }
    }

    // Record base switch info
    if (match.base) {
      lastRecord_.baseSwitchName = match.base->name;
      lastRecord_.baseTechniqueIDs = match.base->techniqueIDs;
    }
    // Record add-on names
    for (auto *a : match.addOns)
      lastRecord_.addOnNames.push_back(a->name);

    // Record unmatched techniques
    if (match.base) {
      for (auto &t : techniqueIDs) {
        if (!match.base->techniqueIDs.count(t)) {
          bool covered = false;
          for (auto *a : match.addOns) {
            if (a->techniqueIDs.count(t)) {
              covered = true;
              break;
            }
          }
          if (!covered)
            lastRecord_.unmatchedTechniques.insert(t);
        }
      }
    }

    // ── 3. Skip if same as currently active match ─────────────────
    bool needSend = (match != currentMatch_);
    currentMatch_ = match;
    lastRecord_.redundancySkipped = !needSend;

    // ── 4. Emit switch-on MIDI instructions ───────────────────────
    if (needSend) {
      auto actions = match.allSwitchOnActions();
      lastRecord_.midiActionsEmitted = actions;
      for (auto &sa : actions) {
        // NoteVelocity and Unknown are not discrete MIDI instructions
        if (sa.type == SwitchActionType::NoteVelocity ||
            sa.type == SwitchActionType::Unknown)
          continue;
        auto *inst = note.add_pre_note();
        actionToInstruction(sa, inst);
      }
    }

    // ── 5. Apply modifiers from the base switch ───────────────────
    lastRecord_.velocityBefore = note.start_velocity();
    if (match.base) {
      if (match.base->velocityFactor != 1.0f) {
        int v = note.start_velocity();
        v = std::clamp(static_cast<int>(v * match.base->velocityFactor), 1,
                       127);
        note.set_start_velocity(v);
      }
      if (match.base->transpose != 0) {
        int n = note.note_number() + match.base->transpose;
        note.set_note_number(std::clamp(n, 0, 127));
        lastRecord_.transposeApplied = match.base->transpose;
      }
    }
    lastRecord_.velocityAfter = note.start_velocity();

    // ── 6. Emit dynamics MIDI output ──────────────────────────────
    if (match.base) {
      // Record dynamics routing description
      auto inputMode = note.dynamics_mode();
      std::string inputStr =
          (inputMode == fiddle::Note::CC) ? "CC" : "velocity";
      auto fmtTarget = [](VolumeType vt, int cc) -> std::string {
        return (vt == VolumeType::kCC) ? ("CC" + std::to_string(cc))
                                       : "velocity";
      };
      lastRecord_.dynamicsRouting =
          inputStr + "→" + fmtTarget(match.base->volumeType, match.base->volumeCC);
      if (match.base->volumeType2 != match.base->volumeType ||
          (match.base->volumeType2 == VolumeType::kCC &&
           match.base->volumeCC2 != match.base->volumeCC)) {
        lastRecord_.dynamicsRouting +=
            " + " + fmtTarget(match.base->volumeType2, match.base->volumeCC2);
      }

      emitDynamics(note, *match.base);
    }
  }

  void onNoteEnd(fiddle::Note &note,
                 const AnnotatorContext &ctx) override {
    durationAdjustMs_ = 0.0;
    if (!emData_)
      return;

    auto techniqueIDs = collectInputTechniqueIds(note);

    // Recompute length category from written duration (note is now complete)
    LengthCategory lengthCat = LengthCategory::VeryLong;
    if (note.duration_samples() > 0 && ctx.sampleRate > 0) {
      double durationSec =
          static_cast<double>(note.duration_samples()) / ctx.sampleRate;
      lengthCat = classifyLength(durationSec);
    }

    auto match = resolveWithCache(techniqueIDs, lengthCat);

    // ── Emit switch-off MIDI instructions into post_note ──────────
    if (!match.empty()) {
      auto actions = match.allSwitchOffActions();
      for (auto &sa : actions) {
        if (sa.type == SwitchActionType::NoteVelocity ||
            sa.type == SwitchActionType::Unknown)
          continue;
        auto *inst = note.add_post_note();
        actionToInstruction(sa, inst);
      }
    }

    // ── Compute duration adjustment ───────────────────────────────
    // Layer 1: category-level duration percent from playbackOptionsOverrides
    int categoryPercent = selectCategoryPercent(techniqueIDs);

    // Layer 2: per-combination lengthFactor
    float lengthFactor = 1.0f;
    if (match.base)
      lengthFactor = match.base->lengthFactor;

    // Compose the two layers
    double combinedFactor =
        (static_cast<double>(categoryPercent) / 100.0) *
        static_cast<double>(lengthFactor);

    // Compute timing delta in ms
    if (combinedFactor != 1.0 && note.duration_samples() > 0 &&
        ctx.sampleRate > 0) {
      double durationMs = static_cast<double>(note.duration_samples()) /
                          ctx.sampleRate * 1000.0;
      durationAdjustMs_ = (combinedFactor - 1.0) * durationMs;
    }

    // Record duration adjustment in the annotation record
    lastRecord_.durationAdjustMs = durationAdjustMs_;
  }

  double durationAdjustMs() const override { return durationAdjustMs_; }

  void resetState() override {
    cache_.clear();
    currentMatch_ = {};
  }

private:
  std::shared_ptr<ExpressionMapData> emData_;
  MatchResult currentMatch_;
  double durationAdjustMs_ = 0.0;
  AnnotationRecord lastRecord_;

  // Cache key: (PT set, LengthCategory)
  using CacheKey = std::pair<std::set<std::string>, LengthCategory>;
  std::map<CacheKey, MatchResult> cache_;

  /// Resolve with cache. Optionally populates candidates on cache miss.
  MatchResult resolveWithCache(const std::set<std::string> &techniques,
                               LengthCategory lengthCat = LengthCategory::VeryLong,
                               std::vector<CandidateInfo> *outCandidates = nullptr) {
    CacheKey key{techniques, lengthCat};
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      // On cache hit, re-run only when caller asks for candidate info.
      if (outCandidates) {
        auto result = emData_->resolveMatch(techniques, lengthCat, outCandidates);
        return result;
      }
      return it->second;
    }
    auto result = emData_->resolveMatch(techniques, lengthCat, outCandidates);
    cache_[key] = result;
    return result;
  }

  /// Select the category-level duration percent from TimingOptions based on
  /// the note's playback techniques.  Priority: staccatissimo > staccato >
  /// legato > tenuto > marcato > default (noteDurationPercent).
  int selectCategoryPercent(const std::set<std::string> &techniques) const {
    if (!emData_)
      return 100;
    const auto &t = emData_->timingOptions;
    if (techniques.count("pt.staccatissimo"))
      return t.staccatissimoDurationPercent;
    if (techniques.count("pt.staccato"))
      return t.staccatoDurationPercent;
    if (techniques.count("pt.legato"))
      return t.legatoDurationPercent;
    if (techniques.count("pt.tenuto"))
      return t.tenutoDurationPercent;
    if (techniques.count("pt.marcato"))
      return t.marcatoDurationPercent;
    return t.noteDurationPercent;
  }

  // filterByCondition removed — conditions are now handled directly
  // in resolveMatch() via the LengthCategory parameter.

  /// Convert a SwitchAction to a MidiInstruction proto.
  static void actionToInstruction(const SwitchAction &sa,
                                  fiddle::MidiInstruction *inst) {
    switch (sa.type) {
    case SwitchActionType::KeySwitch:
      inst->set_type(fiddle::MidiInstruction::KEY_SWITCH);
      inst->set_param1(sa.param1);
      inst->set_param2(sa.param2 > 0 ? sa.param2 : 100);
      inst->set_delay_ms(-5); // keyswitches arrive slightly before the note
      break;
    case SwitchActionType::CC:
    case SwitchActionType::ControlChange:
      inst->set_type(fiddle::MidiInstruction::CC);
      inst->set_param1(sa.param1);
      inst->set_param2(sa.param2);
      break;
    case SwitchActionType::ProgramChange:
      inst->set_type(fiddle::MidiInstruction::PROGRAM_CHANGE);
      inst->set_param1(sa.param1);
      break;
    case SwitchActionType::ChannelSwitch:
      inst->set_type(fiddle::MidiInstruction::CC);
      inst->set_channel(sa.param1);
      break;
    case SwitchActionType::NoteVelocity:
      // Velocity is a modifier, not a discrete MIDI instruction
      break;
    case SwitchActionType::Unknown:
      break;
    }
  }
  /// Emit dynamics MIDI for a note based on the matched switch's dual
  /// dynamics model:
  ///   volumeType  = primary dynamics (attack / velocity)
  ///   volumeType2 = secondary dynamics (sustain / CC)
  /// Both are processed independently.
  static void emitDynamics(fiddle::Note &note,
                           const TechniqueCombination &combo) {
    auto inputMode = note.dynamics_mode();

    // ── Primary dynamics (volumeType) ────────────────────────────
    emitDynamicsForType(note, inputMode, combo.volumeType, combo.volumeCC);

    // ── Secondary dynamics (volumeType2) ─────────────────────────
    // Only process if volumeType2 differs from volumeType to avoid
    // double-emitting the same CC or velocity.
    if (combo.volumeType2 != combo.volumeType ||
        (combo.volumeType2 == VolumeType::kCC &&
         combo.volumeCC2 != combo.volumeCC)) {
      emitDynamicsForType(note, inputMode, combo.volumeType2, combo.volumeCC2);
    }
  }

  /// Emit dynamics for a single volume type slot.
  static void emitDynamicsForType(fiddle::Note &note,
                                  fiddle::Note::DynamicsMode inputMode,
                                  VolumeType targetType, int targetCC) {
    if (targetType == VolumeType::kNoteVelocity) {
      // Target expects velocity — set the note's start_velocity.
      if (inputMode == fiddle::Note::CC) {
        // Input was CC: convert initial CC value to velocity.
        auto it = note.cc_automation().find(1);
        if (it != note.cc_automation().end() &&
            it->second.points_size() > 0) {
          note.set_start_velocity(
              std::clamp(static_cast<int>(it->second.points(0).value()),
                         1, 127));
        }
      }
      // If input was velocity → target is velocity: nothing to do.
    } else {
      // Target expects CC (e.g., CC1)
      if (inputMode == fiddle::Note::CC) {
        // Input was CC1: copy automation curve to during_note instructions.
        auto it = note.cc_automation().find(1);
        if (it != note.cc_automation().end()) {
          for (int i = 0; i < it->second.points_size(); ++i) {
            auto &pt = it->second.points(i);
            auto *inst = note.add_during_note();
            inst->set_type(fiddle::MidiInstruction::CC);
            inst->set_param1(targetCC);
            inst->set_param2(std::clamp(static_cast<int>(pt.value()), 0, 127));
            // Convert sample offset to ms (assuming 44100 for now)
            inst->set_delay_ms(
                static_cast<int>(pt.offset_samples() * 1000 / 44100));
          }
        }
      } else {
        // Input was velocity → target expects CC:
        // emit a single CC at note start with value = velocity.
        auto *inst = note.add_during_note();
        inst->set_type(fiddle::MidiInstruction::CC);
        inst->set_param1(targetCC);
        inst->set_param2(
            std::clamp(static_cast<int>(note.start_velocity()), 0, 127));
        inst->set_delay_ms(0);
      }
    }
  }
};

} // namespace fiddle
