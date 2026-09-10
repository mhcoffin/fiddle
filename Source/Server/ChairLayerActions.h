#pragma once

#include "LibraryRoutingRepository.h"
#include "MixerModel.h"
#include "UndoManager.h"

#include <functional>

namespace fiddle {

/// A chair layer is both a persisted assignment and a live strip. Keep the
/// actual strip (including player, FX, routing and unsaved edits) for undo;
/// recreating it from its catalog patch would lose layer-specific changes.
/// Like the undo manager, this action is used on the message thread.
class RemoveChairLayerAction final : public UndoableAction {
public:
  using Changed = std::function<void(bool)>;

  RemoveChairLayerAction(LibraryRoutingRepository &repository,
                        MixerModel &mixer, LayerRow layer, Changed changed)
      : repository_(repository), mixer_(mixer), layer_(std::move(layer)),
        changed_(std::move(changed)) {}

  void execute() override {
    success_ = false;
    const juce::String id(layer_.id);
    index_ = mixer_.stripIndex(id);
    const auto current = repository_.getLayer(layer_.id);
    if (index_ < 0 || !current || !repository_.deleteLayer(layer_.id)) {
      changed_(false);
      return;
    }
    layer_ = *current;
    removedStrip_ = mixer_.removeStripKeepAlive(id);
    // A removed player no longer receives note-offs. Queue a reset so undo
    // cannot resurrect a held note or old delayed MIDI.
    removedStrip_->allNotesOff();
    if (removedStrip_->isEditorVisible())
      removedStrip_->toggleEditor();
    removedStrip_->audioEngine().closeEditors();
    success_ = true;
    changed_(true);
  }

  void undo() override {
    success_ = false;
    if (!removedStrip_ || !repository_.upsertLayer(layer_)) {
      changed_(false);
      return;
    }
    mixer_.insertStripAt(std::move(removedStrip_), index_);
    success_ = true;
    changed_(true);
  }

  bool succeeded() const override { return success_; }

  juce::String getDescription() const override {
    return "Remove layer '" + juce::String(layer_.patchName) + "'";
  }

private:
  LibraryRoutingRepository &repository_;
  MixerModel &mixer_;
  LayerRow layer_;
  Changed changed_;
  std::shared_ptr<MixerStrip> removedStrip_;
  int index_ = 0;
  bool success_ = false;
};

/// Create once from the catalog. Subsequent undo/redo transfers the original
/// live strip using the same assignment/lifetime rules as layer removal.
class AddChairLayerAction final : public UndoableAction {
public:
  using Instantiate = std::function<bool(const LayerRow &)>;
  using Changed = RemoveChairLayerAction::Changed;

  AddChairLayerAction(LibraryRoutingRepository &repository, MixerModel &mixer,
                     std::string chairId, std::string patchId, int position,
                     Instantiate instantiate, Changed changed)
      : repository_(repository), mixer_(mixer), chairId_(std::move(chairId)),
        patchId_(std::move(patchId)), position_(position),
        instantiate_(std::move(instantiate)), changed_(std::move(changed)) {}

  void execute() override {
    success_ = false;
    if (removal_) {
      removal_->undo();
      success_ = removal_->succeeded();
      return;
    }
    if (!repository_.createLayerFromPatch(id_, chairId_, patchId_, position_)) {
      changed_(false);
      return;
    }
    const auto layer = repository_.getLayer(id_);
    if (!layer || !instantiate_(*layer) || !mixer_.getStrip(juce::String(id_))) {
      repository_.deleteLayer(id_);
      mixer_.removeStrip(juce::String(id_));
      changed_(false);
      return;
    }
    name_ = layer->patchName;
    removal_ = std::make_unique<RemoveChairLayerAction>(
        repository_, mixer_, *layer, changed_);
    success_ = true;
    changed_(true);
  }

  void undo() override {
    success_ = false;
    if (removal_) {
      removal_->execute();
      success_ = removal_->succeeded();
    }
  }

  bool succeeded() const override { return success_; }

  juce::String getDescription() const override {
    return "Add layer '" + juce::String(name_) + "'";
  }

private:
  LibraryRoutingRepository &repository_;
  MixerModel &mixer_;
  std::string id_ = juce::Uuid().toString().toStdString();
  std::string chairId_, patchId_, name_;
  int position_;
  Instantiate instantiate_;
  Changed changed_;
  std::unique_ptr<RemoveChairLayerAction> removal_;
  bool success_ = false;
};

} // namespace fiddle
