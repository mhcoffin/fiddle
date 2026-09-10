#pragma once
#include "MixerModel.h"
#include "UndoManager.h"

namespace fiddle {
class SetProjectSettingsAction final : public UndoableAction {
public:
  SetProjectSettingsAction(MixerModel &mixer, ProjectSettings after,
                           juce::String label, juce::String coalesce = {})
      : mixer_(mixer), before_(mixer.projectSettings()), after_(std::move(after)),
        label_(std::move(label)), coalesce_(std::move(coalesce)) {}
  void execute() override { mixer_.setProjectSettings(after_); }
  void undo() override { mixer_.setProjectSettings(before_); }
  bool isNoOp() const override { return before_ == after_; }
  juce::String getDescription() const override { return label_; }
  juce::String getCoalesceId() const override { return coalesce_; }
  void coalesceWith(const UndoableAction &other) override {
    after_ = static_cast<const SetProjectSettingsAction &>(other).after_;
  }
private:
  MixerModel &mixer_;
  ProjectSettings before_, after_;
  juce::String label_, coalesce_;
};
} // namespace fiddle
