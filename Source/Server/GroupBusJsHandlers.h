#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace fiddle {
class MessageRouter;
class GroupBusCommands;

class GroupBusJsHandlers {
public:
  using Task = std::function<void()>;
  struct Callbacks {
    std::function<void(Task)> dispatch;
    std::function<void()> changed;
    std::function<void()> requested;
    std::function<void(const juce::String &, bool)> balanceChanged;
  };
  GroupBusJsHandlers(MessageRouter &router, GroupBusCommands &commands,
                     Callbacks callbacks);
  void registerHandlers();
private:
  void dispatch(Task task);
  void notify(bool changed);
  MessageRouter &router_;
  GroupBusCommands &commands_;
  Callbacks callbacks_;
};
} // namespace fiddle
