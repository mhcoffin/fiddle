#include "PluginEditor.h"
#include "PluginProcessor.h"

FiddleAudioProcessorEditor::FiddleAudioProcessorEditor(FiddleAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  // Make the window resizable so user can expand it manually
  setResizable(true, true);
  setResizeLimits(400, 150, 800, 500); // min: 400x150, max: 800x500
  setSize(400, 400); // Increased height to ensure buttons are visible

  // Setup load button
  addAndMakeVisible(loadConfigButton);
  loadConfigButton.setButtonText("Browse...");
  loadConfigButton.setColour(juce::TextButton::buttonColourId,
                             juce::Colours::darkgrey);
  loadConfigButton.onClick = [this]() {
    auto startDir =
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    // Create new chooser
    fileChooser = std::make_unique<juce::FileChooser>("Select Fiddle Config...",
                                                      startDir, "*.yaml", true);

    auto folderChooserFlags = juce::FileBrowserComponent::openMode |
                              juce::FileBrowserComponent::canSelectFiles;

    fileChooser->launchAsync(
        folderChooserFlags, [this](const juce::FileChooser &chooser) {
          auto result = chooser.getResult();
          if (result.existsAsFile()) {
            auto path = result.getFullPathName();
            audioProcessor.setConfigPath(path);
            configPathLabel.setText(path, juce::dontSendNotification);
          }
        });
  };

  // Setup Label
  addAndMakeVisible(configPathLabel);
  configPathLabel.setFont(juce::FontOptions(14.0f));
  configPathLabel.setJustificationType(juce::Justification::centredLeft);
  configPathLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  configPathLabel.setColour(juce::Label::backgroundColourId,
                            juce::Colours::black.withAlpha(0.3f));

  juce::String currentPath = audioProcessor.getConfigPath();
  if (currentPath.isEmpty()) {
    configPathLabel.setText("No config loaded (Using FiddleServer defaults)",
                            juce::dontSendNotification);
  } else {
    configPathLabel.setText(currentPath, juce::dontSendNotification);
  }

  startTimer(250);
}

FiddleAudioProcessorEditor::~FiddleAudioProcessorEditor() { stopTimer(); }

void FiddleAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  auto bounds = getLocalBounds().reduced(20);

  // Title
  g.setColour(juce::Colours::white);
  g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
  g.drawText("Fiddle", bounds.removeFromTop(40), juce::Justification::left);

  // Connection Status
  auto statusArea = bounds.removeFromTop(30);
  g.setFont(18.0f);
  g.setColour(juce::Colours::lightgrey);
  g.drawText("Status: ", statusArea.removeFromLeft(60),
             juce::Justification::centredLeft);

  bool connected = audioProcessor.isConnected();
  g.setColour(connected ? juce::Colours::green : juce::Colours::red);
  g.fillEllipse(
      statusArea.removeFromLeft(15).withSizeKeepingCentre(12, 12).toFloat());

  g.setColour(juce::Colours::white);
  g.drawText(connected ? "Connected" : "Disconnected", statusArea,
             juce::Justification::centredLeft);

  // Info
  g.setColour(juce::Colours::grey);
  g.setFont(12.0f);
  g.drawText("Relaying MIDI to Fiddle Server (Port 5252)",
             bounds.removeFromTop(20), juce::Justification::left);
}

void FiddleAudioProcessorEditor::resized() {
  auto bounds = getLocalBounds().reduced(20);

  // Skip the painted content: Title (40) + Status (30) + Info (20) = 90px
  bounds.removeFromTop(40); // Title
  bounds.removeFromTop(30); // Status
  bounds.removeFromTop(20); // Info
  bounds.removeFromTop(10); // Small gap

  // Now place config selector in remaining space
  auto configArea = bounds.removeFromTop(30);

  // Left side: Browse Button (~100px)
  loadConfigButton.setBounds(configArea.removeFromLeft(100));
  configArea.removeFromLeft(10); // Spacing

  // Right side: Active Path Label (takes remaining width)
  configPathLabel.setBounds(configArea);
}

void FiddleAudioProcessorEditor::timerCallback() { repaint(); }
