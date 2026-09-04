#pragma once

#include "MasterAudioEngine.h"
#include "MixerModel.h"
#include "UndoManager.h"

#include <utility>

namespace fiddle {

class AddMasterInsertAction final : public UndoableAction {
public:
  AddMasterInsertAction(MixerModel &mixer,
                        const juce::PluginDescription &description)
      : mixer_(mixer) {
    snapshot_.slotId = juce::Uuid().toString();
    snapshot_.description = description;
    index_ = mixer_.masterAudio().insertCount();
  }

  void execute() override {
    mixer_.masterAudio().insert(snapshot_, index_, mixer_.getFormatManager());
  }
  void undo() override { mixer_.masterAudio().remove(snapshot_.slotId); }
  juce::String getDescription() const override {
    return "Add " + snapshot_.description.name + " to Master";
  }

private:
  MixerModel &mixer_;
  MasterInsertSnapshot snapshot_;
  int index_ = 0;
};

class RemoveMasterInsertAction final : public UndoableAction {
public:
  RemoveMasterInsertAction(MixerModel &mixer, const juce::String &slotId)
      : mixer_(mixer), index_(mixer.masterAudio().indexOf(slotId)) {
    if (const auto snapshot = mixer.masterAudio().snapshot(slotId))
      snapshot_ = *snapshot;
  }

  void execute() override {
    if (snapshot_.slotId.isNotEmpty())
      mixer_.masterAudio().remove(snapshot_.slotId);
  }
  void undo() override {
    if (snapshot_.slotId.isNotEmpty())
      mixer_.masterAudio().insert(snapshot_, index_, mixer_.getFormatManager());
  }
  juce::String getDescription() const override {
    return "Remove " + snapshot_.description.name + " from Master";
  }

private:
  MixerModel &mixer_;
  MasterInsertSnapshot snapshot_;
  int index_ = -1;
};

class MoveMasterInsertAction final : public UndoableAction {
public:
  MoveMasterInsertAction(MixerModel &mixer, juce::String slotId, int newIndex)
      : mixer_(mixer), slotId_(std::move(slotId)),
        oldIndex_(mixer.masterAudio().indexOf(slotId_)), newIndex_(newIndex) {}

  void execute() override { mixer_.masterAudio().move(slotId_, newIndex_); }
  void undo() override { mixer_.masterAudio().move(slotId_, oldIndex_); }
  juce::String getDescription() const override { return "Move Master insert"; }

private:
  MixerModel &mixer_;
  juce::String slotId_;
  int oldIndex_ = -1;
  int newIndex_ = -1;
};

class BypassMasterInsertAction final : public UndoableAction {
public:
  BypassMasterInsertAction(MixerModel &mixer, juce::String slotId,
                           bool oldValue, bool newValue)
      : mixer_(mixer), slotId_(std::move(slotId)), oldValue_(oldValue),
        newValue_(newValue) {}

  void execute() override {
    mixer_.masterAudio().setBypassed(slotId_, newValue_);
  }
  void undo() override { mixer_.masterAudio().setBypassed(slotId_, oldValue_); }
  juce::String getDescription() const override {
    return newValue_ ? "Bypass Master insert" : "Enable Master insert";
  }

private:
  MixerModel &mixer_;
  juce::String slotId_;
  bool oldValue_ = false;
  bool newValue_ = false;
};

class SetMasterGainAction final : public UndoableAction {
public:
  SetMasterGainAction(MixerModel &mixer, float oldGain, float newGain)
      : mixer_(mixer), oldGain_(oldGain), newGain_(newGain) {}

  void execute() override { mixer_.masterAudio().setGainDb(newGain_); }
  void undo() override { mixer_.masterAudio().setGainDb(oldGain_); }
  juce::String getDescription() const override {
    return "Set Master gain to " + juce::String(newGain_, 1) + " dB";
  }
  juce::String getCoalesceId() const override { return "gain:master"; }
  void coalesceWith(const UndoableAction &newer) override {
    newGain_ = static_cast<const SetMasterGainAction &>(newer).newGain_;
  }

private:
  MixerModel &mixer_;
  float oldGain_ = 0.0f;
  float newGain_ = 0.0f;
};

} // namespace fiddle
