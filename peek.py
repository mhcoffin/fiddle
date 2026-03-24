with open("Source/Server/MainComponent.cpp", "r") as f:
    text = f.read()
idx = text.find('void MainComponent::handleJsMessage')
print(text[idx:idx+2500])
