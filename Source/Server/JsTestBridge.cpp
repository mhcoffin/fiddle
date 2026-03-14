#include "JsTestBridge.h"
#include <juce_core/juce_core.h>

namespace fiddle {

JsTestBridge::JsTestBridge(juce::WebBrowserComponent &webComponent, int port)
    : juce::Thread("JsTestBridge"), webComponent_(webComponent), port_(port) {
  startThread();
}

JsTestBridge::~JsTestBridge() {
  listenerSocket_.close();
  stopThread(2000);
}

void JsTestBridge::run() {
  if (!listenerSocket_.createListener(port_)) {
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

    juce::String js(buffer.data());

    juce::WaitableEvent event;
    juce::String resultString;
    bool hasError = false;

    juce::MessageManager::callAsync([&]() {
      webComponent_.evaluateJavascript(
          js, [&](juce::WebBrowserComponent::EvaluationResult result) {
            if (auto *err = result.getError()) {
              hasError = true;
              resultString = err->message;
            } else if (auto *varRes = result.getResult()) {
              resultString = juce::JSON::toString(*varRes);
            } else {
              resultString = "null";
            }
            event.signal();
          });
    });

    // Wait for JS thread to process it
    if (!event.wait(5000)) { // 5 second timeout
      hasError = true;
      resultString = "Timeout evaluating Javascript";
    }

    juce::String response = (hasError ? "ERROR:" : "OK:") + resultString;
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
