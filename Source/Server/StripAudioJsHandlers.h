#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace fiddle {

class MessageRouter;
class StripAudioCommands;

class StripAudioJsHandlers {
public:
  using Task = std::function<void()>;

  struct Callbacks {
    std::function<void(Task)> dispatch;
    std::function<void(const juce::var &)> publishState;
  };

  StripAudioJsHandlers(MessageRouter &router, StripAudioCommands &commands,
                       Callbacks callbacks);
  void registerHandlers();

private:
  void dispatch(Task task);

  MessageRouter &router_;
  StripAudioCommands &commands_;
  Callbacks callbacks_;
};

} // namespace fiddle
