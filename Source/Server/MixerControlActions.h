#pragma once
#include "MixerModel.h"
#include "UndoManager.h"
#include <optional>

namespace fiddle {

/// A whole user gesture, including compensating sibling gains. Unlike a list
/// of unrelated setters, this has one before/after image and one history ID.
class SetMixerControlsAction final : public UndoableAction {
public:
  struct Values {
    float gainDb;
    bool muted, soloed, active;
    bool operator==(const Values &other) const {
      return gainDb == other.gainDb && muted == other.muted &&
             soloed == other.soloed && active == other.active;
    }
    static Values of(const MixerStrip &strip) {
      return {strip.gainDb(), strip.isMuted(), strip.isSoloed(), strip.isActive()};
    }
  };
  struct Change { juce::String id; Values before, after; };
  SetMixerControlsAction(MixerModel &mixer, std::vector<Change> changes,
                         juce::String label, bool coalesce = false,
                         std::optional<ProjectSettings> settings = std::nullopt)
      : mixer_(mixer), changes_(std::move(changes)), label_(std::move(label)), coalesce_(coalesce),
        beforeSettings_(mixer.projectSettings()), afterSettings_(std::move(settings)) {
    std::sort(changes_.begin(), changes_.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
    if (afterSettings_) {
      for (const auto &change : changes_)
        if (const auto *strip = mixer_.getStrip(change.id); strip && strip->chairId.isNotEmpty())
          settingsTargets_.insert(strip->chairId.toStdString());
      for (const auto &[id, state] : afterSettings_->chairLevels) {
        const auto old = beforeSettings_.chairLevels.find(id);
        if (old == beforeSettings_.chairLevels.end() || !(old->second == state)) settingsTargets_.insert(id);
      }
    }
  }
  void execute() override { apply(false); }
  void undo() override { apply(true); }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override {
    return (!afterSettings_ || *afterSettings_ == beforeSettings_) &&
        std::all_of(changes_.begin(), changes_.end(),
                      [](const auto &c) { return c.before == c.after; });
  }
  juce::String getDescription() const override { return label_; }
  juce::String getCoalesceId() const override { return coalesce_ ? label_ : juce::String{}; }
  bool canCoalesceWith(const UndoableAction &other) const override {
    if (!UndoableAction::canCoalesceWith(other)) return false;
    const auto &next = static_cast<const SetMixerControlsAction &>(other);
    if (afterSettings_.has_value() != next.afterSettings_.has_value()) return false;
    if (settingsTargets_ != next.settingsTargets_) return false;
    if (next.changes_.size() != changes_.size()) return false;
    for (size_t i = 0; i < changes_.size(); ++i)
      if (changes_[i].id != next.changes_[i].id) return false;
    return true;
  }
  void coalesceWith(const UndoableAction &other) override {
    const auto &next = static_cast<const SetMixerControlsAction &>(other);
    for (size_t i = 0; i < changes_.size(); ++i) changes_[i].after = next.changes_[i].after;
    afterSettings_ = next.afterSettings_;
  }
private:
  void apply(bool undo) {
    success_ = false;
    for (const auto &change : changes_)
      if (!mixer_.getStrip(change.id)) return; // Reject before changing anything.
    {
      AudioProcessingGate::Control control;
      for (const auto &change : changes_) {
        auto *strip = mixer_.getStrip(change.id);
        const auto &value = undo ? change.before : change.after;
        strip->setGainDb(value.gainDb);
        strip->setMuted(value.muted);
        strip->setSoloed(value.soloed);
        strip->setActive(value.active);
      }
    }
    // Message-thread metadata copies must not extend the audio processing pause.
    if (afterSettings_) mixer_.setProjectSettings(undo ? beforeSettings_ : *afterSettings_);
    success_ = true;
  }
  MixerModel &mixer_;
  std::vector<Change> changes_;
  juce::String label_;
  bool coalesce_ = false, success_ = true;
  ProjectSettings beforeSettings_;
  std::optional<ProjectSettings> afterSettings_;
  std::set<std::string> settingsTargets_;
};
} // namespace fiddle
