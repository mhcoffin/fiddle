#pragma once

#include "ExpressionMapLibrary.h"
#include "LuaPlugin.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"
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
      s->inputPort = newPort_;
      s->inputChannel = newCh_;
    }
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_)) {
      s->inputPort = oldPort_;
      s->inputChannel = oldCh_;
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
      s->gainDb.store(newGain_, std::memory_order_relaxed);
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_))
      s->gainDb.store(oldGain_, std::memory_order_relaxed);
  }
  juce::String getDescription() const override {
    return "Set gain to " + juce::String(newGain_, 1) + " dB";
  }
  juce::String getCoalesceId() const override { return "gain:" + stripId_; }
  void coalesceWith(const UndoableAction &newer) override {
    // Keep our oldGain_ (original before-drag value), update newGain_
    newGain_ = static_cast<const SetGainAction &>(newer).newGain_;
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  float oldGain_, newGain_;
};

/// Undo/redo for loadExpressionMap.
class SetExpressionMapAction : public UndoableAction {
public:
  SetExpressionMapAction(MixerModel &mixer, ExpressionMapLibrary &library,
                         const juce::String &stripId,
                         const std::string &oldEntityID,
                         const std::string &newEntityID,
                         std::shared_ptr<ExpressionMapData> newData)
      : mixer_(mixer), library_(library), stripId_(stripId),
        oldEntityID_(oldEntityID), newEntityID_(newEntityID),
        newData_(std::move(newData)) {}

  void execute() override {
    if (auto *s = mixer_.getStrip(stripId_)) {
      s->setExpressionMap(newData_);
      s->expressionMapPath = "";
    }
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_)) {
      if (oldEntityID_.empty()) {
        s->setExpressionMap(nullptr);
      } else {
        s->setExpressionMap(library_.load(oldEntityID_));
      }
      s->expressionMapPath = "";
    }
  }
  juce::String getDescription() const override {
    return "Set expression map to '" + juce::String(newEntityID_) + "'";
  }

private:
  MixerModel &mixer_;
  ExpressionMapLibrary &library_;
  juce::String stripId_;
  std::string oldEntityID_, newEntityID_;
  std::shared_ptr<ExpressionMapData> newData_;
};

/// Undo/redo for setStripPlugin.
class SetPluginAction : public UndoableAction {
public:
  SetPluginAction(MixerModel &mixer, PluginScanner &scanner,
                  const juce::String &stripId, int oldUid, int newUid,
                  std::function<void()> onComplete = nullptr)
      : mixer_(mixer), scanner_(scanner), stripId_(stripId), oldUid_(oldUid),
        newUid_(newUid), onComplete_(std::move(onComplete)) {}

  void execute() override { loadPluginByUid(newUid_); }
  void undo() override { loadPluginByUid(oldUid_); }
  juce::String getDescription() const override {
    return "Set plugin UID " + juce::String(newUid_);
  }

private:
  void loadPluginByUid(int uid) {
    auto *s = mixer_.getStrip(stripId_);
    if (!s)
      return;
    if (uid == 0) {
      s->unloadPlugin();
      s->pluginUid = 0;
      if (onComplete_)
        onComplete_();
      return;
    }
    for (const auto &d : scanner_.getKnownPluginList().getTypes()) {
      if (d.uniqueId == uid) {
        auto cb = onComplete_;
        s->loadPlugin(d, mixer_.getFormatManager(), [cb](bool) {
          if (cb)
            cb();
        });
        return;
      }
    }
  }

  MixerModel &mixer_;
  PluginScanner &scanner_;
  juce::String stripId_;
  int oldUid_, newUid_;
  std::function<void()> onComplete_;
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
  std::unique_ptr<MixerStrip> removedStrip_;
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
        strip->inputPort.store(src->inputPort.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        strip->inputChannel.store(
            src->inputChannel.load(std::memory_order_relaxed),
            std::memory_order_relaxed);
        strip->family = src->family;
        strip->isSolo = src->isSolo;
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
        oldStates_.push_back({s->id, s->active});
    }
  }

  void execute() override {
    for (auto *s : mixer_.getAllStrips()) {
      if (s->library == libraryName_)
        s->active = newState_;
    }
  }
  void undo() override {
    for (const auto &[id, was] : oldStates_) {
      if (auto *s = mixer_.getStrip(id))
        s->active = was;
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
                     const std::string &pluginFileName)
      : mixer_(mixer), catalog_(catalog), stripId_(stripId),
        pluginFileName_(pluginFileName) {}

  void execute() override {
    auto *s = mixer_.getStrip(stripId_);
    if (!s) {
      std::cerr << "[Lua] AddLuaPluginAction: strip not found: " << stripId_
                << std::endl;
      return;
    }
    auto resolved = catalog_.resolvePluginPath(pluginFileName_);
    if (resolved.empty()) {
      std::cerr << "[Lua] AddLuaPluginAction: resolvePluginPath failed for '"
                << pluginFileName_ << "'" << std::endl;
      return;
    }
    auto plugin = std::make_shared<LuaPlugin>(resolved);
    if (plugin->load()) {
      s->addLuaPlugin(std::move(plugin));
      insertIndex_ = (int)s->luaPlugins.size() - 1;
      std::cerr << "[Lua] AddLuaPluginAction: added '" << pluginFileName_
                << "' to strip " << stripId_
                << " (chain size: " << s->luaPlugins.size() << ")"
                << std::endl;
    } else {
      std::cerr << "[Lua] AddLuaPluginAction: plugin->load() failed for '"
                << resolved << "'" << std::endl;
    }
  }
  void undo() override {
    auto *s = mixer_.getStrip(stripId_);
    if (!s || insertIndex_ < 0) return;
    s->removeLuaPlugin((size_t)insertIndex_);
  }
  juce::String getDescription() const override {
    return "Add Lua plugin '" + juce::String(pluginFileName_) + "'";
  }

private:
  MixerModel &mixer_;
  LuaPluginCatalog &catalog_;
  juce::String stripId_;
  std::string pluginFileName_;
  int insertIndex_ = -1;
};

/// Undo/redo for removing a Lua plugin from a strip.
class RemoveLuaPluginAction : public UndoableAction {
public:
  RemoveLuaPluginAction(MixerModel &mixer, LuaPluginCatalog &catalog,
                        const juce::String &stripId, int pluginIndex)
      : mixer_(mixer), catalog_(catalog), stripId_(stripId),
        pluginIndex_(pluginIndex) {
    // Capture the plugin filename before removal for undo
    if (auto *s = mixer_.getStrip(stripId_)) {
      if (pluginIndex_ >= 0 && (size_t)pluginIndex_ < s->luaPlugins.size()) {
        std::filesystem::path fp(s->luaPlugins[(size_t)pluginIndex_]->filePath());
        pluginFileName_ = fp.filename().string();
      }
    }
  }

  void execute() override {
    auto *s = mixer_.getStrip(stripId_);
    if (!s || pluginIndex_ < 0) return;
    if ((size_t)pluginIndex_ < s->luaPlugins.size())
      s->removeLuaPlugin((size_t)pluginIndex_);
  }
  void undo() override {
    auto *s = mixer_.getStrip(stripId_);
    if (!s || pluginFileName_.empty()) return;
    auto resolved = catalog_.resolvePluginPath(pluginFileName_);
    if (resolved.empty()) return;
    auto plugin = std::make_shared<LuaPlugin>(resolved);
    if (plugin->load())
      s->insertLuaPlugin((size_t)pluginIndex_, std::move(plugin));
  }
  juce::String getDescription() const override {
    return "Remove Lua plugin '" + juce::String(pluginFileName_) + "'";
  }

private:
  MixerModel &mixer_;
  LuaPluginCatalog &catalog_;
  juce::String stripId_;
  int pluginIndex_;
  std::string pluginFileName_;
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
    for (auto &a : actions_)
      a->execute();
  }
  void undo() override {
    for (int i = (int)actions_.size() - 1; i >= 0; --i)
      actions_[(size_t)i]->undo();
  }
  juce::String getDescription() const override { return description_; }
  juce::String getCoalesceId() const override { return coalesceId_; }

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
};

} // namespace fiddle
