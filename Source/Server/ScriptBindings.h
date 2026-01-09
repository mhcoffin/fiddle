#pragma once

#include <angelscript.h>
#include <functional>
#include <string>

namespace fiddle {

class ScriptBindings {
public:
  static void RegisterFiddleAPI(asIScriptEngine *engine);

  // Global print bridge for scripts
  static void Print(const std::string &msg);
};

void SetPrintCallback(std::function<void(const std::string &)> cb);

} // namespace fiddle
