#pragma once

#include <iostream>
#include <juce_core/juce_core.h>
#include <memory>
#include <functional>
#include <typeinfo>
#include <vector>

namespace fiddle {

/// Abstract base for all undoable actions.
class UndoableAction {
public:
  virtual ~UndoableAction() = default;

  /// Execute the action (called on first perform and on redo).
  virtual void execute() = 0;

  /// Reverse the action.
  virtual void undo() = 0;

  /// Human-readable description (for debugging/UI).
  virtual juce::String getDescription() const = 0;

  /// ID used for coalescing. Actions with the same coalesceId that arrive
  /// within the coalesce window are merged (the new action's "after" state
  /// replaces the top of the undo stack's "after" state).
  /// Return empty string to disable coalescing for this action type.
  virtual juce::String getCoalesceId() const { return {}; }

  /// Called when coalescing: absorb the new action's target state.
  /// Default does nothing. Override for coalescing actions (e.g. gain).
  virtual void coalesceWith(const UndoableAction &) {}

  virtual bool canCoalesceWith(const UndoableAction &other) const {
    return typeid(*this) == typeid(other) && getCoalesceId().isNotEmpty() &&
           getCoalesceId() == other.getCoalesceId();
  }

  // Incremental migration seam for actions with fallible model/database work.
  // A rejected operation must not move history or mark a new saved identity.
  virtual bool succeeded() const { return true; }
  virtual bool isNoOp() const { return false; }
};

/// Manages undo/redo stacks using the Command pattern.
class UndoManager {
public:
  static constexpr int kMaxStackDepth = 100;
  static constexpr int kCoalesceWindowMs = 500;

  /// Perform an action: execute it, push to undo stack, clear redo stack.
  /// If the action coalesces with the top of the stack, merge instead.
  bool perform(std::unique_ptr<UndoableAction> action) {
    if (!action || action->isNoOp())
      return false;
    action->execute();
    if (!action->succeeded())
      return false;
    const auto before = currentState_;
    currentState_ = ++nextState_;
    // This applies to *every* new edit, including the coalescing path.
    redoStack_.clear();
    const auto now = juce::Time::getMillisecondCounter();
    if (!undoStack_.empty()) {
      auto &top = undoStack_.back();
      const bool sameGesture = top.epoch == epoch_ &&
          (gestureActive_ || now - lastActionTimeMs_ < kCoalesceWindowMs);
      if (sameGesture && top.action->canCoalesceWith(*action)) {
        top.action->coalesceWith(*action);
        top.after = currentState_;
        lastActionTimeMs_ = now;
        notify();
        return true;
      }
    }
    undoStack_.push_back({std::move(action), before, currentState_, epoch_});
    lastActionTimeMs_ = now;
    while ((int)undoStack_.size() > kMaxStackDepth)
      undoStack_.erase(undoStack_.begin());
    notify();
    return true;
  }

  /// Undo the most recent action. Returns true if something was undone.
  bool undo() {
    breakCoalescing();
    if (undoStack_.empty())
      return false;
    auto &entry = undoStack_.back();
    std::cerr << "[Undo] " << entry.action->getDescription() << std::endl;
    entry.action->undo();
    if (!entry.action->succeeded())
      return false;
    currentState_ = entry.before;
    redoStack_.push_back(std::move(entry));
    undoStack_.pop_back();
    notify();
    return true;
  }

  /// Redo the most recently undone action. Returns true if something was
  /// redone.
  bool redo() {
    breakCoalescing();
    if (redoStack_.empty())
      return false;
    auto &entry = redoStack_.back();
    std::cerr << "[Redo] " << entry.action->getDescription() << std::endl;
    entry.action->execute();
    if (!entry.action->succeeded())
      return false;
    currentState_ = entry.after;
    undoStack_.push_back(std::move(entry));
    redoStack_.pop_back();
    notify();
    return true;
  }

  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }
  juce::String undoDescription() const {
    return canUndo() ? undoStack_.back().action->getDescription() : juce::String{};
  }
  juce::String redoDescription() const {
    return canRedo() ? redoStack_.back().action->getDescription() : juce::String{};
  }
  void beginGesture() { breakCoalescing(); gestureActive_ = true; }
  void endGesture() { breakCoalescing(); }
  void breakCoalescing() { ++epoch_; gestureActive_ = false; }
  void noteExternalChange() {
    breakCoalescing();
    ++externalRevision_;
    redoStack_.clear();
    notify();
  }
  std::function<void()> onChanged;

  /// Clear all undo/redo history (e.g. on config load).
  void clear() {
    undoStack_.clear();
    redoStack_.clear();
    currentState_ = ++nextState_;
    externalRevision_ = 0;
    markSavePoint();
  }

  /// Mark the current undo stack position as the "saved" state.
  void markSavePoint() {
    breakCoalescing();
    savedState_ = currentState_;
    savedExternalRevision_ = externalRevision_;
    notify();
  }

  /// Returns true if the undo stack is at the save point (i.e. no unsaved
  /// changes relative to the last save).
  bool isAtSavePoint() const {
    return currentState_ == savedState_ &&
           externalRevision_ == savedExternalRevision_;
  }

private:
  struct Entry {
    std::unique_ptr<UndoableAction> action;
    uint64_t before, after, epoch;
  };
  void notify() { if (onChanged) onChanged(); }
  std::vector<Entry> undoStack_, redoStack_;
  uint32_t lastActionTimeMs_ = 0;
  uint64_t currentState_ = 0, savedState_ = 0, nextState_ = 0, epoch_ = 0;
  uint64_t externalRevision_ = 0, savedExternalRevision_ = 0;
  bool gestureActive_ = false;
};

} // namespace fiddle
