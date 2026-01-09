#pragma once

#include <angelscript.h>
#include <functional>
#include <juce_core/juce_core.h>
#include <string>

namespace fiddle {

class ScriptEngine {
public:
  ScriptEngine();
  ~ScriptEngine();

  // Load and compile a script from a file
  bool loadScript(const juce::File &file);

  // Execute a function in the script
  void execute(const std::string &functionName);

  // Execute a function with one object pointer argument
  void execute(const std::string &functionName, void *arg);

  // Set a callback for compiler/runtime messages
  void setMessageCallback(
      std::function<void(const std::string &, bool isError)> callback) {
    messageCallback = callback;
  }

  // Get the underlying engine for registration
  asIScriptEngine *getEngine() const { return engine; }

private:
  asIScriptEngine *engine = nullptr;
  asIScriptModule *module = nullptr;
  std::function<void(const std::string &, bool)> messageCallback;
  mutable std::mutex engineMutex;

  static void messageCallbackImpl(const asSMessageInfo *msg, void *param);
  void handleMessage(const asSMessageInfo *msg);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScriptEngine)
};

} // namespace fiddle
