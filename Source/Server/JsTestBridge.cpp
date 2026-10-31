#include "JsTestBridge.h"

#include <memory>

#include <juce_core/juce_core.h>

namespace fiddle {

namespace {

// Everything one evaluation touches from the message thread. Shared between
// the posted message, the WKWebView completion callback and the bridge thread
// so that whichever of them runs last still has live memory to work with.
struct PendingEvaluation {
  explicit PendingEvaluation(juce::String script) : js(std::move(script)) {}

  const juce::String js;
  juce::WaitableEvent done;
  // Written on the message thread before `done` is signalled; read on the
  // bridge thread only after `done.wait()` returned true.
  juce::String result;
  bool hasError = false;
};

} // namespace

JsTestBridge::JsTestBridge(juce::WebBrowserComponent &webComponent, int port)
    : juce::Thread("JsTestBridge"),
      evaluator_([target = juce::Component::SafePointer<
                      juce::WebBrowserComponent>(&webComponent)](
                     const juce::String &js, EvaluationCallback done) {
        if (target == nullptr) {
          done(EvaluationResult::Error{EvaluationResult::Error::Type::unknown,
                                       "WebBrowserComponent destroyed"});
          return;
        }
        target->evaluateJavascript(js, std::move(done));
      }),
      port_(port) {
  startThread();
}

JsTestBridge::~JsTestBridge() {
  listenerSocket_.close();
  stopThread(2000);
}

void JsTestBridge::run() {
  // Loopback only: this evaluates arbitrary JavaScript in the UI without
  // authentication, so it must never be reachable from the network.
  if (!listenerSocket_.createListener(port_, "127.0.0.1")) {
    DBG("JsTestBridge: Failed to create listener on port " << port_);
    return;
  }

  DBG("JsTestBridge: Listening on port " << port_);

  while (!threadShouldExit()) {
    auto *client = listenerSocket_.waitForNextConnection();
    if (client != nullptr) {
      handleConnection(std::unique_ptr<juce::StreamingSocket>(client));
    }
  }

  listenerSocket_.close();
}

juce::String JsTestBridge::evaluate(const Evaluator &evaluator,
                                    const juce::String &js, int timeoutMs) {
  auto pending = std::make_shared<PendingEvaluation>(js);

  juce::MessageManager::callAsync([evaluator, pending]() {
    evaluator(pending->js, [pending](EvaluationResult result) {
      if (auto *err = result.getError()) {
        pending->hasError = true;
        pending->result = err->message;
      } else if (auto *varRes = result.getResult()) {
        pending->result = juce::JSON::toString(*varRes);
      } else {
        pending->result = "null";
      }
      pending->done.signal();
    });
  });

  if (!pending->done.wait(timeoutMs)) {
    // The message thread may still be holding `pending`; it finishes on its
    // own and nobody reads what it writes.
    return kTimeoutMessage;
  }

  return (pending->hasError ? "ERROR:" : "OK:") + pending->result;
}

void JsTestBridge::handleConnection(
    std::unique_ptr<juce::StreamingSocket> clientSocket) {
  DBG("JsTestBridge: Client connected from " << clientSocket->getHostName());

  while (!threadShouldExit() && clientSocket->isConnected()) {
    // Read 4-byte length prefix (Little Endian bytes to uint32)
    uint8_t header[4];
    int bytesRead = clientSocket->read(header, 4, true);

    if (bytesRead != 4) {
      break;
    }

    uint32_t size =
        header[0] | (header[1] << 8) | (header[2] << 16) | (header[3] << 24);
    if (size == 0 || size > 1024 * 1024 * 10) { // 10MB sanity check
      break;
    }

    // Read payload
    std::vector<char> buffer(size + 1, 0);
    bytesRead = clientSocket->read(buffer.data(), (int)size, true);

    if (bytesRead != (int)size) {
      break;
    }

    const juce::String response =
        evaluate(evaluator_, juce::String(buffer.data()), 5000);
    auto outBytes = response.toRawUTF8();
    uint32_t outSize = (uint32_t)response.getNumBytesAsUTF8();

    uint8_t outHeader[4];
    outHeader[0] = outSize & 0xFF;
    outHeader[1] = (outSize >> 8) & 0xFF;
    outHeader[2] = (outSize >> 16) & 0xFF;
    outHeader[3] = (outSize >> 24) & 0xFF;

    clientSocket->write(outHeader, 4);
    clientSocket->write(outBytes, (int)outSize);
  }
}

} // namespace fiddle
