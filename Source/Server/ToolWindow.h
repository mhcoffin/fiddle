#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <map>

namespace fiddle {

// Reusable non-modal tool host. One instance per tool; closing hides it.
// State is coalesced by topic, so slow/hidden WebViews cannot build a backlog.
class ToolWindow final : public juce::DocumentWindow, private juce::Timer {
public:
  using SaveBounds = std::function<void(juce::Rectangle<int>)>;
  ToolWindow(const juce::String &id, const juce::String &title,
             juce::WebBrowserComponent::Options options,
             juce::Rectangle<int> restored, int width, int height,
             SaveBounds saveBounds)
      : DocumentWindow("Fiddle " + title, juce::Colour(0xff182638),
                       DocumentWindow::closeButton | DocumentWindow::minimiseButton),
        webView_(options.withKeepPageLoadedWhenBrowserIsHidden()),
        saveBounds_(std::move(saveBounds)) {
    setUsingNativeTitleBar(true);
    setContentNonOwned(&webView_, false);
    setResizable(true, false);
    setResizeLimits(480, 420, 2400, 1800);
    centreWithSize(width, height);
    const auto *display = juce::Desktop::getInstance().getDisplays().getDisplayForRect(restored);
    if (!restored.isEmpty() && display != nullptr) {
      const auto displays = display->userBounds.toNearestInt();
      restored.setSize(juce::jlimit(480, juce::jmax(480, displays.getWidth()), restored.getWidth()),
                       juce::jlimit(420, juce::jmax(420, displays.getHeight()), restored.getHeight()));
      // Keep the title bar reachable after a monitor/layout change.
      restored.setPosition(juce::jlimit(displays.getX(), juce::jmax(displays.getX(), displays.getRight() - restored.getWidth()), restored.getX()),
                           juce::jlimit(displays.getY(), juce::jmax(displays.getY(), displays.getBottom() - restored.getHeight()), restored.getY()));
      setBounds(restored);
    }
    restoring_ = false;
    webView_.goToURL(juce::WebBrowserComponent::getResourceProviderRoot() + "index.html?mode=" + id);
  }
  ~ToolWindow() override { stopTimer(); saveGeometry(); }

  void show() {
    setVisible(true);
    setMinimised(false);
    toFront(true);
    sendVisibility(true);
    dirty_ = latest_; // Rehydrate the live page after hiding, without recreating it.
    flush();
  }
  void closeButtonPressed() override {
    sendVisibility(false);
    setVisible(false);
    stopTimer();
    saveGeometry();
  }
  void ready() { ready_ = true; sendVisibility(isVisible() && !isMinimised()); flush(); }
  void sendMessage(const juce::String &type, const juce::var &data) {
    latest_[type] = data;
    dirty_[type] = data;
    flush();
  }
  void moved() override { DocumentWindow::moved(); geometryChanged(); }
  void resized() override { DocumentWindow::resized(); geometryChanged(); }
  void minimisationStateChanged(bool minimised) override {
    DocumentWindow::minimisationStateChanged(minimised);
    sendVisibility(isVisible() && !minimised);
    if (!minimised) flush();
  }

private:
  void sendVisibility(bool visible) {
    if (ready_)
      webView_.evaluateJavascript(
          "window.__dispatchFromCpp && window.__dispatchFromCpp({type:'setToolWindowVisible',data:" +
          juce::String(visible ? "true" : "false") + "})");
  }
  void flush() {
    if (!ready_ || pending_ || !isVisible() || isMinimised() || dirty_.empty()) return;
    juce::Array<juce::var> batch;
    for (const auto &[type, data] : dirty_) {
      auto *message = new juce::DynamicObject();
      message->setProperty("type", type);
      message->setProperty("data", data);
      batch.add(juce::var(message));
    }
    dirty_.clear();
    pending_ = true;
    juce::Component::SafePointer<ToolWindow> safeThis(this);
    webView_.evaluateJavascript(
        "window.__dispatchFromCpp && " + juce::JSON::toString(batch, true) +
        ".forEach(window.__dispatchFromCpp)",
        [safeThis](juce::WebBrowserComponent::EvaluationResult) {
          if (safeThis != nullptr) { safeThis->pending_ = false; safeThis->flush(); }
        });
  }
  void geometryChanged() { if (!restoring_) startTimer(250); }
  void timerCallback() override { stopTimer(); saveGeometry(); }
  void saveGeometry() { if (!restoring_ && saveBounds_) saveBounds_(getBounds()); }
  juce::WebBrowserComponent webView_;
  SaveBounds saveBounds_;
  std::map<juce::String, juce::var> latest_, dirty_;
  bool ready_ = false, pending_ = false, restoring_ = true;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ToolWindow)
};

} // namespace fiddle
