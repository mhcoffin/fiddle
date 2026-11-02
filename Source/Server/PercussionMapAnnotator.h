#pragma once

#include "Annotator.h"
#include "NoteInspection.h"
#include "PercussionMap.h"
#include <algorithm>
#include <map>
#include <memory>
#include <tuple>

namespace fiddle {

/// Plays an unpitched layer through its library percussion map, ahead of the
/// layer's expression map (docs/library-percussion-maps.md): the note takes
/// the winning entry's pitch, and the entry's keyswitches sound with the note
/// (note-on before it, note-off with its note-off), as Dorico sends them.
/// Switches, dynamics and durations stay with the expression map, which sees
/// the library pitch. A note no entry covers is withheld.
class PercussionMapAnnotator : public Annotator {
public:
  PercussionMapAnnotator(std::shared_ptr<const PercussionMap> map, std::string instrument)
      : map_(std::move(map)), instrument_(std::move(instrument)) {}

  std::string name() const override { return map_ ? map_->name : "PercussionMap"; }
  const AnnotationRecord *lastAnnotationRecord() const override { return &record_; }
  bool noteSuppressed() const override { return suppressed_; }

  void onNoteStart(fiddle::Note &note, const AnnotatorContext &ctx) override {
    record_ = makeIncomingNoteRecord(note, ctx.sampleRate, false);
    auto &decision = record_.percussion;
    decision.mapName = map_ ? map_->name : "";
    decision.instrument = instrument_;
    suppressed_ = false;
    const auto key = noteKey(note);
    const auto &techniques = record_.inputTechniques;
    const auto *entry = resolve(techniques, decision.instrumentFallback);
    if (!entry) {
      suppressed_ = true;
      decision.withheld = "no percussion-map entry for " + (instrument_.empty() ? "this chair" : instrument_) +
                          " " + joinTechniques(techniques);
      withheld_.insert(key);
      return;
    }
    decision.entryName = entry->name;
    decision.entryTechniques = entry->techniques;
    decision.pitch = entry->pitch;
    decision.keyswitches = entry->keyswitches;
    const bool roll = std::any_of(techniques.begin(), techniques.end(), isRollTechnique);
    const bool entryRoll = std::any_of(entry->techniques.begin(), entry->techniques.end(), isRollTechnique);
    if (roll && !entryRoll)
      decision.warning = "roll without a roll entry: Dorico would play strokes on this pitch";

    for (const int k : entry->keyswitches) {
      auto *inst = note.add_pre_note();
      inst->set_type(fiddle::MidiInstruction::NOTE_ON);
      inst->set_param1(k);
      inst->set_param2(127);
    }
    note.set_note_number(static_cast<uint32_t>(entry->pitch));
    sounding_[key] = {entry->pitch, entry->keyswitches};
  }

  void onNoteEnd(fiddle::Note &note, const AnnotatorContext &) override {
    const auto key = noteKey(note);
    suppressed_ = withheld_.erase(key) > 0;
    if (suppressed_)
      return;
    const auto it = sounding_.find(key);
    if (it == sounding_.end())
      return; // started before this map was assigned
    note.set_note_number(static_cast<uint32_t>(it->second.pitch));
    for (const int k : it->second.keyswitches) {
      auto *inst = note.add_post_note();
      inst->set_type(fiddle::MidiInstruction::NOTE_OFF);
      inst->set_param1(k);
      inst->set_param2(64);
    }
    sounding_.erase(it);
  }

  void onTransportStart(const AnnotatorContext &, std::vector<fiddle::MidiInstruction> &) override {
    sounding_.clear();
    withheld_.clear();
  }

  void resetState() override { cache_.clear(); }

private:
  struct Sounding {
    int pitch = 0;
    std::vector<int> keyswitches;
  };
  struct Cached {
    const PercussionMapEntry *entry = nullptr;
    bool instrumentFallback = false;
  };
  using NoteKey = std::tuple<uint64_t, uint32_t, uint32_t>;

  static NoteKey noteKey(const fiddle::Note &note) { return {note.id(), note.channel(), note.note_number()}; }

  const PercussionMapEntry *resolve(const std::set<std::string> &techniques, bool &instrumentFallback) {
    if (!map_)
      return nullptr;
    auto it = cache_.find(techniques);
    if (it == cache_.end()) {
      const auto match = map_->resolve(instrument_, techniques);
      it = cache_.emplace(techniques, Cached{match.entry, match.instrumentFallback}).first;
    }
    instrumentFallback = it->second.instrumentFallback;
    return it->second.entry;
  }

  std::shared_ptr<const PercussionMap> map_;
  std::string instrument_;
  AnnotationRecord record_;
  bool suppressed_ = false;
  std::map<std::set<std::string>, Cached> cache_;
  std::map<NoteKey, Sounding> sounding_;
  std::set<NoteKey> withheld_;
};

} // namespace fiddle
