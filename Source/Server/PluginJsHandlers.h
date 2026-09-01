#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace fiddle {

class MessageRouter;
class PluginCommands;

/// Registers and adapts JavaScript messages for hosted-plugin operations.
class PluginJsHandlers {
public:
  using Task = std::function<void()>;

  struct Callbacks {
    std::function<void(Task)> dispatch;
    std::function<void(const juce::String &)> logMessage;
    std::function<void(bool)> publishScanning;
    std::function<void(const juce::var &)> publishPluginList;
    std::function<void()> mixerChanged;
  };

  PluginJsHandlers(MessageRouter &router, PluginCommands &commands,
                   Callbacks callbacks);

  void registerHandlers();

private:
  void dispatch(Task task);
  void beginScan(bool fullRescan);

  MessageRouter &router_;
  PluginCommands &commands_;
  Callbacks callbacks_;
};

} // namespace fiddle
