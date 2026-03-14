#pragma once

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class JsTestBridge : public juce::Thread {
public:
  JsTestBridge(juce::WebBrowserComponent &webComponent, int port = 9223);
  ~JsTestBridge() override;

  void run() override;

private:
  juce::WebBrowserComponent &webComponent_;
  int port_;
  juce::StreamingSocket listenerSocket_;

  void handleConnection(std::unique_ptr<juce::StreamingSocket> clientSocket);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsTestBridge)
};

} // namespace fiddle
