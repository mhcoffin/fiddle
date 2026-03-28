#pragma once

#include "JsTestBridge.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class LibraryManagerWindow : public juce::DocumentWindow {
public:
  LibraryManagerWindow(juce::WebBrowserComponent::Options options)
      : DocumentWindow(
            "Fiddle - Library Manager",
            juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                juce::ResizableWindow::backgroundColourId),
            DocumentWindow::closeButton | DocumentWindow::minimiseButton),
        webView_(std::move(options)) {
    setUsingNativeTitleBar(true);
    setResizable(true, true);
    setContentNonOwned(&webView_, false);
    setSize(1100, 700);

    // Initialize test bridge on port 9226 for the Library Manager window
    jsTestBridge_ = std::make_unique<JsTestBridge>(webView_, 9226);
  }

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
      bounds = juce::Rectangle<int>(100, 100, 1100, 700);
    }
    if (bounds.getWidth() < 400)
      bounds.setWidth(400);
    if (bounds.getHeight() < 300)
      bounds.setHeight(300);
    setBounds(bounds);
    setVisible(visible);
  }

  juce::WebBrowserComponent &getWebView() { return webView_; }

private:
  juce::WebBrowserComponent webView_;
  std::unique_ptr<JsTestBridge> jsTestBridge_;
  std::function<void()> geometrySaver_;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryManagerWindow)
};

} // namespace fiddle
