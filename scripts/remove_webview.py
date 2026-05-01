import re

with open('Source/Server/MainComponent.cpp', 'r') as f:
    code = f.read()

def remove_func(name, signature):
    global code
    idx = code.find(signature)
    if idx == -1:
        return
    start = idx
    brace_count = 0
    in_func = False
    end = -1
    for i in range(start, len(code)):
        if code[i] == '{':
            brace_count += 1
            in_func = True
        elif code[i] == '}':
            brace_count -= 1
            if in_func and brace_count == 0:
                end = i + 1
                break
    code = code[:start] + code[end:]

remove_func('createWebOptions', 'juce::WebBrowserComponent::Options MainComponent::createWebOptions()')
remove_func('setupWebView', 'void MainComponent::setupWebView()')
remove_func('getResource', 'MainComponent::getResource(const juce::String &url)')
remove_func('getResource', 'std::optional<juce::WebBrowserComponent::Resource>\nMainComponent::getResource(const juce::String &url)')
remove_func('escapeForJS', 'juce::String MainComponent::escapeForJS(const juce::String &str)')
remove_func('broadcastJavascript', 'void MainComponent::broadcastJavascript(const juce::String &js)')

with open('Source/Server/MainComponent.cpp', 'w') as f:
    f.write(code)

