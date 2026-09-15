#pragma once

#include "LibraryRoutingRepository.h"
#include "UndoManager.h"

namespace fiddle {

class LibraryCatalogAction final : public UndoableAction {
public:
  LibraryCatalogAction(LibraryRoutingRepository &repository,
                       LibraryCatalogSnapshot target)
      : repository_(repository), after_(std::move(target)),
        before_(repository.captureLibrary(after_.id)) {}
  void execute() override {
    apply(after_, executed_);
    if (success_ && !executed_) {
      if (auto saved = repository_.captureLibrary(after_.id)) after_ = std::move(*saved);
      executed_ = true;
    }
  }
  void undo() override { if (before_) apply(*before_); else success_ = false; }
  bool succeeded() const override { return success_; }
  bool isNoOp() const override {
    if (!before_ || before_->exists != after_.exists) return false;
    if (!after_.exists) return true;
    if (before_->name != after_.name || before_->vendor != after_.vendor ||
        before_->variant != after_.variant || before_->patches.size() != after_.patches.size()) return false;
    for (std::size_t i = 0; i < after_.patches.size(); ++i) {
      const auto &a = before_->patches[i]; const auto &b = after_.patches[i];
      if (a.id != b.id || a.libraryId != b.libraryId || a.position != b.position ||
          a.name != b.name || a.instrumentEntityId != b.instrumentEntityId ||
          a.family != b.family || a.character != b.character || a.pluginUid != b.pluginUid ||
          a.pluginState != b.pluginState || a.expressionMapId != b.expressionMapId ||
          a.expectedPresetName != b.expectedPresetName ||
          a.setupComplete != b.setupComplete) return false;
    }
    return true;
  }
  juce::String getDescription() const override {
    return (after_.exists ? "Save library: " : "Delete library: ") +
      juce::String(after_.exists ? after_.name : before_ ? before_->name : after_.id);
  }
private:
  void apply(const LibraryCatalogSnapshot &snapshot, bool restoreRevisions = true) {
    success_ = before_.has_value() &&
      repository_.restoreLibrary(snapshot, restoreRevisions) == PatchReplaceResult::replaced;
  }
  LibraryRoutingRepository &repository_;
  LibraryCatalogSnapshot after_;
  std::optional<LibraryCatalogSnapshot> before_;
  bool success_ = false;
  bool executed_ = false;
};

} // namespace fiddle
