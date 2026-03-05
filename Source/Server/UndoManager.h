#pragma once

#include <iostream>
#include <juce_core/juce_core.h>
#include <memory>
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
};

/// Manages undo/redo stacks using the Command pattern.
class UndoManager {
public:
  static constexpr int kMaxStackDepth = 100;
  static constexpr int kCoalesceWindowMs = 500;

  /// Perform an action: execute it, push to undo stack, clear redo stack.
  /// If the action coalesces with the top of the stack, merge instead.
  void perform(std::unique_ptr<UndoableAction> action) {
    action->execute();

    auto coalesceId = action->getCoalesceId();
    if (coalesceId.isNotEmpty() && !undoStack_.empty()) {
      auto &top = undoStack_.back();
      if (top->getCoalesceId() == coalesceId) {
        auto now = juce::Time::getMillisecondCounter();
        if (now - lastActionTimeMs_ < (uint32_t)kCoalesceWindowMs) {
          // Merge: keep the old "before" state, update "after" state
          top->coalesceWith(*action);
          lastActionTimeMs_ = now;
          return;
        }
      }
    }

    undoStack_.push_back(std::move(action));
    redoStack_.clear();
    lastActionTimeMs_ = juce::Time::getMillisecondCounter();

    // Trim stack if too large
    while ((int)undoStack_.size() > kMaxStackDepth)
      undoStack_.erase(undoStack_.begin());
  }

  /// Undo the most recent action. Returns true if something was undone.
  bool undo() {
    if (undoStack_.empty())
      return false;
    auto action = std::move(undoStack_.back());
    undoStack_.pop_back();
    std::cerr << "[Undo] " << action->getDescription() << std::endl;
    action->undo();
    redoStack_.push_back(std::move(action));
    return true;
  }

  /// Redo the most recently undone action. Returns true if something was
  /// redone.
  bool redo() {
    if (redoStack_.empty())
      return false;
    auto action = std::move(redoStack_.back());
    redoStack_.pop_back();
    std::cerr << "[Redo] " << action->getDescription() << std::endl;
    action->execute();
    undoStack_.push_back(std::move(action));
    return true;
  }

  bool canUndo() const { return !undoStack_.empty(); }
  bool canRedo() const { return !redoStack_.empty(); }

  /// Clear all undo/redo history (e.g. on config load).
  void clear() {
    undoStack_.clear();
    redoStack_.clear();
    savePointIndex_ = 0;
  }

  /// Mark the current undo stack position as the "saved" state.
  void markSavePoint() { savePointIndex_ = (int)undoStack_.size(); }

  /// Returns true if the undo stack is at the save point (i.e. no unsaved
  /// changes relative to the last save).
  bool isAtSavePoint() const {
    return (int)undoStack_.size() == savePointIndex_;
  }

private:
  std::vector<std::unique_ptr<UndoableAction>> undoStack_;
  std::vector<std::unique_ptr<UndoableAction>> redoStack_;
  uint32_t lastActionTimeMs_ = 0;
  int savePointIndex_ = 0; ///< Undo stack size at last save
};

} // namespace fiddle
