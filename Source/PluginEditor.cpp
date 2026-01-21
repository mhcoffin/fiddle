#include "PluginEditor.h"
#include "PluginProcessor.h"

FiddleAudioProcessorEditor::FiddleAudioProcessorEditor(FiddleAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setSize(400, 150);
  startTimer(250);
}

FiddleAudioProcessorEditor::~FiddleAudioProcessorEditor() { stopTimer(); }

void FiddleAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  auto bounds = getLocalBounds().reduced(20);

  // Title
  g.setColour(juce::Colours::white);
  g.setFont(juce::Font(24.0f, juce::Font::bold));
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
  g.drawText("Relaying MIDI to Fiddle Server (Port 3000)",
             bounds.removeFromTop(20), juce::Justification::left);
}

void FiddleAudioProcessorEditor::resized() {}

void FiddleAudioProcessorEditor::timerCallback() { repaint(); }
