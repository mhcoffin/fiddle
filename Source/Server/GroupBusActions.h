#pragma once

#include "MixerModel.h"
#include "UndoManager.h"

namespace fiddle {

class AddGroupBusAction final : public UndoableAction {
public:
  AddGroupBusAction(MixerModel &mixer, juce::String name,
                    std::vector<juce::String> stripIds)
      : mixer_(mixer), name_(std::move(name)), stripIds_(std::move(stripIds)) {
    for (const auto &id : stripIds_)
      if (auto *strip = mixer_.getStrip(id))
        previousOutputs_.push_back({id, strip->directOutputBusId});
  }

  void execute() override {
    if (removedBus_) {
      mixer_.insertGroupBusAt(std::move(removedBus_), index_);
    } else {
      busId_ = mixer_.addGroupBus(name_);
      index_ = mixer_.groupBusIndex(busId_);
    }
    for (const auto &id : stripIds_)
      mixer_.setStripDirectOutput(id, busId_);
  }

  void undo() override {
    for (const auto &[id, output] : previousOutputs_)
      mixer_.setStripDirectOutput(id, output);
    index_ = mixer_.groupBusIndex(busId_);
    removedBus_ = mixer_.removeGroupBusKeepAlive(busId_);
  }

  juce::String getDescription() const override { return "Add group bus"; }

private:
  MixerModel &mixer_;
  juce::String name_;
  juce::String busId_;
  std::vector<juce::String> stripIds_;
  std::vector<std::pair<juce::String, juce::String>> previousOutputs_;
  std::shared_ptr<GroupBus> removedBus_;
  int index_ = 0;
};

class RemoveGroupBusAction final : public UndoableAction {
public:
  RemoveGroupBusAction(MixerModel &mixer, juce::String busId)
      : mixer_(mixer), busId_(std::move(busId)) {}

  void execute() override {
    affectedRoutes_.clear();
    for (auto *strip : mixer_.getAllStrips())
      if (strip->directOutputBusId == busId_)
        affectedRoutes_.push_back(strip->id);
    index_ = mixer_.groupBusIndex(busId_);
    removedBus_ = mixer_.removeGroupBusKeepAlive(busId_);
  }

  void undo() override {
    if (!removedBus_)
      return;
    mixer_.insertGroupBusAt(std::move(removedBus_), index_);
    for (const auto &stripId : affectedRoutes_)
      mixer_.setStripDirectOutput(stripId, busId_);
  }

  juce::String getDescription() const override { return "Remove group bus"; }

private:
  MixerModel &mixer_;
  juce::String busId_;
  std::vector<juce::String> affectedRoutes_;
  std::shared_ptr<GroupBus> removedBus_;
  int index_ = 0;
};

class SetStripOutputAction final : public UndoableAction {
public:
  SetStripOutputAction(MixerModel &mixer, juce::String stripId,
                       juce::String oldOutput, juce::String newOutput)
      : mixer_(mixer), stripId_(std::move(stripId)),
        oldOutput_(std::move(oldOutput)), newOutput_(std::move(newOutput)) {}
  void execute() override { mixer_.setStripDirectOutput(stripId_, newOutput_); }
  void undo() override { mixer_.setStripDirectOutput(stripId_, oldOutput_); }
  juce::String getDescription() const override { return "Change strip output"; }

private:
  MixerModel &mixer_;
  juce::String stripId_, oldOutput_, newOutput_;
};

class RenameGroupBusAction final : public UndoableAction {
public:
  RenameGroupBusAction(MixerModel &mixer, juce::String id,
                       juce::String oldName, juce::String newName)
      : mixer_(mixer), id_(std::move(id)), old_(std::move(oldName)),
        value_(std::move(newName)) {}
  void execute() override { mixer_.renameGroupBus(id_, value_); }
  void undo() override { mixer_.renameGroupBus(id_, old_); }
  juce::String getDescription() const override { return "Rename group bus"; }
private:
  MixerModel &mixer_;
  juce::String id_, old_, value_;
};

class MoveGroupBusAction final : public UndoableAction {
public:
  MoveGroupBusAction(MixerModel &mixer, juce::String id, int oldIndex,
                     int newIndex)
      : mixer_(mixer), id_(std::move(id)), old_(oldIndex), value_(newIndex) {}
  void execute() override { mixer_.moveGroupBus(id_, value_); }
  void undo() override { mixer_.moveGroupBus(id_, old_); }
  juce::String getDescription() const override { return "Move group bus"; }
private:
  MixerModel &mixer_;
  juce::String id_;
  int old_, value_;
};

class SetGroupBusGainAction final : public UndoableAction {
public:
  SetGroupBusGainAction(MixerModel &mixer, juce::String id, float oldGain,
                        float newGain)
      : mixer_(mixer), id_(std::move(id)), old_(oldGain), value_(newGain) {}
  void execute() override { mixer_.setGroupBusGain(id_, value_); }
  void undo() override { mixer_.setGroupBusGain(id_, old_); }
  juce::String getDescription() const override { return "Set group bus gain"; }
  juce::String getCoalesceId() const override { return "bus-gain:" + id_; }
  void coalesceWith(const UndoableAction &newer) override {
    value_ = static_cast<const SetGroupBusGainAction &>(newer).value_;
  }
private:
  MixerModel &mixer_;
  juce::String id_;
  float old_, value_;
};

class SetGroupBusMuteAction final : public UndoableAction {
public:
  SetGroupBusMuteAction(MixerModel &mixer, juce::String id, bool oldValue,
                        bool newValue)
      : mixer_(mixer), id_(std::move(id)), old_(oldValue), value_(newValue) {}
  void execute() override { mixer_.setGroupBusMute(id_, value_); }
  void undo() override { mixer_.setGroupBusMute(id_, old_); }
  juce::String getDescription() const override { return "Mute group bus"; }
private:
  MixerModel &mixer_;
  juce::String id_;
  bool old_, value_;
};

class SetGroupBusSoloAction final : public UndoableAction {
public:
  SetGroupBusSoloAction(MixerModel &mixer, juce::String id, bool oldValue,
                        bool newValue)
      : mixer_(mixer), id_(std::move(id)), old_(oldValue), value_(newValue) {}
  void execute() override { mixer_.setGroupBusSolo(id_, value_); }
  void undo() override { mixer_.setGroupBusSolo(id_, old_); }
  juce::String getDescription() const override { return "Solo group bus"; }
private:
  MixerModel &mixer_;
  juce::String id_;
  bool old_, value_;
};

class AddGroupBusInsertAction final : public UndoableAction {
public:
  AddGroupBusInsertAction(MixerModel &mixer, juce::String busId,
                          const juce::PluginDescription &description,
                          StripInsertPosition position)
      : mixer_(mixer), busId_(std::move(busId)), position_(position) {
    snapshot_.slotId = juce::Uuid().toString();
    snapshot_.description = description;
    if (auto *bus = mixer_.getGroupBus(busId_))
      index_ = bus->audioEngine().insertCount(position_);
  }

  void execute() override {
    success_ = false;
    if (auto *bus = mixer_.getGroupBus(busId_))
      success_ = bus->audioEngine().insert(
          snapshot_, position_, index_, mixer_.getFormatManager(),
          [mixer = &mixer_](bool, const juce::String &) {
            mixer->refreshAudioRouting();
          });
    mixer_.refreshAudioRouting();
  }

  void undo() override {
    success_ = false;
    if (auto *bus = mixer_.getGroupBus(busId_)) {
      if (const auto state = bus->audioEngine().snapshot(snapshot_.slotId)) {
        snapshot_ = *state;
        success_ = bus->audioEngine().remove(snapshot_.slotId);
      }
    }
    mixer_.refreshAudioRouting();
  }

  juce::String getDescription() const override {
    return "Add " + snapshot_.description.name + " to group bus";
  }
  bool succeeded() const override { return success_; }

private:
  MixerModel &mixer_;
  juce::String busId_;
  StripInsertPosition position_;
  AudioInsertSnapshot snapshot_;
  int index_ = 0;
  bool success_ = false;
};

class RemoveGroupBusInsertAction final : public UndoableAction {
public:
  RemoveGroupBusInsertAction(MixerModel &mixer, juce::String busId,
                             const juce::String &slotId)
      : mixer_(mixer), busId_(std::move(busId)) {
    if (auto *bus = mixer_.getGroupBus(busId_)) {
      if (const auto position = bus->audioEngine().positionOf(slotId)) {
        position_ = *position;
        index_ = bus->audioEngine().indexOf(slotId, *position);
      }
      if (const auto snapshot = bus->audioEngine().snapshot(slotId))
        snapshot_ = *snapshot;
    }
  }

  void execute() override {
    success_ = false;
    if (auto *bus = mixer_.getGroupBus(busId_)) {
      if (const auto state = bus->audioEngine().snapshot(snapshot_.slotId)) {
        snapshot_ = *state;
        success_ = bus->audioEngine().remove(snapshot_.slotId);
      }
    }
    mixer_.refreshAudioRouting();
  }

  void undo() override {
    success_ = false;
    if (auto *bus = mixer_.getGroupBus(busId_))
      success_ = bus->audioEngine().insert(
          snapshot_, position_, index_, mixer_.getFormatManager(),
          [mixer = &mixer_](bool, const juce::String &) {
            mixer->refreshAudioRouting();
          });
    mixer_.refreshAudioRouting();
  }

  juce::String getDescription() const override {
    return "Remove " + snapshot_.description.name + " from group bus";
  }
  bool succeeded() const override { return success_; }

private:
  MixerModel &mixer_;
  juce::String busId_;
  StripInsertPosition position_ = StripInsertPosition::preFader;
  AudioInsertSnapshot snapshot_;
  int index_ = 0;
  bool success_ = false;
};

class MoveGroupBusInsertAction final : public UndoableAction {
public:
  MoveGroupBusInsertAction(MixerModel &mixer, juce::String busId,
                           juce::String slotId,
                           StripInsertPosition newPosition, int newIndex)
      : mixer_(mixer), busId_(std::move(busId)), slotId_(std::move(slotId)),
        newPosition_(newPosition), newIndex_(newIndex) {
    if (auto *bus = mixer_.getGroupBus(busId_)) {
      if (const auto position = bus->audioEngine().positionOf(slotId_)) {
        oldPosition_ = *position;
        oldIndex_ = bus->audioEngine().indexOf(slotId_, *position);
      }
    }
  }

  void execute() override { move(newPosition_, newIndex_); }
  void undo() override { move(oldPosition_, oldIndex_); }
  juce::String getDescription() const override { return "Move group bus insert"; }

private:
  void move(StripInsertPosition position, int index) {
    if (auto *bus = mixer_.getGroupBus(busId_))
      bus->audioEngine().move(slotId_, position, index);
    mixer_.refreshAudioRouting();
  }
  MixerModel &mixer_;
  juce::String busId_, slotId_;
  StripInsertPosition oldPosition_ = StripInsertPosition::preFader;
  StripInsertPosition newPosition_ = StripInsertPosition::preFader;
  int oldIndex_ = 0, newIndex_ = 0;
};

class BypassGroupBusInsertAction final : public UndoableAction {
public:
  BypassGroupBusInsertAction(MixerModel &mixer, juce::String busId,
                             juce::String slotId, bool oldValue,
                             bool newValue)
      : mixer_(mixer), busId_(std::move(busId)), slotId_(std::move(slotId)),
        oldValue_(oldValue), newValue_(newValue) {}

  void execute() override { set(newValue_); }
  void undo() override { set(oldValue_); }
  juce::String getDescription() const override {
    return newValue_ ? "Bypass group bus insert" : "Enable group bus insert";
  }

private:
  void set(bool bypassed) {
    if (auto *bus = mixer_.getGroupBus(busId_))
      bus->audioEngine().setBypassed(slotId_, bypassed);
    mixer_.refreshAudioRouting();
  }
  MixerModel &mixer_;
  juce::String busId_, slotId_;
  bool oldValue_ = false, newValue_ = false;
};

} // namespace fiddle
