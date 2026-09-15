#pragma once

#include "LuaPlugin.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"
#include <algorithm>
#include <filesystem>

namespace fiddle {

// ─── Property change actions ─────────────────────────────────────────

/// Undo/redo for setStripLibrary.
class SetLibraryAction : public UndoableAction {
public:
  SetLibraryAction(MixerModel &mixer, const juce::String &stripId,
                   const juce::String &oldLib, const juce::String &newLib)
      : mixer_(mixer), stripId_(stripId), oldLib_(oldLib), newLib_(newLib) {}

  void execute() override {
    if (auto *s = mixer_.getStrip(stripId_))
      s->library = newLib_;
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_))
      s->library = oldLib_;
  }
  juce::String getDescription() const override {
    return "Set library to '" + newLib_ + "'";
  }

private:
  MixerModel &mixer_;
  juce::String stripId_, oldLib_, newLib_;
};

/// Undo/redo for setStripInput.
class SetInputAction : public UndoableAction {
public:
  SetInputAction(MixerModel &mixer, const juce::String &stripId, int oldPort,
                 int oldCh, int newPort, int newCh)
      : mixer_(mixer), stripId_(stripId), oldPort_(oldPort), oldCh_(oldCh),
        newPort_(newPort), newCh_(newCh) {}

  void execute() override {
    if (auto *s = mixer_.getStrip(stripId_)) {
      s->setInputAssignment(newPort_, newCh_);
    }
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_)) {
      s->setInputAssignment(oldPort_, oldCh_);
    }
  }
  juce::String getDescription() const override {
    return "Set input to port " + juce::String(newPort_) + " ch " +
           juce::String(newCh_);
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  int oldPort_, oldCh_, newPort_, newCh_;
};

/// Undo/redo for setStripGain. Supports coalescing for fader drags.
class SetGainAction : public UndoableAction {
public:
  SetGainAction(MixerModel &mixer, const juce::String &stripId, float oldGain,
                float newGain)
      : mixer_(mixer), stripId_(stripId), oldGain_(oldGain), newGain_(newGain) {
  }

  void execute() override {
    if (auto *s = mixer_.getStrip(stripId_))
      s->setGainDb(newGain_);
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_))
      s->setGainDb(oldGain_);
  }
  juce::String getDescription() const override {
    return "Set gain to " + juce::String(newGain_, 1) + " dB";
  }
  juce::String getCoalesceId() const override { return "gain:" + stripId_; }
  void coalesceWith(const UndoableAction &newer) override {
    // Keep our oldGain_ (original before-drag value), update newGain_
    newGain_ = static_cast<const SetGainAction &>(newer).newGain_;
  }
  bool isNoOp() const override { return oldGain_ == newGain_; }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  float oldGain_, newGain_;
};

/// Undo/redo for loadExpressionMap.
class SetExpressionMapAction : public UndoableAction {
public:
  SetExpressionMapAction(MixerModel &mixer, const juce::String &stripId,
                         ExpressionMapAssignment before,
                         ExpressionMapAssignment after)
      : mixer_(mixer), stripId_(stripId), before_(std::move(before)),
        after_(std::move(after)) {}

  void execute() override { apply(after_); }
  void undo() override { apply(before_); }
  bool succeeded() const override { return success_; }
  juce::String getDescription() const override {
    if (!after_.data)
      return "Clear expression map";
    return "Set expression map to '" + juce::String(after_.data->name) + "'";
  }

private:
  void apply(const ExpressionMapAssignment &assignment) {
    success_ = false;
    if (auto *strip = mixer_.getStrip(stripId_)) {
      strip->setExpressionMapAssignment(assignment);
      success_ = true;
    }
  }

  MixerModel &mixer_;
  juce::String stripId_;
  ExpressionMapAssignment before_, after_;
  bool success_ = false;
};

/// Undo/redo for a host-visible VSTi program selection. Program index alone is
/// insufficient: many instruments update additional opaque state when a
/// program changes, so both sides retain the complete serialized state too.
class SetPluginProgramAction : public UndoableAction {
public:
  SetPluginProgramAction(MixerModel &mixer, const juce::String &stripId,
                         int pluginUid,
                         HostedPluginSlot::ProgramState before,
                         int requestedProgram,
                         std::function<void(const juce::String &)> onApplied = {})
      : mixer_(mixer), stripId_(stripId), pluginUid_(pluginUid),
        before_(std::move(before)), requestedProgram_(requestedProgram),
        onApplied_(std::move(onApplied)) {}

  void execute() override {
    if (capturedAfter_) {
      swapTo(after_, before_);
      return;
    }
    success_ = false;
    auto *strip = matchingStrip();
    if (!strip || !strip->selectPluginProgram(requestedProgram_, after_))
      return;
    success_ = !(after_ == before_);
    capturedAfter_ = success_;
    if (success_ && onApplied_)
      onApplied_(stripId_);
  }

  void undo() override { swapTo(before_, after_); }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override { return requestedProgram_ == before_.index; }
  juce::String getDescription() const override {
    return "Select instrument program " + juce::String(requestedProgram_ + 1);
  }

private:
  MixerStrip *matchingStrip() const {
    auto *strip = mixer_.getStrip(stripId_);
    return strip && strip->pluginUid == pluginUid_ ? strip : nullptr;
  }

  void swapTo(const HostedPluginSlot::ProgramState &target,
              HostedPluginSlot::ProgramState &departing) {
    success_ = false;
    auto *strip = matchingStrip();
    HostedPluginSlot::ProgramState current;
    if (!strip || !strip->capturePluginProgramState(current))
      return;
    if (!strip->restorePluginProgramState(target))
      return;
    departing = std::move(current);
    success_ = true;
    if (onApplied_)
      onApplied_(stripId_);
  }

  MixerModel &mixer_;
  juce::String stripId_;
  int pluginUid_ = 0;
  HostedPluginSlot::ProgramState before_, after_;
  int requestedProgram_ = -1;
  bool capturedAfter_ = false;
  bool success_ = false;
  std::function<void(const juce::String &)> onApplied_;
};

/// Undo/redo for setStripPlugin.
class SetPluginAction : public UndoableAction {
public:
  SetPluginAction(MixerModel &mixer, PluginScanner &scanner,
                  const juce::String &stripId, int /*oldUid*/, int newUid,
                  std::function<void()> onComplete = nullptr)
      : mixer_(mixer), stripId_(stripId),
        newUid_(newUid), onComplete_(std::move(onComplete)) {
    for (const auto &d : scanner.getKnownPluginList().getTypes())
      if (d.uniqueId == newUid) after_.description = d;
    valid_ = newUid == 0 || (after_.description.uniqueId == newUid &&
        PluginCompatibility::fromDescription(after_.description, PluginSlotRole::instrument).compatible);
    if (auto *strip = mixer_.getStrip(stripId_);
        strip && strip->pluginStatus() == HostedPluginStatus::missing &&
        strip->requestedPluginUid() == newUid) {
      const auto missing = strip->snapshotInstrument();
      after_.state = missing.state; // Retry availability, not reset the patch.
      after_.bypassed = missing.bypassed;
    }
  }

  void execute() override { swapTo(after_, before_); }
  void undo() override { swapTo(before_, after_); }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override {
    auto *strip = mixer_.getStrip(stripId_);
    return valid_ && strip && strip->pluginStatus() != HostedPluginStatus::missing &&
           strip->pluginStatus() != HostedPluginStatus::failed &&
           strip->requestedPluginUid() == newUid_;
  }
  juce::String getDescription() const override {
    return "Set plugin UID " + juce::String(newUid_);
  }

private:
  void swapTo(const MixerStrip::InstrumentSnapshot &target,
              MixerStrip::InstrumentSnapshot &departing) {
    success_ = false;
    auto *s = mixer_.getStrip(stripId_);
    if (!s || !valid_) return;
    departing = s->snapshotInstrument(); // Includes latest vendor edits, every cycle.
    s->allNotesOff();
    s->unloadPlugin(); // Cancels any superseded asynchronous request.
    success_ = true;
    if (target.description.uniqueId == 0) {
      if (onComplete_)
        onComplete_();
      return;
    }
    s->pluginUid = target.description.uniqueId;
    auto cb = onComplete_;
    s->loadPlugin(target.description, mixer_.getFormatManager(),
                  [s, cb, bypassed = target.bypassed](bool) {
                    s->setPluginBypassed(bypassed);
                    if (cb) cb();
                  }, target.state);
    s->setPluginBypassed(target.bypassed);
  }

  MixerModel &mixer_;
  juce::String stripId_;
  int newUid_;
  std::function<void()> onComplete_;
  MixerStrip::InstrumentSnapshot before_, after_;
  bool valid_ = false, success_ = false;
};

// ─── Structural actions ──────────────────────────────────────────────

/// Undo/redo for addMixerStrip.
class AddStripAction : public UndoableAction {
public:
  AddStripAction(MixerModel &mixer) : mixer_(mixer) {}

  void execute() override {
    if (createdId_.isEmpty()) {
      createdId_ = mixer_.addStrip();
    } else {
      // Re-add on redo: create a new strip with same ID
      auto strip = std::make_unique<MixerStrip>();
      strip->id = createdId_;
      mixer_.insertStripAt(std::move(strip), insertIndex_);
    }
    insertIndex_ = mixer_.stripIndex(createdId_);
  }
  void undo() override {
    insertIndex_ = mixer_.stripIndex(createdId_);
    mixer_.removeStrip(createdId_);
  }
  juce::String getDescription() const override { return "Add strip"; }
  juce::String getCreatedId() const { return createdId_; }

private:
  MixerModel &mixer_;
  juce::String createdId_;
  int insertIndex_ = -1;
};

/// Undo/redo for removeMixerStrip. Keeps the strip alive for undo.
class RemoveStripAction : public UndoableAction {
public:
  RemoveStripAction(MixerModel &mixer, const juce::String &stripId)
      : mixer_(mixer), stripId_(stripId) {
    index_ = mixer_.stripIndex(stripId);
  }

  void execute() override {
    index_ = mixer_.stripIndex(stripId_);
    removedStrip_ = mixer_.removeStripKeepAlive(stripId_);
  }
  void undo() override {
    if (removedStrip_) {
      mixer_.insertStripAt(std::move(removedStrip_), index_);
    }
  }
  juce::String getDescription() const override { return "Remove strip"; }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  int index_ = 0;
  std::shared_ptr<MixerStrip> removedStrip_;
};

/// Undo/redo for duplicateStripInput.
class DuplicateStripAction : public UndoableAction {
public:
  DuplicateStripAction(MixerModel &mixer, const juce::String &sourceStripId)
      : mixer_(mixer), sourceStripId_(sourceStripId) {}

  void execute() override {
    if (createdId_.isEmpty()) {
      createdId_ = mixer_.duplicateStripAfter(sourceStripId_);
    } else {
      // Re-duplicate on redo
      auto strip = std::make_unique<MixerStrip>();
      strip->id = createdId_;
      // Copy source properties
      if (auto *src = mixer_.getStrip(sourceStripId_)) {
        const auto sourceState = src->realtimeState();
        strip->setInputAssignment(sourceState.inputPort,
                                  sourceState.inputChannel);
        strip->family = src->family;
        strip->isSolo = src->isSolo;
        strip->directOutputBusId = src->directOutputBusId;
      }
      int sourceIdx = mixer_.stripIndex(sourceStripId_);
      mixer_.insertStripAt(std::move(strip), sourceIdx + 1);
    }
  }
  void undo() override { mixer_.removeStrip(createdId_); }
  juce::String getDescription() const override { return "Duplicate strip"; }
  juce::String getCreatedId() const { return createdId_; }

private:
  MixerModel &mixer_;
  juce::String sourceStripId_;
  juce::String createdId_;
};

/// Undo/redo for toggleLibraryActive. Captures per-strip old active states.
class ToggleLibraryActiveAction : public UndoableAction {
public:
  ToggleLibraryActiveAction(MixerModel &mixer, const juce::String &libraryName,
                            bool newState)
      : mixer_(mixer), libraryName_(libraryName), newState_(newState) {
    // Capture old active state for every strip in this library
    for (auto *s : mixer_.getAllStrips()) {
      if (s->library == libraryName_)
        oldStates_.push_back({s->id, s->isActive()});
    }
  }

  void execute() override {
    for (auto *s : mixer_.getAllStrips()) {
      if (s->library == libraryName_)
        s->setActive(newState_);
    }
  }
  void undo() override {
    for (const auto &[id, was] : oldStates_) {
      if (auto *s = mixer_.getStrip(id))
        s->setActive(was);
    }
  }
  juce::String getDescription() const override {
    return (newState_ ? "Activate " : "Deactivate ") + libraryName_;
  }

private:
  MixerModel &mixer_;
  juce::String libraryName_;
  bool newState_;
  std::vector<std::pair<juce::String, bool>> oldStates_;
};

// ─── Lua plugin actions ──────────────────────────────────────────────

/// Undo/redo for adding a Lua plugin to a strip.
class AddLuaPluginAction : public UndoableAction {
public:
  AddLuaPluginAction(MixerModel &mixer, LuaPluginCatalog &catalog,
                     const juce::String &stripId,
                     const std::string &pluginReference)
      : mixer_(mixer), stripId_(stripId) {
    auto *strip = mixer_.getStrip(stripId_);
    if (!strip)
      return;

    {
      auto lock = strip->lockMidiState();
      insertIndex_ = strip->luaPlugins.size();
    }
    const auto resolved = catalog.resolvePluginPath(pluginReference);
    if (resolved.empty())
      return;

    plugin_ = std::make_shared<LuaPlugin>(resolved);
    pluginFileName_ = std::filesystem::path(resolved).filename().string();
    valid_ = plugin_->load();
  }

  void execute() override {
    success_ = false;
    auto *s = mixer_.getStrip(stripId_);
    if (!valid_ || !s || !plugin_)
      return;

    auto lock = s->lockMidiState();
    if (insertIndex_ > s->luaPlugins.size() ||
        std::find(s->luaPlugins.begin(), s->luaPlugins.end(), plugin_) !=
            s->luaPlugins.end()) {
      return;
    }
    s->insertLuaPlugin(insertIndex_, plugin_);
    success_ = insertIndex_ < s->luaPlugins.size() &&
               s->luaPlugins[insertIndex_] == plugin_;
  }

  void undo() override {
    success_ = false;
    auto *s = mixer_.getStrip(stripId_);
    if (!s || !plugin_)
      return;

    auto lock = s->lockMidiState();
    if (insertIndex_ >= s->luaPlugins.size() ||
        s->luaPlugins[insertIndex_] != plugin_)
      return;
    s->removeLuaPlugin(insertIndex_);
    success_ = true;
  }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override { return !valid_; }
  juce::String getDescription() const override {
    return "Add Lua plugin '" + juce::String(pluginFileName_) + "'";
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  std::string pluginFileName_;
  std::shared_ptr<LuaPlugin> plugin_;
  std::size_t insertIndex_ = 0;
  bool valid_ = false;
  bool success_ = false;
};

/// Undo/redo for removing a Lua plugin from a strip.
class RemoveLuaPluginAction : public UndoableAction {
public:
  RemoveLuaPluginAction(MixerModel &mixer, const juce::String &stripId,
                        int pluginIndex)
      : mixer_(mixer), stripId_(stripId), pluginIndex_(pluginIndex) {
    auto *strip = mixer_.getStrip(stripId_);
    if (!strip || pluginIndex_ < 0)
      return;

    auto lock = strip->lockMidiState();
    if (static_cast<std::size_t>(pluginIndex_) >= strip->luaPlugins.size())
      return;
    plugin_ = strip->luaPlugins[static_cast<std::size_t>(pluginIndex_)];
    if (!plugin_)
      return;
    pluginFileName_ =
        std::filesystem::path(plugin_->filePath()).filename().string();
    valid_ = true;
  }

  void execute() override {
    success_ = false;
    auto *s = mixer_.getStrip(stripId_);
    if (!valid_ || !s || !plugin_)
      return;

    auto lock = s->lockMidiState();
    const auto index = static_cast<std::size_t>(pluginIndex_);
    if (index >= s->luaPlugins.size() || s->luaPlugins[index] != plugin_)
      return;
    s->removeLuaPlugin(index);
    success_ = true;
  }

  void undo() override {
    success_ = false;
    auto *s = mixer_.getStrip(stripId_);
    if (!s || !plugin_)
      return;

    auto lock = s->lockMidiState();
    const auto index = static_cast<std::size_t>(pluginIndex_);
    if (index > s->luaPlugins.size() ||
        std::find(s->luaPlugins.begin(), s->luaPlugins.end(), plugin_) !=
            s->luaPlugins.end()) {
      return;
    }
    s->insertLuaPlugin(index, plugin_);
    success_ = index < s->luaPlugins.size() &&
               s->luaPlugins[index] == plugin_;
  }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override { return !valid_; }
  juce::String getDescription() const override {
    return "Remove Lua plugin '" + juce::String(pluginFileName_) + "'";
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  int pluginIndex_;
  std::string pluginFileName_;
  std::shared_ptr<LuaPlugin> plugin_;
  bool valid_ = false;
  bool success_ = false;
};

// ─── Compound action (multi-strip group operations) ──────────────────

/// Wraps multiple sub-actions into a single undo step.
/// execute() runs all sub-actions in order; undo() reverses them.
class CompoundAction : public UndoableAction {
public:
  CompoundAction(juce::String description,
                 std::vector<std::unique_ptr<UndoableAction>> actions,
                 juce::String coalesceId = {})
      : description_(std::move(description)), actions_(std::move(actions)),
        coalesceId_(std::move(coalesceId)) {}

  void execute() override {
    success_ = false;
    for (size_t i = 0; i < actions_.size(); ++i) {
      actions_[i]->execute();
      if (!actions_[i]->succeeded()) {
        while (i > 0) actions_[--i]->undo();
        return;
      }
    }
    success_ = true;
  }
  void undo() override {
    success_ = false;
    for (size_t i = actions_.size(); i > 0; --i) {
      actions_[i - 1]->undo();
      if (!actions_[i - 1]->succeeded()) {
        for (size_t j = i; j < actions_.size(); ++j) actions_[j]->execute();
        return;
      }
    }
    success_ = true;
  }
  bool succeeded() const override { return success_; }
  juce::String getDescription() const override { return description_; }
  juce::String getCoalesceId() const override { return coalesceId_; }

  bool canCoalesceWith(const UndoableAction &newer) const override {
    if (!UndoableAction::canCoalesceWith(newer))
      return false;
    const auto &other = static_cast<const CompoundAction &>(newer);
    if (actions_.size() != other.actions_.size())
      return false;
    for (size_t i = 0; i < actions_.size(); ++i)
      if (!actions_[i]->canCoalesceWith(*other.actions_[i]))
        return false;
    return true;
  }
  bool isNoOp() const override {
    return std::all_of(actions_.begin(), actions_.end(),
                       [](const auto &action) { return action->isNoOp(); });
  }

  void coalesceWith(const UndoableAction &newer) override {
    auto &other = static_cast<const CompoundAction &>(newer);
    // Merge sub-actions pairwise (assumes same count and order)
    if (actions_.size() == other.actions_.size()) {
      for (size_t i = 0; i < actions_.size(); ++i)
        actions_[i]->coalesceWith(*other.actions_[i]);
    }
  }

private:
  juce::String description_;
  std::vector<std::unique_ptr<UndoableAction>> actions_;
  juce::String coalesceId_;
  bool success_ = false;
};

} // namespace fiddle
