#pragma once

#include "MixerModel.h"
#include "UndoManager.h"

#include <utility>

namespace fiddle {

class AddStripInsertAction final : public UndoableAction {
public:
  AddStripInsertAction(MixerModel &mixer, juce::String stripId,
                       const juce::PluginDescription &description,
                       StripInsertPosition position)
      : mixer_(mixer), stripId_(std::move(stripId)), position_(position) {
    snapshot_.slotId = juce::Uuid().toString();
    snapshot_.description = description;
    if (auto *strip = mixer_.getStrip(stripId_))
      index_ = strip->audioEngine().insertCount(position_);
  }

  void execute() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().insert(snapshot_, position_, index_,
                                  mixer_.getFormatManager());
  }
  void undo() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().remove(snapshot_.slotId);
  }
  juce::String getDescription() const override {
    return "Add " + snapshot_.description.name + " to strip";
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  StripInsertPosition position_;
  AudioInsertSnapshot snapshot_;
  int index_ = 0;
};

class RemoveStripInsertAction final : public UndoableAction {
public:
  RemoveStripInsertAction(MixerModel &mixer, juce::String stripId,
                          const juce::String &slotId)
      : mixer_(mixer), stripId_(std::move(stripId)) {
    if (auto *strip = mixer_.getStrip(stripId_)) {
      if (const auto position = strip->audioEngine().positionOf(slotId)) {
        position_ = *position;
        index_ = strip->audioEngine().indexOf(slotId, *position);
      }
      if (const auto snapshot = strip->audioEngine().snapshot(slotId))
        snapshot_ = *snapshot;
    }
  }

  void execute() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().remove(snapshot_.slotId);
  }
  void undo() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().insert(snapshot_, position_, index_,
                                  mixer_.getFormatManager());
  }
  juce::String getDescription() const override {
    return "Remove " + snapshot_.description.name + " from strip";
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  StripInsertPosition position_ = StripInsertPosition::preFader;
  AudioInsertSnapshot snapshot_;
  int index_ = 0;
};

class MoveStripInsertAction final : public UndoableAction {
public:
  MoveStripInsertAction(MixerModel &mixer, juce::String stripId,
                        juce::String slotId,
                        StripInsertPosition newPosition, int newIndex)
      : mixer_(mixer), stripId_(std::move(stripId)),
        slotId_(std::move(slotId)), newPosition_(newPosition),
        newIndex_(newIndex) {
    if (auto *strip = mixer_.getStrip(stripId_)) {
      if (const auto position = strip->audioEngine().positionOf(slotId_)) {
        oldPosition_ = *position;
        oldIndex_ = strip->audioEngine().indexOf(slotId_, *position);
      }
    }
  }

  void execute() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().move(slotId_, newPosition_, newIndex_);
  }
  void undo() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().move(slotId_, oldPosition_, oldIndex_);
  }
  juce::String getDescription() const override { return "Move strip insert"; }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  juce::String slotId_;
  StripInsertPosition oldPosition_ = StripInsertPosition::preFader;
  StripInsertPosition newPosition_ = StripInsertPosition::preFader;
  int oldIndex_ = 0;
  int newIndex_ = 0;
};

class BypassStripInsertAction final : public UndoableAction {
public:
  BypassStripInsertAction(MixerModel &mixer, juce::String stripId,
                          juce::String slotId, bool oldValue, bool newValue)
      : mixer_(mixer), stripId_(std::move(stripId)),
        slotId_(std::move(slotId)), oldValue_(oldValue),
        newValue_(newValue) {}

  void execute() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().setBypassed(slotId_, newValue_);
  }
  void undo() override {
    if (auto *strip = mixer_.getStrip(stripId_))
      strip->audioEngine().setBypassed(slotId_, oldValue_);
  }
  juce::String getDescription() const override {
    return newValue_ ? "Bypass strip insert" : "Enable strip insert";
  }

private:
  MixerModel &mixer_;
  juce::String stripId_;
  juce::String slotId_;
  bool oldValue_ = false;
  bool newValue_ = false;
};

} // namespace fiddle
