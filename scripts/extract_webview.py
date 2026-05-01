import re

with open('Source/Server/MainComponent.cpp', 'r') as f:
    code = f.read()

def get_func_body(name, signature):
    idx = code.find(signature)
    if idx == -1:
        return ""
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
    return code[start:end]

createWebOptions = get_func_body('createWebOptions', 'juce::WebBrowserComponent::Options MainComponent::createWebOptions()')
setupWebView = get_func_body('setupWebView', 'void MainComponent::setupWebView()')
getResource = get_func_body('getResource', 'MainComponent::getResource(const juce::String &url)')
if 'std::optional' not in getResource:
    getResource = get_func_body('getResource', 'std::optional<juce::WebBrowserComponent::Resource>\nMainComponent::getResource(const juce::String &url)')
escapeForJS = get_func_body('escapeForJS', 'juce::String MainComponent::escapeForJS(const juce::String &str)')
broadcastJavascript = get_func_body('broadcastJavascript', 'void MainComponent::broadcastJavascript(const juce::String &js)')

with open('scripts/out_webview.cpp', 'w') as f:
    f.write(createWebOptions + '\n\n' + setupWebView + '\n\n' + getResource + '\n\n' + escapeForJS + '\n\n' + broadcastJavascript)

