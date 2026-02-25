#pragma once

#include "ExpressionMapLibrary.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "UndoManager.h"

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
      s->expressionMap = newData_;
      s->expressionMapPath = "";
    }
  }
  void undo() override {
    if (auto *s = mixer_.getStrip(stripId_)) {
      if (oldEntityID_.empty()) {
        s->expressionMap = nullptr;
      } else {
        s->expressionMap = library_.load(oldEntityID_);
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
                  const juce::String &stripId, int oldUid, int newUid)
      : mixer_(mixer), scanner_(scanner), stripId_(stripId), oldUid_(oldUid),
        newUid_(newUid) {}

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
      return;
    }
    for (const auto &d : scanner_.getKnownPluginList().getTypes()) {
      if (d.uniqueId == uid) {
        s->loadPlugin(d, mixer_.getFormatManager());
        return;
      }
    }
  }

  MixerModel &mixer_;
  PluginScanner &scanner_;
  juce::String stripId_;
  int oldUid_, newUid_;
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
        strip->inputPort = src->inputPort;
        strip->inputChannel = src->inputChannel;
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

} // namespace fiddle
