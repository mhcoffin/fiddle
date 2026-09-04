#pragma once

#include <functional>
#include <juce_core/juce_core.h>

namespace fiddle {

class MasterAudioCommands;
class MessageRouter;

class MasterAudioJsHandlers {
public:
  using Task = std::function<void()>;

  struct Callbacks {
    std::function<void(Task)> dispatch;
    std::function<void(const juce::var &)> publishState;
  };

  MasterAudioJsHandlers(MessageRouter &router, MasterAudioCommands &commands,
                        Callbacks callbacks);
  void registerHandlers();

private:
  void dispatch(Task task);

  MessageRouter &router_;
  MasterAudioCommands &commands_;
  Callbacks callbacks_;
};

} // namespace fiddle
