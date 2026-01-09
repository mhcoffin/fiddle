#include "ScriptEngine.h"
#include <iostream>

namespace fiddle {

ScriptEngine::ScriptEngine() {
  engine = asCreateScriptEngine();
  if (engine) {
    engine->SetMessageCallback(asFUNCTION(messageCallbackImpl), this,
                               asCALL_CDECL);
  }
}

ScriptEngine::~ScriptEngine() {
  if (engine) {
    engine->ShutDownAndRelease();
  }
}

void ScriptEngine::messageCallbackImpl(const asSMessageInfo *msg, void *param) {
  reinterpret_cast<ScriptEngine *>(param)->handleMessage(msg);
}

void ScriptEngine::handleMessage(const asSMessageInfo *msg) {
  if (messageCallback) {
    std::string type = "Info";
    bool isError = false;
    if (msg->type == asMSGTYPE_ERROR) {
      type = "Error";
      isError = true;
    } else if (msg->type == asMSGTYPE_WARNING) {
      type = "Warning";
    }

    std::string text =
        juce::String::formatted("%s (%d, %d): %s: %s", msg->section, msg->row,
                                msg->col, type.c_str(), msg->message)
            .toStdString();
    messageCallback(text, isError);
  }
}

bool ScriptEngine::loadScript(const juce::File &file) {
  if (!engine)
    return false;

  juce::String content = file.loadFileAsString();
  if (content.isEmpty())
    return false;

  std::lock_guard<std::mutex> lock(engineMutex);
  module = engine->GetModule("FiddleModule", asGM_ALWAYS_CREATE);
  if (!module)
    return false;

  int r = module->AddScriptSection("main", content.toRawUTF8());
  if (r < 0)
    return false;

  r = module->Build();
  if (r < 0)
    return false;

  return true;
}

void ScriptEngine::execute(const std::string &functionName) {
  std::lock_guard<std::mutex> lock(engineMutex);
  if (!module)
    return;

  asIScriptFunction *func = module->GetFunctionByDecl(functionName.c_str());
  if (!func)
    return;

  asIScriptContext *ctx = engine->CreateContext();
  if (ctx) {
    ctx->Prepare(func);
    int r = ctx->Execute();
    if (r == asEXECUTION_EXCEPTION) {
      if (messageCallback) {
        messageCallback(std::string("Execution Exception: ") +
                            ctx->GetExceptionString(),
                        true);
      }
    }
    ctx->Release();
  }
}

void ScriptEngine::execute(const std::string &functionName, void *arg) {
  std::lock_guard<std::mutex> lock(engineMutex);
  if (!module)
    return;

  asIScriptFunction *func = module->GetFunctionByDecl(functionName.c_str());
  if (!func) {
    std::cerr << "[ScriptEngine] Error: Could not find function: "
              << functionName << std::endl;
    return;
  }

  asIScriptContext *ctx = engine->CreateContext();
  if (ctx) {
    ctx->Prepare(func);
    ctx->SetArgAddress(0, arg);
    int r = ctx->Execute();
    if (r == asEXECUTION_EXCEPTION) {
      std::cerr << "[ScriptEngine] Exception: " << ctx->GetExceptionString()
                << std::endl;
      if (messageCallback) {
        messageCallback(std::string("Execution Exception: ") +
                            ctx->GetExceptionString(),
                        true);
      }
    }
    ctx->Release();
  }
}

} // namespace fiddle
