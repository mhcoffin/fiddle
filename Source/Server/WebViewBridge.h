#pragma once

#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_core/juce_core.h>
#include "MessageRouter.h"
#include <functional>
#include <optional>

namespace fiddle {

class WebViewBridge {
public:
    WebViewBridge(MessageRouter& router, std::function<void(std::function<void()>)> asyncRunner);
    
    void setup();
    juce::WebBrowserComponent::Options createWebOptions();
    
    juce::WebBrowserComponent& getMainWebComponent() { return mainWebComponent_; }
    
    bool isLoaded() const { return webViewLoaded_; }
    void setLoaded(bool loaded) { webViewLoaded_ = loaded; }
    
    void broadcastJavascript(const juce::String &js, 
                             juce::WebBrowserComponent* historyWebView = nullptr, 
                             juce::WebBrowserComponent* libraryWebView = nullptr);

    static juce::String escapeForJS(const juce::String &str);

    std::optional<juce::WebBrowserComponent::Resource> getResource(const juce::String &url);

private:
    MessageRouter& router_;
    std::function<void(std::function<void()>)> asyncRunner_;
    juce::File uiDir_;
    juce::WebBrowserComponent mainWebComponent_;
    bool webViewLoaded_ = false;
};

} // namespace fiddle
