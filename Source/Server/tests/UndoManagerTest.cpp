#include "UndoManager.h"
#include <stdexcept>

#define CHECK(value) do { if (!(value)) throw std::runtime_error(#value); } while (false)

struct ValueAction final : fiddle::UndoableAction {
  int &value;
  int before, after;
  juce::String key;
  bool fail = false;
  ValueAction(int &v, int next, juce::String k = {}) : value(v), before(v), after(next), key(k) {}
  void execute() override { if (!fail) value = after; }
  void undo() override { if (!fail) value = before; }
  bool succeeded() const override { return !fail; }
  bool isNoOp() const override { return before == after; }
  juce::String getDescription() const override { return "Change " + key; }
  juce::String getCoalesceId() const override { return key; }
  void coalesceWith(const fiddle::UndoableAction &other) override {
    after = static_cast<const ValueAction &>(other).after;
  }
};

int main() {
  try {
    {
      fiddle::UndoManager history;
      int a = 0, b = 0;
      history.perform(std::make_unique<ValueAction>(a, 1, "a"));
      history.markSavePoint();
      CHECK(history.undo());
      history.perform(std::make_unique<ValueAction>(b, 2, "b"));
      CHECK(!history.isAtSavePoint() && !history.canRedo());
    }
    {
      fiddle::UndoManager history;
      int a = 0;
      history.perform(std::make_unique<ValueAction>(a, 1, "a"));
      history.markSavePoint();
      history.perform(std::make_unique<ValueAction>(a, 2, "a"));
      CHECK(!history.isAtSavePoint());
      CHECK(history.undo() && a == 1 && history.isAtSavePoint());
      CHECK(history.redo() && a == 2);
    }
    {
      fiddle::UndoManager history;
      int a = 0, b = 0;
      history.perform(std::make_unique<ValueAction>(a, 1, "a"));
      history.perform(std::make_unique<ValueAction>(b, 1, "b"));
      history.undo();
      history.perform(std::make_unique<ValueAction>(a, 2, "a"));
      CHECK(!history.canRedo());
      CHECK(history.undo() && a == 1);
    }
    {
      fiddle::UndoManager history;
      int a = 0;
      for (int i = 1; i <= 100; ++i) history.perform(std::make_unique<ValueAction>(a, i));
      history.markSavePoint();
      history.perform(std::make_unique<ValueAction>(a, 101));
      CHECK(!history.isAtSavePoint());
      CHECK(history.undo() && a == 100 && history.isAtSavePoint());
    }
    {
      fiddle::UndoManager history;
      int a = 0;
      history.beginGesture();
      history.perform(std::make_unique<ValueAction>(a, 1, "a"));
      juce::Thread::sleep(550); // Still one drag, despite a pause.
      history.perform(std::make_unique<ValueAction>(a, 2, "a"));
      history.endGesture();
      history.beginGesture();
      history.perform(std::make_unique<ValueAction>(a, 3, "a"));
      history.endGesture();
      CHECK(history.undo() && a == 2);
      CHECK(history.undo() && a == 0 && !history.canUndo());
    }
    {
      fiddle::UndoManager history;
      int a = 0;
      history.perform(std::make_unique<ValueAction>(a, 1));
      history.undo();
      auto rejected = std::make_unique<ValueAction>(a, 5);
      rejected->fail = true;
      CHECK(!history.perform(std::move(rejected)));
      CHECK(history.canRedo() && history.isAtSavePoint());
      CHECK(!history.perform(std::make_unique<ValueAction>(a, 0)));
      CHECK(history.canRedo());
      CHECK(history.redo() && a == 1);
      history.noteExternalChange();
      CHECK(history.undo() && !history.isAtSavePoint());
      CHECK(history.canRedo()); // Undo of an older command remains reversible.
      history.markSavePoint();
      CHECK(history.isAtSavePoint());
    }
    {
      fiddle::UndoManager history;
      int a = 0;
      auto action = std::make_unique<ValueAction>(a, 1);
      auto *failureSwitch = action.get();
      history.perform(std::move(action));
      failureSwitch->fail = true;
      CHECK(!history.undo() && a == 1 && history.canUndo() && !history.canRedo());
      failureSwitch->fail = false;
      CHECK(history.undo() && a == 0);
      failureSwitch->fail = true;
      CHECK(!history.redo() && a == 0 && !history.canUndo() && history.canRedo());
      failureSwitch->fail = false;
      CHECK(history.redo() && a == 1);
    }
    std::cout << "PASS undo identities, barriers, transactions, failures and external edits\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
