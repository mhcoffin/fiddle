#pragma once

#include <functional>

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class JsTestBridge : public juce::Thread {
public:
  using EvaluationResult = juce::WebBrowserComponent::EvaluationResult;
  using EvaluationCallback = std::function<void(EvaluationResult)>;

  /// Runs a script on the message thread and delivers its result through the
  /// callback, either synchronously or at any later time. The production
  /// evaluator forwards to WebBrowserComponent::evaluateJavascript.
  using Evaluator =
      std::function<void(const juce::String &js, EvaluationCallback done)>;

  JsTestBridge(juce::WebBrowserComponent &webComponent, int port = 9223);
  ~JsTestBridge() override;

  void run() override;

  /// Posts `js` to the message thread through `evaluator` and waits up to
  /// `timeoutMs` for the result. Returns the wire response: "OK:<json>" or
  /// "ERROR:<message>". The posted work and the evaluation callback own their
  /// state, so an evaluation that runs or completes after the timeout is
  /// discarded instead of touching the caller's dead stack frame.
  static juce::String evaluate(const Evaluator &evaluator,
                               const juce::String &js, int timeoutMs);

  static constexpr const char *kTimeoutMessage =
      "ERROR:Timeout evaluating Javascript";

private:
  Evaluator evaluator_;
  int port_;
  juce::StreamingSocket listenerSocket_;

  void handleConnection(std::unique_ptr<juce::StreamingSocket> clientSocket);

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(JsTestBridge)
};

} // namespace fiddle
