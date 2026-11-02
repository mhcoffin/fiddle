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
    run(note, ctx, &Annotator::onNoteStart);
  }

  void onNoteEnd(fiddle::Note &note, const AnnotatorContext &ctx) override {
    run(note, ctx, &Annotator::onNoteEnd);
  }

  bool noteSuppressed() const override { return suppressedBy_ != nullptr; }

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

  void translateCC(const fiddle::MidiEvent &event, const AnnotatorContext &ctx,
                   std::vector<fiddle::MidiInstruction> &out) override {
    for (auto &annotator : chain_)
      annotator->translateCC(event, ctx, out);
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
    for (size_t i = 0; i < ran_; ++i)
      total += chain_[i]->durationAdjustMs();
    return total;
  }

  double noteStartOffsetMs() const override {
    double total = 0.0;
    for (size_t i = 0; i < ran_; ++i)
      total += chain_[i]->noteStartOffsetMs();
    return total;
  }

  double controllerOffsetMs() const override {
    double total = 0.0;
    for (const auto &a : chain_)
      total += a->controllerOffsetMs();
    return total;
  }

  void onTransportStart(const AnnotatorContext &ctx,
                        std::vector<fiddle::MidiInstruction> &out) override {
    for (auto &annotator : chain_)
      annotator->onTransportStart(ctx, out);
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
    // The last annotator that ran and has a record (typically the EM
    // annotator), with an earlier percussion-map decision folded in.
    const AnnotationRecord *last = nullptr, *percussion = nullptr;
    for (size_t i = 0; i < ran_; ++i) {
      if (auto *rec = chain_[i]->lastAnnotationRecord()) {
        last = rec;
        if (!rec->percussion.mapName.empty())
          percussion = rec;
      }
    }
    if (!percussion || percussion == last)
      return last;
    merged_ = *last;
    merged_.percussion = percussion->percussion;
    return &merged_;
  }

private:
  std::vector<std::unique_ptr<Annotator>> chain_;
  size_t ran_ = 0;                        // annotators that saw the last note
  const Annotator *suppressedBy_ = nullptr;
  mutable AnnotationRecord merged_;

  // A withheld note goes no further down the chain.
  void run(fiddle::Note &note, const AnnotatorContext &ctx,
           void (Annotator::*step)(fiddle::Note &, const AnnotatorContext &)) {
    suppressedBy_ = nullptr;
    ran_ = 0;
    for (auto &annotator : chain_) {
      ((*annotator).*step)(note, ctx);
      ++ran_;
      if (annotator->noteSuppressed()) {
        suppressedBy_ = annotator.get();
        break;
      }
    }
  }
};

} // namespace fiddle
