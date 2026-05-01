#pragma once

#include "Annotator.h"
#include <memory>
#include <vector>

namespace fiddle {

/**
 * Composes N annotators in sequence. Each annotator's onNoteStart/onNoteEnd
 * is called in order, allowing them to mutate the Note cumulatively.
 *
 * Typical chain: [LuaAnnotator 1] → [LuaAnnotator 2] → [ExpressionMapAnnotator]
 *
 * The last annotator in the chain should be the ExpressionMapAnnotator
 * (if one is assigned), which translates technique names into concrete MIDI.
 */
class AnnotatorChain : public Annotator {
public:
  AnnotatorChain() = default;

  /// Add an annotator to the end of the chain.
  void add(std::unique_ptr<Annotator> annotator) {
    if (annotator)
      chain_.push_back(std::move(annotator));
  }

  /// Number of annotators in the chain.
  size_t size() const { return chain_.size(); }

  /// Access an annotator by index (for inspection).
  Annotator *at(size_t index) const {
    return index < chain_.size() ? chain_[index].get() : nullptr;
  }

  void onNoteStart(fiddle::Note &note,
                   const AnnotatorContext &ctx) override {
    for (auto &annotator : chain_)
      annotator->onNoteStart(note, ctx);
  }

  void onNoteEnd(fiddle::Note &note, const AnnotatorContext &ctx) override {
    for (auto &annotator : chain_)
      annotator->onNoteEnd(note, ctx);
  }

  bool onCC(const fiddle::MidiEvent &event,
            const AnnotatorContext &ctx) override {
    // Each annotator can suppress the CC. If any returns false, the CC is
    // consumed and not forwarded.
    for (auto &annotator : chain_) {
      if (!annotator->onCC(event, ctx))
        return false;
    }
    return true;
  }

  std::string name() const override {
    if (chain_.empty())
      return "Empty Chain";
    std::string result;
    for (size_t i = 0; i < chain_.size(); ++i) {
      if (i > 0)
        result += " → ";
      result += chain_[i]->name();
    }
    return result;
  }

  double durationAdjustMs() const override {
    // Sum adjustments from all annotators in the chain so both Lua plugins
    // and the ExpressionMapAnnotator can contribute duration adjustments.
    double total = 0.0;
    for (const auto &a : chain_)
      total += a->durationAdjustMs();
    return total;
  }

  double scheduledNoteOffMs() const override {
    // Return the smallest positive value — the earliest scheduled note-off wins.
    double earliest = -1.0;
    for (const auto &a : chain_) {
      double v = a->scheduledNoteOffMs();
      if (v > 0 && (earliest < 0 || v < earliest))
        earliest = v;
    }
    return earliest;
  }

  void resetState() override {
    for (auto &annotator : chain_)
      annotator->resetState();
  }

  const AnnotationRecord *lastAnnotationRecord() const override {
    // Return the last annotator's record that has one (typically the EM annotator)
    for (auto it = chain_.rbegin(); it != chain_.rend(); ++it) {
      if (auto *rec = (*it)->lastAnnotationRecord())
        return rec;
    }
    return nullptr;
  }

private:
  std::vector<std::unique_ptr<Annotator>> chain_;
};

} // namespace fiddle
