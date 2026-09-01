#pragma once

#include <functional>

namespace fiddle {

class MessageRouter;
class MixerCommands;

/// Registers and adapts JavaScript mixer messages to typed mixer commands.
class MixerJsHandlers {
public:
  using Task = std::function<void()>;

  struct Callbacks {
    std::function<void(Task)> dispatch;
    std::function<void()> mixerChanged;
    std::function<void()> persistMixer;
  };

  MixerJsHandlers(MessageRouter &router, MixerCommands &commands,
                  Callbacks callbacks);

  void registerHandlers();

private:
  void dispatch(Task task);
  void notifyChanged(bool changed, bool persist);

  MessageRouter &router_;
  MixerCommands &commands_;
  Callbacks callbacks_;
};

} // namespace fiddle
