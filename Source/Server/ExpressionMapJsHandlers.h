#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace fiddle {

class ExpressionMapCommands;
class MessageRouter;

/// Registers and adapts JavaScript expression-map messages.
class ExpressionMapJsHandlers {
public:
  using Task = std::function<void()>;
  using FileSelection = std::function<void(juce::File)>;

  struct Callbacks {
    std::function<void(Task)> dispatch;
    std::function<void(FileSelection)> chooseFile;
    std::function<void(const juce::var &)> publishCatalog;
    std::function<void()> mixerChanged;
  };

  ExpressionMapJsHandlers(MessageRouter &router, ExpressionMapCommands &commands,
                          Callbacks callbacks);

  void registerHandlers();

private:
  void dispatch(Task task);

  MessageRouter &router_;
  ExpressionMapCommands &commands_;
  Callbacks callbacks_;
};

} // namespace fiddle
