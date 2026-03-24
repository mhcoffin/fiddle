with open("Source/Server/MainComponent.cpp", "r") as f:
    text = f.read()

# First replace everything globally
text = text.replace("webComponent.evaluateJavascript(", "broadcastJavascript(")

# Then restore the occurrences inside createWebOptions
start = text.find("juce::WebBrowserComponent::Options MainComponent::createWebOptions()")
end = text.find("MainComponent::MainComponent(const juce::String &configName)")

if start != -1 and end != -1:
    header = text[:start]
    options_block = text[start:end]
    tail = text[end:]
    
    # In Options block we shouldn't use broadcastJavascript because it might not be initialized
    # Wait, inside signalReady in Options block, we need targetWebComponent->evaluateJavascript(
    options_block = options_block.replace("broadcastJavascript(", "targetWebComponent->evaluateJavascript(")
    
    text = header + options_block + tail

with open("Source/Server/MainComponent.cpp", "w") as f:
    f.write(text)
