#pragma once

#include <functional>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

/// Callbacks that the debug window delegates to MainComponent.
struct DebugWindowCallbacks {
  std::function<void()> onSignalReady;
  std::function<void(const juce::String &)> onNativeLog;
  std::function<void()> onScanPlugins;
  std::function<void()> onRescanPlugins;
  std::function<void()> onRequestPluginsState;
};

/// Second window for debug/diagnostic views (Timeline, Event Log, Plugins).
/// Contains its own WebBrowserComponent loading the UI in debug mode.
///
/// Native functions are registered directly in the constructor (not via
/// moved Options) to avoid JUCE Options move-semantics losing registrations.
class DebugWindow : public juce::DocumentWindow {
public:
  DebugWindow(std::function<std::optional<juce::WebBrowserComponent::Resource>(
                  const juce::String &)>
                  resourceProvider,
              DebugWindowCallbacks callbacks)
      : DocumentWindow(
            "Fiddle - Debug",
            juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            DocumentWindow::closeButton | DocumentWindow::minimiseButton),
        callbacks_(std::move(callbacks)),
        webView_(
            juce::WebBrowserComponent::Options{}
                .withNativeIntegrationEnabled(true)
                .withResourceProvider(std::move(resourceProvider))
                .withNativeFunction(
                    "signalReady",
                    [this](const juce::Array<juce::var> &,
                           juce::WebBrowserComponent::NativeFunctionCompletion
                               completion) {
                      debugReady_ = true;
                      std::cerr << "[DebugWindow] signalReady fired"
                                << std::endl;
                      if (callbacks_.onSignalReady)
                        callbacks_.onSignalReady();
                      completion(true);
                    })
                .withNativeFunction(
                    "nativeLog",
                    [this](const juce::Array<juce::var> &args,
                           juce::WebBrowserComponent::NativeFunctionCompletion
                               completion) {
                      if (args.size() > 0) {
                        auto msg = args[0].toString();
                        std::cerr << "[Debug JS] " << msg << std::endl;
                        if (callbacks_.onNativeLog)
                          callbacks_.onNativeLog(msg);
                      }
                      completion(true);
                    })
                .withNativeFunction(
                    "scanPlugins",
                    [this](const juce::Array<juce::var> &,
                           juce::WebBrowserComponent::NativeFunctionCompletion
                               completion) {
                      std::cerr << "[DebugWindow] scanPlugins called"
                                << std::endl;
                      if (callbacks_.onScanPlugins)
                        callbacks_.onScanPlugins();
                      completion(true);
                    })
                .withNativeFunction(
                    "rescanPlugins",
                    [this](const juce::Array<juce::var> &,
                           juce::WebBrowserComponent::NativeFunctionCompletion
                               completion) {
                      std::cerr << "[DebugWindow] rescanPlugins called"
                                << std::endl;
                      if (callbacks_.onRescanPlugins)
                        callbacks_.onRescanPlugins();
                      completion(true);
                    })
                .withNativeFunction(
                    "requestPluginsState",
                    [this](const juce::Array<juce::var> &,
                           juce::WebBrowserComponent::NativeFunctionCompletion
                               completion) {
                      std::cerr << "[DebugWindow] requestPluginsState called"
                                << std::endl;
                      if (callbacks_.onRequestPluginsState)
                        callbacks_.onRequestPluginsState();
                      completion(true);
                    })) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentNonOwned(&webView_, false);
    setSize(700, 500);
    // Start hidden; caller will call restoreGeometry() which sets visibility
    // from the persisted state.
    setVisible(false);
  }

  /// Navigate the debug WebView to the debug-mode URL.
  void loadDebugPage(const juce::String &rootUrl) {
    webView_.goToURL(rootUrl + "?mode=debug");
  }

  /// Evaluate JavaScript in the debug WebView.
  void evaluateJavascript(const juce::String &js) {
    if (debugReady_)
      webView_.evaluateJavascript(js);
  }

  bool isDebugReady() const { return debugReady_; }

  void closeButtonPressed() override {
    setVisible(false);
    if (geometrySaver_)
      geometrySaver_();
  }

  void moved() override {
    DocumentWindow::moved();
    if (geometrySaver_)
      geometrySaver_();
  }

  void resized() override {
    DocumentWindow::resized();
    if (geometrySaver_)
      geometrySaver_();
  }

  void setGeometrySaver(std::function<void()> cb) {
    geometrySaver_ = std::move(cb);
  }

  void restoreGeometry(int x, int y, int w, int h, bool visible) {
    auto bounds = juce::Rectangle<int>(x, y, w, h);
    auto displays = juce::Desktop::getInstance().getDisplays();
    auto totalBounds = displays.getTotalBounds(true);
    if (!totalBounds.intersects(bounds)) {
      bounds = juce::Rectangle<int>(100, 100, 700, 500);
    }
    if (bounds.getWidth() < 300)
      bounds.setWidth(300);
    if (bounds.getHeight() < 200)
      bounds.setHeight(200);
    setBounds(bounds);
    setVisible(visible);
  }

  juce::WebBrowserComponent &getWebView() { return webView_; }

private:
  DebugWindowCallbacks callbacks_;
  juce::WebBrowserComponent webView_;
  bool debugReady_ = false;
  std::function<void()> geometrySaver_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DebugWindow)
};

} // namespace fiddle
