#pragma once

#include "JsTestBridge.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class HistoryWindow : public juce::DocumentWindow {
public:
  HistoryWindow(juce::WebBrowserComponent::Options options)
      : DocumentWindow(
            "Fiddle - History",
            juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            DocumentWindow::closeButton | DocumentWindow::minimiseButton),
        webView_(std::move(options)) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentNonOwned(&webView_, false);
    setSize(800, 600);

    // Initialize test bridge on port 9224 specifically for the History window
    jsTestBridge_ = std::make_unique<JsTestBridge>(webView_, 9224);
  }

  void closeButtonPressed() override { setVisible(false); }

  void restoreGeometry(int x, int y, int w, int h, bool visible) {
    auto bounds = juce::Rectangle<int>(x, y, w, h);
    auto displays = juce::Desktop::getInstance().getDisplays();
    auto totalBounds = displays.getTotalBounds(true);
    if (!totalBounds.intersects(bounds)) {
      bounds = juce::Rectangle<int>(100, 100, 800, 600);
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
  juce::WebBrowserComponent webView_;
  std::unique_ptr<JsTestBridge> jsTestBridge_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HistoryWindow)
};

} // namespace fiddle
