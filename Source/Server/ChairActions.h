#pragma once

#include "LibraryRoutingRepository.h"
#include "MixerModel.h"
#include "UndoManager.h"
#include <functional>
#include <set>

namespace fiddle {

using ChairChanged = std::function<void(bool)>;

/// The assignment and the live DSP objects have different lifetimes. Keep
/// both, including a pending instrument load, for exact Undo/Redo restoration.
class RemoveChairAction final : public UndoableAction {
public:
  RemoveChairAction(LibraryRoutingRepository &repository, MixerModel &mixer,
                    std::string id, ChairChanged changed)
      : repository_(repository), mixer_(mixer), id_(std::move(id)), changed_(std::move(changed)) {}

  void execute() override {
    success_ = false;
    const auto chair = repository_.getChair(id_);
    if (!chair) { changed_(false); return; }
    auto layers = repository_.listLayers(id_);
    std::vector<Retained> retained;
    for (const auto &layer : layers) {
      const juce::String id(layer.id);
      auto *strip = mixer_.getStrip(id);
      if (!strip || strip->chairId.toStdString() != id_) { changed_(false); return; }
      retained.push_back({id, mixer_.stripIndex(id), {}});
    }
    // Do not leave an orphan live layer behind if topology is inconsistent.
    for (auto *strip : mixer_.getAllStrips()) {
      if (strip->chairId.toStdString() == id_ &&
          std::none_of(retained.begin(), retained.end(), [&](const auto &r) { return r.id == strip->id; })) {
        changed_(false); return;
      }
    }
    if (!repository_.deleteChairAndLayers(id_)) { changed_(false); return; }
    chair_ = *chair;
    layers_ = std::move(layers);
    retained_ = std::move(retained);
    for (auto &r : retained_) {
      r.strip = mixer_.removeStripKeepAlive(r.id);
      r.strip->allNotesOff();
      if (r.strip->isEditorVisible()) r.strip->toggleEditor();
      r.strip->audioEngine().closeEditors();
    }
    auto settings = mixer_.projectSettings();
    locked_ = settings.lockedChairIds.erase(id_) != 0;
    if (const auto found = settings.chairLevels.find(id_); found != settings.chairLevels.end()) {
      level_ = found->second;
      settings.chairLevels.erase(found);
    } else level_.reset();
    mixer_.setProjectSettings(settings);
    success_ = true;
    changed_(true);
  }

  void undo() override {
    success_ = false;
    for (const auto &r : retained_)
      if (!r.strip || mixer_.getStrip(r.id)) { changed_(false); return; }
    if (!repository_.restoreChairAndLayers(chair_, layers_)) { changed_(false); return; }
    std::sort(retained_.begin(), retained_.end(), [](const auto &a, const auto &b) { return a.index < b.index; });
    for (auto &r : retained_) mixer_.insertStripAt(std::move(r.strip), r.index);
    auto settings = mixer_.projectSettings();
    if (locked_) settings.lockedChairIds.insert(id_);
    if (level_) settings.chairLevels[id_] = *level_;
    mixer_.setProjectSettings(settings);
    success_ = true;
    changed_(true);
  }
  bool succeeded() const override { return success_; }
  juce::String getDescription() const override { return "Delete chair '" + juce::String(chair_.name) + "'"; }
private:
  struct Retained { juce::String id; int index; std::shared_ptr<MixerStrip> strip; };
  LibraryRoutingRepository &repository_;
  MixerModel &mixer_;
  std::string id_;
  ChairChanged changed_;
  ChairRow chair_;
  std::vector<LayerRow> layers_;
  std::vector<Retained> retained_;
  bool success_ = false, locked_ = false;
  std::optional<ChairLevelState> level_;
};

class AddChairAction final : public UndoableAction {
public:
  AddChairAction(LibraryRoutingRepository &repository, MixerModel &mixer,
                 ChairRow chair, ChairChanged changed)
      : repository_(repository), mixer_(mixer), chair_(std::move(chair)), changed_(std::move(changed)) {}
  void execute() override {
    if (removal_) { removal_->undo(); success_ = removal_->succeeded(); return; }
    success_ = repository_.insertChairWithStableAssignment(chair_);
    if (success_) removal_ = std::make_unique<RemoveChairAction>(repository_, mixer_, chair_.id, changed_);
    changed_(success_);
  }
  void undo() override {
    success_ = false;
    if (removal_) { removal_->execute(); success_ = removal_->succeeded(); }
  }
  bool succeeded() const override { return success_; }
  juce::String getDescription() const override { return "Add chair '" + juce::String(chair_.name) + "'"; }
private:
  LibraryRoutingRepository &repository_;
  MixerModel &mixer_;
  ChairRow chair_;
  ChairChanged changed_;
  std::unique_ptr<RemoveChairAction> removal_;
  bool success_ = false;
};

/// Name and role changes affect only chair metadata. Never reapply cached
/// layer rows here: their gains or player state may be older than live edits.
class EditChairsAction final : public UndoableAction {
public:
  struct Edit { std::string id; std::optional<std::string> name; std::optional<DoricoRole> role; };
  EditChairsAction(LibraryRoutingRepository &repository, MixerModel &mixer,
                   const std::vector<Edit> &edits, ChairChanged changed)
      : repository_(repository), mixer_(mixer), changed_(std::move(changed)) {
    auto working = repository_.listChairs();
    std::set<std::string> ids;
    for (const auto &edit : edits) {
      auto found = std::find_if(working.begin(), working.end(), [&](const auto &c) { return c.id == edit.id; });
      if (found == working.end() || !ids.insert(edit.id).second || (edit.name && edit.name->empty())) return;
      const auto before = *found;
      if (edit.name) found->name = *edit.name;
      if (edit.role && *edit.role != found->role) {
        const bool conflict = std::any_of(working.begin(), working.end(), [&](const auto &c) {
          return c.id != found->id && c.instrumentEntityId == found->instrumentEntityId &&
                 c.role == *edit.role && c.ordinal == found->ordinal;
        });
        if (conflict) {
          int maximum = 0;
          for (const auto &c : working)
            if (c.instrumentEntityId == found->instrumentEntityId && c.role == *edit.role)
              maximum = std::max(maximum, c.ordinal);
          found->ordinal = maximum + 1;
        }
        found->role = *edit.role;
      }
      if (before.name != found->name || before.role != found->role) {
        before_.push_back(before);
        after_.push_back(*found);
      }
    }
    valid_ = true;
  }
  void execute() override { apply(after_); }
  void undo() override { apply(before_); }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override { return valid_ && before_.empty(); }
  juce::String getDescription() const override {
    return after_.size() == 1 ? "Edit chair '" + juce::String(after_[0].name) + "'" : "Change chair player types";
  }
private:
  void apply(const std::vector<ChairRow> &rows) {
    success_ = valid_ && repository_.updateChairs(rows);
    if (success_) {
      for (const auto &chair : rows)
        for (auto *strip : mixer_.getAllStrips())
          if (strip->chairId.toStdString() == chair.id) {
            strip->family = juce::String(chair.family);
            strip->isSolo = chair.role == DoricoRole::solo;
          }
    }
    changed_(success_);
  }
  LibraryRoutingRepository &repository_;
  MixerModel &mixer_;
  ChairChanged changed_;
  std::vector<ChairRow> before_, after_;
  bool valid_ = false, success_ = false;
};

} // namespace fiddle
