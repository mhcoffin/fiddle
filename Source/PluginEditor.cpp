#include "PluginEditor.h"
#include "PluginProcessor.h"

FiddleAudioProcessorEditor::FiddleAudioProcessorEditor(FiddleAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setResizable(true, true);
  setResizeLimits(400, 120, 800, 400);
  setSize(400, 200);

  // Config path label (read-only)
  addAndMakeVisible(configPathLabel);
  configPathLabel.setFont(juce::FontOptions(14.0f));
  configPathLabel.setJustificationType(juce::Justification::centredLeft);
  configPathLabel.setColour(juce::Label::textColourId,
                            juce::Colours::lightgrey);
  configPathLabel.setColour(juce::Label::backgroundColourId,
                            juce::Colours::black.withAlpha(0.3f));

  updateConfigLabel();
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
  bounds.removeFromTop(10); // Gap

  // Config path label
  auto configArea = bounds.removeFromTop(24);
  configPathLabel.setBounds(configArea);
}

void FiddleAudioProcessorEditor::timerCallback() {
  updateConfigLabel();
  repaint();
}

void FiddleAudioProcessorEditor::updateConfigLabel() {
  juce::String currentPath = audioProcessor.getConfigPath();
  if (currentPath.isEmpty()) {
    configPathLabel.setText("Config: (using FiddleServer defaults)",
                            juce::dontSendNotification);
  } else {
    juce::File f(currentPath);
    configPathLabel.setText("Config: " + f.getFileNameWithoutExtension(),
                            juce::dontSendNotification);
  }
}
