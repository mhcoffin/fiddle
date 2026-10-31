// JsTestBridge::evaluate must survive the message thread running its posted
// work, or delivering the WebView result, after the bridge thread has already
// timed out and moved on. The original code captured the bridge thread's
// stack by reference and crashed in WKWebView's evaluateJavascript when
// FiddleServer was still busy instantiating plug-ins at launch.
#include "../JsTestBridge.h"

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      ++passed;                                                                \
    } else {                                                                   \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                  \
                << "]: " #condition << std::endl;                             \
    }                                                                          \
  } while (false)

using fiddle::JsTestBridge;
using Error = JsTestBridge::EvaluationResult::Error;

void pump() { juce::MessageManager::getInstance()->runDispatchLoopUntil(20); }

// Runs evaluate() on a bridge-like background thread while this (message)
// thread pumps, mirroring production where the socket thread blocks and the
// message thread does the work.
juce::String evaluateWhilePumping(const JsTestBridge::Evaluator &evaluator,
                                  const juce::String &js, int timeoutMs) {
  juce::String response;
  std::atomic<bool> finished{false};
  std::thread bridge([&] {
    response = JsTestBridge::evaluate(evaluator, js, timeoutMs);
    finished = true;
  });
  while (!finished) {
    pump();
  }
  bridge.join();
  return response;
}

void testSuccessfulEvaluationReturnsJson() {
  const auto response = evaluateWhilePumping(
      [](const juce::String &js, JsTestBridge::EvaluationCallback done) {
        CHECK(juce::MessageManager::getInstance()->isThisTheMessageThread());
        CHECK(js == "1 + 1");
        done(juce::var(2));
      },
      "1 + 1", 2000);
  CHECK(response == "OK:2");
}

void testJavascriptErrorIsReported() {
  const auto response = evaluateWhilePumping(
      [](const juce::String &, JsTestBridge::EvaluationCallback done) {
        done(Error{Error::Type::javascriptException, "boom"});
      },
      "throw new Error('boom')", 2000);
  CHECK(response == "ERROR:boom");
}

void testObjectResultIsJsonStringified() {
  const auto response = evaluateWhilePumping(
      [](const juce::String &, JsTestBridge::EvaluationCallback done) {
        auto *obj = new juce::DynamicObject();
        obj->setProperty("a", 1);
        done(juce::var(obj));
      },
      "({a: 1})", 2000);
  CHECK(response.startsWith("OK:"));
  const auto parsed = juce::JSON::parse(response.fromFirstOccurrenceOf("OK:", false, false));
  CHECK(parsed.isObject());
  CHECK(static_cast<int>(parsed["a"]) == 1);
}

// The crash scenario: the message thread is too busy to run the posted
// message before the bridge gives up. Here the message loop is simply not
// pumped until evaluate() has returned, so the evaluator runs against a stack
// frame that no longer exists. It must still see the script intact.
void testLateMessageAfterTimeoutIsHarmless() {
  const juce::String script = "document.title // posted late";
  std::atomic<int> evaluatorCalls{0};
  std::atomic<bool> sawIntactScript{false};
  auto lateCallback = std::make_shared<JsTestBridge::EvaluationCallback>();

  juce::String response;
  {
    std::thread bridge([&] {
      response = JsTestBridge::evaluate(
          [&, lateCallback](const juce::String &js,
                            JsTestBridge::EvaluationCallback done) {
            ++evaluatorCalls;
            sawIntactScript = (js == script);
            *lateCallback = std::move(done); // like WKWebView, answer later
          },
          script, 50);
    });
    bridge.join(); // no pumping: the posted message is still queued
  }
  CHECK(response == JsTestBridge::kTimeoutMessage);
  CHECK(evaluatorCalls == 0);

  // Now the message thread catches up, long after the bridge moved on.
  for (int i = 0; i < 10 && evaluatorCalls == 0; ++i) {
    pump();
  }
  CHECK(evaluatorCalls == 1);
  CHECK(sawIntactScript);

  // And WKWebView finally delivers a result nobody is waiting for.
  CHECK(static_cast<bool>(*lateCallback));
  if (*lateCallback) {
    (*lateCallback)(juce::var("too late"));
  }
  pump();
}

// Variant: the evaluator runs in time but the WebView answers after the
// timeout, so only the completion callback outlives the bridge's wait.
void testLateResultAfterTimeoutIsHarmless() {
  auto lateCallback = std::make_shared<JsTestBridge::EvaluationCallback>();
  const auto response = evaluateWhilePumping(
      [lateCallback](const juce::String &,
                     JsTestBridge::EvaluationCallback done) {
        *lateCallback = std::move(done);
      },
      "await new Promise(() => {})", 50);
  CHECK(response == JsTestBridge::kTimeoutMessage);
  CHECK(static_cast<bool>(*lateCallback));
  if (*lateCallback) {
    (*lateCallback)(juce::var("too late"));
  }
  pump();
}

void testWaitDoesNotReturnEarlyWithoutResult() {
  const auto start = juce::Time::getMillisecondCounter();
  const auto response = evaluateWhilePumping(
      [](const juce::String &, JsTestBridge::EvaluationCallback) {},
      "void 0", 100);
  CHECK(response == JsTestBridge::kTimeoutMessage);
  CHECK(juce::Time::getMillisecondCounter() - start >= 90);
}

} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI gui;

  testSuccessfulEvaluationReturnsJson();
  testJavascriptErrorIsReported();
  testObjectResultIsJsonStringified();
  testLateMessageAfterTimeoutIsHarmless();
  testLateResultAfterTimeoutIsHarmless();
  testWaitDoesNotReturnEarlyWithoutResult();

  std::cout << "JsTestBridgeTest: " << passed << " passed, " << failed
            << " failed" << std::endl;
  return failed == 0 ? 0 : 1;
}
