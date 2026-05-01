import re

with open('Source/Server/MainComponent.cpp', 'r') as f:
    code = f.read()

# jsTestBridge_ = std::make_unique<JsTestBridge>(webComponent, 9223);
code = code.replace('std::make_unique<JsTestBridge>(webComponent, 9223);', 'std::make_unique<JsTestBridge>(webViewBridge_.getMainWebComponent(), 9223);')

# webComponent.setBounds(getLocalBounds());
code = code.replace('webComponent.setBounds(getLocalBounds());', 'webViewBridge_.getMainWebComponent().setBounds(getLocalBounds());')

# juce::WebBrowserComponent *targetWebComponent = &webComponent;
code = code.replace('juce::WebBrowserComponent *targetWebComponent = &webComponent;', 'juce::WebBrowserComponent *targetWebComponent = &webViewBridge_.getMainWebComponent();')

# webViewLoaded = true;
code = code.replace('webViewLoaded = true;', 'webViewBridge_.setLoaded(true);')

# if (webViewLoaded) -> if (webViewBridge_.isLoaded())
code = code.replace('if (!webViewLoaded)', 'if (!webViewBridge_.isLoaded())')
code = code.replace('if (webViewLoaded)', 'if (webViewBridge_.isLoaded())')

# createWebOptions() -> webViewBridge_.createWebOptions()
code = code.replace('createWebOptions()', 'webViewBridge_.createWebOptions()')

# escapeForJS -> WebViewBridge::escapeForJS
code = code.replace('escapeForJS', 'WebViewBridge::escapeForJS')

with open('Source/Server/MainComponent.cpp', 'w') as f:
    f.write(code)

