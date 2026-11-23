#pragma once

#include "Annotator.h"
#include "LuaPlugin.h"
#include "NoteInspection.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <tuple>
#include <vector>

namespace fiddle {

/**
 * One of a layer's Lua scripts in its annotator chain (docs/lua-scripts.md).
 *
 * The script's on_note sees each note once, as it starts. What it decided
 * is kept until Dorico's note-off, so the note-off reaches the pitch and
 * channel the note-on used, keeps its techniques, and is dropped with a
 * dropped note: scripts never see note-offs.
 */
class LuaAnnotator : public Annotator {
public:
  explicit LuaAnnotator(std::shared_ptr<LuaPlugin> plugin)
      : plugin_(std::move(plugin)) {}

  void onNoteStart(fiddle::Note &note, const AnnotatorContext &ctx) override {
    suppressed_ = false;
    startOffsetMs_ = 0.0;
    maxLengthMs_ = -1.0;
    const auto key = noteKey(note);
    sounding_.erase(key);
    if (!running())
      return;
    const auto change = plugin_->onNote(note, luaContext(ctx));
    if (!change)
      return; // no on_note, or it failed: the note goes on as it came

    Sounding sounding;
    sounding.startMs = ctx.currentTimeMs;
    if (change->drop) {
      suppressed_ = true;
      sounding.dropped = true;
      sounding_[key] = sounding;
      return;
    }
    note.set_note_number(static_cast<uint32_t>(change->pitch));
    note.set_start_velocity(static_cast<uint32_t>(change->velocity));
    note.set_channel(static_cast<uint32_t>(change->channel));
    if (change->techniques)
      setInputTechniqueIds(note, *change->techniques);
    for (const auto &controller : change->controllers) {
      if (controller.atEnd)
        sounding.endControllers.push_back(controller);
      else
        addController(*note.mutable_during_note(), controller);
    }
    startOffsetMs_ = change->startOffsetMs;
    maxLengthMs_ = change->maxLengthMs;
    sounding.pitch = change->pitch;
    sounding.channel = change->channel;
    sounding.techniques = change->techniques;
    sounding.startOffsetMs = change->startOffsetMs;
    sounding.lengthDeltaMs = change->lengthDeltaMs;
    sounding_[key] = std::move(sounding);
  }

  // Runs even when the script is bypassed, so a note it changed before
  // then still ends as it began.
  void onNoteEnd(fiddle::Note &note, const AnnotatorContext &ctx) override {
    suppressed_ = false;
    durationAdjustMs_ = 0.0;
    const auto found = sounding_.find(noteKey(note));
    if (found == sounding_.end())
      return;
    const auto sounding = std::move(found->second);
    sounding_.erase(found);
    if (sounding.dropped) {
      suppressed_ = true;
      return;
    }
    note.set_note_number(static_cast<uint32_t>(sounding.pitch));
    note.set_channel(static_cast<uint32_t>(sounding.channel));
    if (sounding.techniques)
      setInputTechniqueIds(note, *sounding.techniques);
    // A note moves with its start, and is shortened no further than
    // kLuaMinNoteLengthMs (one Dorico sent shorter isn't lengthened).
    double delta = sounding.lengthDeltaMs;
    if (delta < 0.0) {
      const double length = ctx.currentTimeMs - sounding.startMs;
      delta = std::max(delta, std::min(0.0, kLuaMinNoteLengthMs - length));
    }
    durationAdjustMs_ = sounding.startOffsetMs + delta;
    for (const auto &controller : sounding.endControllers)
      addController(*note.mutable_post_note(), controller);
  }

  bool transformCC(int &number, int &value, int &channel,
                   const AnnotatorContext &ctx) override {
    if (!running())
      return true;
    const auto change = plugin_->onCC(number, value, channel, luaContext(ctx));
    if (!change)
      return true;
    if (change->drop)
      return false;
    number = change->number;
    value = change->value;
    channel = change->channel;
    return true;
  }

  std::string name() const override {
    if (plugin_)
      return plugin_->meta().name;
    return "Lua (unloaded)";
  }

  bool noteSuppressed() const override { return suppressed_; }
  double noteStartOffsetMs() const override { return startOffsetMs_; }
  double durationAdjustMs() const override { return durationAdjustMs_; }
  double scheduledNoteOffMs() const override { return maxLengthMs_; }

  /// Each pass starts the script from the same state, random numbers
  /// included, so playback repeats (docs/lua-scripts.md).
  void onTransportStart(const AnnotatorContext &,
                        std::vector<fiddle::MidiInstruction> &) override {
    sounding_.clear();
    if (plugin_)
      plugin_->reset();
  }

  void resetState() override {
    if (plugin_)
      plugin_->reset();
  }

  /// Access the underlying plugin (for config UI).
  LuaPlugin *plugin() const { return plugin_.get(); }

private:
  /// What on_note decided for a sounding note, for its note-off.
  struct Sounding {
    bool dropped = false;
    int pitch = 0, channel = 1;
    std::optional<std::set<std::string>> techniques;
    double startMs = 0.0, startOffsetMs = 0.0, lengthDeltaMs = 0.0;
    std::vector<LuaNoteChange::Controller> endControllers;
  };
  /// The note as this script receives it, at its start and at its end.
  using NoteKey = std::tuple<uint64_t, uint32_t, uint32_t>;
  static NoteKey noteKey(const fiddle::Note &note) {
    return {note.id(), note.channel(), note.note_number()};
  }

  bool running() const { return plugin_ && plugin_->isLoaded() && plugin_->isEnabled(); }

  static LuaContext luaContext(const AnnotatorContext &ctx) {
    LuaContext lua;
    lua.timeMs = ctx.currentTimeMs;
    lua.tempoBpm = ctx.tempoBpm;
    lua.tonalContext = ctx.tonalContext;
    return lua;
  }

  static void addController(google::protobuf::RepeatedPtrField<fiddle::MidiInstruction> &list,
                            const LuaNoteChange::Controller &controller) {
    auto *instruction = list.Add();
    instruction->set_type(fiddle::MidiInstruction::CC);
    instruction->set_param1(controller.number);
    instruction->set_param2(controller.value);
    instruction->set_delay_ms(static_cast<int32_t>(std::lround(controller.offsetMs)));
  }

  std::shared_ptr<LuaPlugin> plugin_;
  std::map<NoteKey, Sounding> sounding_;
  bool suppressed_ = false;
  double startOffsetMs_ = 0.0;
  double maxLengthMs_ = -1.0;
  double durationAdjustMs_ = 0.0;
};

} // namespace fiddle
