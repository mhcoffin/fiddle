#pragma once

#include "Annotator.h"
#include "ExpressionMapData.h"
#include "NoteInspection.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

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
/// Results are cached by (PT set, LengthCategory[, velocity, pitch]) and the
/// annotator tracks the current active MatchResult to avoid redundant switch
/// actions; Off actions are sent when that match changes away from a switch.
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
  ///
  /// While a kChannelSwitch is active every controller is re-sent on that
  /// channel through translateCC(), as Dorico sends it with the notes.
  bool onCC(const fiddle::MidiEvent &event,
            const AnnotatorContext &) override {
    // Dorico bends for the bridge map's ±2 semitones; rescale for a layer
    // map that declares another instrument range.
    if (event.has_pitch_bend())
      return activeChannel_ == 0 &&
             (!emData_ || emData_->pitchBendRange == kInputBendRange || emData_->pitchBendRange <= 0);
    if (!event.has_cc())
      return true;
    int ccNum = event.cc().controller_number();
    // CC1 = dynamics input — handled via Note's during_note path
    if (ccNum == 1)
      return false;
    // CC102–CC119 = Dorico dimension encoding — already consumed
    if (isDimensionCC(ccNum))
      return false;
    return activeChannel_ == 0;
  }

  /// Live CC1 dynamics while notes sound: re-express each change on the
  /// controller(s) the current base switch expects. Without this a hairpin
  /// over a held note never reached the instrument; note start only carried
  /// the value current at that moment.
  void translateCC(const fiddle::MidiEvent &event, const AnnotatorContext &,
                   std::vector<fiddle::MidiInstruction> &out) override {
    if (event.has_pitch_bend()) {
      int value = static_cast<int>(event.pitch_bend().value());
      if (emData_ && emData_->pitchBendRange > 0) {
        const double semitones = (value - 8192) * kInputBendRange / 8192.0;
        value = std::clamp(8192 + int(std::lround(semitones / emData_->pitchBendRange * 8192.0)), 0, 16383);
      }
      fiddle::MidiInstruction inst;
      inst.set_type(fiddle::MidiInstruction::PITCH_BEND);
      inst.set_channel(activeChannel_);
      inst.set_param1(value >> 7);
      inst.set_param2(value & 0x7f);
      out.push_back(inst);
      return;
    }
    if (!event.has_cc())
      return;
    const int ccNum = static_cast<int>(event.cc().controller_number());
    const int value = std::clamp(static_cast<int>(event.cc().controller_value()), 0, 127);
    const auto emit = [&](int controller) {
      fiddle::MidiInstruction inst;
      inst.set_type(fiddle::MidiInstruction::CC);
      inst.set_channel(activeChannel_);
      inst.set_param1(controller);
      inst.set_param2(value);
      out.push_back(inst);
    };
    if (ccNum == 1) {
      for (const int target : liveDynamicsCCs_)
        emit(target);
    } else if (activeChannel_ != 0 && !isDimensionCC(ccNum)) {
      emit(ccNum);
    }
  }

  double noteStartOffsetMs() const override { return noteStartOffsetMs_; }
  double controllerOffsetMs() const override { return controllerOffsetMs_; }

  /// Playback starts from an unknown instrument state: send the map's
  /// initialization actions and make the first note send its switch again.
  void onTransportStart(const AnnotatorContext &,
                        std::vector<fiddle::MidiInstruction> &out) override {
    clearPlaybackState();
    if (!emData_)
      return;
    for (const auto &sa : emData_->initActions) {
      if (!emitsMidi(sa))
        continue;
      out.emplace_back();
      actionToInstruction(sa, &out.back(), 0);
    }
  }

  void onNoteStart(fiddle::Note &note,
                   const AnnotatorContext &ctx) override {
    lastRecord_ = makeIncomingNoteRecord(note, ctx.sampleRate,
                                         emData_ != nullptr);
    noteStartOffsetMs_ = 0.0;
    if (!emData_)
      return;
    const auto key = noteKey(note);

    // ── 1. Collect the note's playback techniques ──────────────────
    const auto &techniqueIDs = lastRecord_.inputTechniques;

    // NOTE: no expandWithDefaults — the 6-tier resolver works on raw PTs.
    // MEG expansion was removed because it breaks EMs where absence of
    // a technique is semantically meaningful (e.g. VSL vibrato).

    // ── 2. Determine note length category ──────────────────────────
    const auto lengthCat = lastRecord_.lengthCategory;

    // ── 3. Resolve match (cached by PT set + length [+ ranges]) ───
    // Velocity/pitch ranges are checked against Dorico's note, before this
    // map's velocity factor or transpose.
    std::vector<CandidateInfo> candidates;
    auto match = resolveWithCache(techniqueIDs, lengthCat, &candidates,
                                  static_cast<int>(note.start_velocity()),
                                  static_cast<int>(note.note_number()));
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
    const auto previous = currentMatch_;
    currentMatch_ = match;
    lastRecord_.redundancySkipped = !needSend;

    // A kChannelSwitch routes the note, its switch actions and its
    // controllers to that channel, as Dorico does.
    const int switchChannel = match.switchChannel();
    activeChannel_ = switchChannel;

    // ── 4. Emit switch MIDI: Off for what is left, then On ────────
    // Dorico sends a switch's Off actions when the active switch changes
    // away from it, not after each of its notes. ticksBefore sends them
    // ahead of the note.
    const int switchDelayMs = -static_cast<int>(std::lround(match.switchLeadMs(ctx.tempoBpm)));
    const int firstAction = note.pre_note_size();
    if (needSend) {
      const auto incoming = match.components();
      for (auto *old : previous.components()) {
        if (std::find(incoming.begin(), incoming.end(), old) != incoming.end())
          continue;
        for (const auto &sa : old->switchOffActions)
          if (emitsMidi(sa))
            actionToInstruction(sa, note.add_pre_note(), previous.switchChannel());
      }
      auto actions = match.allSwitchOnActions();
      lastRecord_.midiActionsEmitted = actions;
      for (auto &sa : actions) {
        // NoteVelocity, ChannelSwitch and Unknown send no message themselves
        if (emitsMidi(sa))
          actionToInstruction(sa, note.add_pre_note(), switchChannel);
      }
    }
    for (int i = firstAction; i < note.pre_note_size(); ++i)
      note.mutable_pre_note(i)->set_delay_ms(switchDelayMs);
    if (switchChannel != 0)
      note.set_channel(switchChannel);

    // millisecondsBefore: the note and its switch actions sound this much
    // earlier than Dorico placed them.
    const double leadMs = match.noteLeadMs();
    noteStartOffsetMs_ = -leadMs;
    controllerOffsetMs_ = match.leadAppliesToControllers() ? -leadMs : 0.0;

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

      emitDynamics(note, *match.base, ctx.sampleRate);
      liveDynamicsCCs_ = liveDynamicsTargets(note, *match.base);
    }

    // The note-off must reach the channel and pitch this note-on used, even
    // if a later note has changed the active switch.
    sounding_[key] = {static_cast<int>(note.channel()),
                      static_cast<int>(note.note_number()),
                      match.base ? match.base->lengthFactor : 1.0f,
                      match.leadAppliesToEnd() ? leadMs : 0.0};
  }

  void onNoteEnd(fiddle::Note &note,
                 const AnnotatorContext &ctx) override {
    durationAdjustMs_ = 0.0;
    if (!emData_) {
      // Without a map, a written-length note still gets the project option.
      durationAdjustMs_ = scaledDurationAdjustMs(
          note, TimingOptions{}.durationCorrection(collectInputTechniqueIds(note),
                                                   ctx.doricoDurationPercents,
                                                   hasWrittenDuration(note)),
          ctx.sampleRate);
      return;
    }

    auto techniqueIDs = collectInputTechniqueIds(note);

    // Switch Off actions are sent when the active switch changes (see
    // onNoteStart), so a note ending never emits them: an overlapping
    // successor keeps its switch.
    float lengthFactor = 1.0f;
    double endLeadMs = 0.0;
    if (auto it = sounding_.find(noteKey(note)); it != sounding_.end()) {
      note.set_channel(static_cast<uint32_t>(it->second.channel));
      note.set_note_number(static_cast<uint32_t>(it->second.noteNumber));
      lengthFactor = it->second.lengthFactor;
      endLeadMs = it->second.endLeadMs;
      sounding_.erase(it);
    } else {
      // Use the same written-length category as note-on. A staccato note's
      // performed duration can be much shorter and must not select another
      // length-conditioned switch. Without bridge metadata, the shared
      // helper still falls back to the completed duration.
      auto match = resolveWithCache(techniqueIDs, inputLengthCategory(note, ctx.sampleRate));
      if (match.base)
        lengthFactor = match.base->lengthFactor;
    }

    // ── Compute duration adjustment ───────────────────────────────
    // Layer 1: this map's note-duration percentages (its overrides, else
    //          the project's Dorico options), from the written length when
    //          the bridge pins Dorico's options to 100%, else relative to the
    //          project option Dorico already applied
    // Layer 2: per-combination lengthFactor
    const double combinedFactor =
        emData_->timingOptions.durationCorrection(techniqueIDs, ctx.doricoDurationPercents,
                                                  hasWrittenDuration(note)) *
        static_cast<double>(lengthFactor);
    durationAdjustMs_ = scaledDurationAdjustMs(note, combinedFactor, ctx.sampleRate);
    // A lead applied to end offsets moves the note-off with the note-on.
    durationAdjustMs_ -= endLeadMs;

    // Record duration adjustment in the annotation record
    lastRecord_.durationAdjustMs = durationAdjustMs_;
  }

  double durationAdjustMs() const override { return durationAdjustMs_; }

  void resetState() override {
    cache_.clear();
    clearPlaybackState();
  }

private:
  static constexpr int kInputBendRange = 2; // bridge maps require pitchBendRange 2
  std::shared_ptr<ExpressionMapData> emData_;
  MatchResult currentMatch_;
  std::vector<int> liveDynamicsCCs_; // targets for live CC1, from the last base
  int activeChannel_ = 0; // kChannelSwitch channel of the last note, or 0
  double durationAdjustMs_ = 0.0;
  double noteStartOffsetMs_ = 0.0;
  double controllerOffsetMs_ = 0.0;
  AnnotationRecord lastRecord_;

  /// What a sounding note's note-off needs from its note-on.
  struct SoundingNote {
    int channel = 0;
    int noteNumber = 0;
    float lengthFactor = 1.0f;
    double endLeadMs = 0.0;
  };
  using NoteKey = std::tuple<uint64_t, uint32_t, uint32_t>;
  std::map<NoteKey, SoundingNote> sounding_;

  /// Identity of Dorico's note (before this map changes channel or pitch).
  static NoteKey noteKey(const fiddle::Note &note) {
    return {note.id(), note.channel(), note.note_number()};
  }

  void clearPlaybackState() {
    currentMatch_ = {};
    liveDynamicsCCs_.clear();
    activeChannel_ = 0;
    controllerOffsetMs_ = 0.0;
    sounding_.clear();
  }

  static bool isDimensionCC(int cc) { return cc >= 102 && cc <= 119; }

  // Cache key: (PT set, LengthCategory, velocity, pitch). Velocity and pitch
  // are -1 unless the map restricts a switch's ranges.
  using CacheKey = std::tuple<std::set<std::string>, LengthCategory, int, int>;
  std::map<CacheKey, MatchResult> cache_;

  /// Resolve with cache. Optionally populates candidates on cache miss.
  MatchResult resolveWithCache(const std::set<std::string> &techniques,
                               LengthCategory lengthCat = LengthCategory::VeryLong,
                               std::vector<CandidateInfo> *outCandidates = nullptr,
                               int velocity = -1, int pitch = -1) {
    if (!hasRangeLimits())
      velocity = pitch = -1;
    CacheKey key{techniques, lengthCat, velocity, pitch};
    auto it = cache_.find(key);
    if (it != cache_.end()) {
      // On cache hit, re-run only when caller asks for candidate info.
      if (outCandidates)
        return emData_->resolveMatch(techniques, lengthCat, outCandidates, velocity, pitch);
      return it->second;
    }
    auto result = emData_->resolveMatch(techniques, lengthCat, outCandidates, velocity, pitch);
    cache_[key] = result;
    return result;
  }

  bool hasRangeLimits() const {
    return std::any_of(emData_->combinations.begin(), emData_->combinations.end(),
                       [](const auto &c) { return c.hasRangeLimits(); });
  }

  /// Whether a switch action produces a MIDI message of its own.
  static bool emitsMidi(const SwitchAction &sa) {
    return sa.type != SwitchActionType::NoteVelocity &&
           sa.type != SwitchActionType::ChannelSwitch &&
           sa.type != SwitchActionType::Unknown;
  }

  /// Convert a SwitchAction to a MidiInstruction proto, on `channel`
  /// (1-based; 0 = the note's channel). Keyswitches go at the note's own
  /// time, ahead of it in send order, as Dorico sends them.
  static void actionToInstruction(const SwitchAction &sa,
                                  fiddle::MidiInstruction *inst, int channel) {
    inst->set_channel(channel);
    switch (sa.type) {
    case SwitchActionType::KeySwitch:
      inst->set_type(fiddle::MidiInstruction::KEY_SWITCH);
      inst->set_param1(sa.param1);
      inst->set_param2(sa.param2 > 0 ? sa.param2 : 100);
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
    case SwitchActionType::ChannelSwitch: // routes the note; no message
    case SwitchActionType::NoteVelocity:  // a modifier, not an instruction
    case SwitchActionType::Unknown:
      break;
    }
  }
  /// Emit dynamics MIDI for a note based on the matched switch's dual
  /// dynamics model:
  ///   volumeType  = primary dynamics (attack / velocity)
  ///   volumeType2 = secondary dynamics (sustain / CC)
  /// Both are processed independently.
  /// Controllers that should follow live CC1, or none when the input
  /// dynamics are velocity-based (CC1 is then not a dynamics stream).
  static std::vector<int> liveDynamicsTargets(const fiddle::Note &note,
                                              const TechniqueCombination &combo) {
    std::vector<int> targets;
    if (note.dynamics_mode() != fiddle::Note::CC)
      return targets;
    if (combo.volumeType == VolumeType::kCC)
      targets.push_back(combo.volumeCC);
    if (combo.volumeType2 == VolumeType::kCC &&
        std::find(targets.begin(), targets.end(), combo.volumeCC2) == targets.end())
      targets.push_back(combo.volumeCC2);
    return targets;
  }

  static void emitDynamics(fiddle::Note &note,
                           const TechniqueCombination &combo,
                           double sampleRate = 44100.0) {
    auto inputMode = note.dynamics_mode();

    // ── Primary dynamics (volumeType) ────────────────────────────
    emitDynamicsForType(note, inputMode, combo.volumeType, combo.volumeCC, sampleRate);

    // ── Secondary dynamics (volumeType2) ─────────────────────────
    // Only process if volumeType2 differs from volumeType to avoid
    // double-emitting the same CC or velocity.
    if (combo.volumeType2 != combo.volumeType ||
        (combo.volumeType2 == VolumeType::kCC &&
         combo.volumeCC2 != combo.volumeCC)) {
      emitDynamicsForType(note, inputMode, combo.volumeType2, combo.volumeCC2, sampleRate);
    }
  }

  /// Emit dynamics for a single volume type slot.
  static void emitDynamicsForType(fiddle::Note &note,
                                  fiddle::Note::DynamicsMode inputMode,
                                  VolumeType targetType, int targetCC,
                                  double sampleRate) {
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
            inst->set_delay_ms(static_cast<int>(
                pt.offset_samples() * 1000.0 / std::max(1.0, sampleRate)));
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
