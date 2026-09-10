#pragma once
#include "MixerModel.h"
#include "UndoManager.h"

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
                         juce::String label, bool coalesce = false)
      : mixer_(mixer), changes_(std::move(changes)), label_(std::move(label)), coalesce_(coalesce) {
    std::sort(changes_.begin(), changes_.end(),
              [](const auto &a, const auto &b) { return a.id < b.id; });
  }
  void execute() override { apply(false); }
  void undo() override { apply(true); }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override {
    return std::all_of(changes_.begin(), changes_.end(),
                      [](const auto &c) { return c.before == c.after; });
  }
  juce::String getDescription() const override { return label_; }
  juce::String getCoalesceId() const override { return coalesce_ ? label_ : juce::String{}; }
  bool canCoalesceWith(const UndoableAction &other) const override {
    if (!UndoableAction::canCoalesceWith(other)) return false;
    const auto &next = static_cast<const SetMixerControlsAction &>(other);
    if (next.changes_.size() != changes_.size()) return false;
    for (size_t i = 0; i < changes_.size(); ++i)
      if (changes_[i].id != next.changes_[i].id) return false;
    return true;
  }
  void coalesceWith(const UndoableAction &other) override {
    const auto &next = static_cast<const SetMixerControlsAction &>(other);
    for (size_t i = 0; i < changes_.size(); ++i) changes_[i].after = next.changes_[i].after;
  }
private:
  void apply(bool undo) {
    success_ = false;
    for (const auto &change : changes_)
      if (!mixer_.getStrip(change.id)) return; // Reject before changing anything.
    AudioProcessingGate::Control control;
    for (const auto &change : changes_) {
      auto *strip = mixer_.getStrip(change.id);
      const auto &value = undo ? change.before : change.after;
      strip->setGainDb(value.gainDb);
      strip->setMuted(value.muted);
      strip->setSoloed(value.soloed);
      strip->setActive(value.active);
    }
    success_ = true;
  }
  MixerModel &mixer_;
  std::vector<Change> changes_;
  juce::String label_;
  bool coalesce_ = false, success_ = true;
};
} // namespace fiddle
