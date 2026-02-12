#include "PluginEditor.h"
#include "PluginProcessor.h"

FiddleAudioProcessorEditor::FiddleAudioProcessorEditor(FiddleAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  // Make the window resizable so user can expand it manually
  setResizable(true, true);
  setResizeLimits(400, 150, 800, 500); // min: 400x150, max: 800x500
  setSize(400, 400); // Increased height to ensure buttons are visible

  // Add test buttons
  addAndMakeVisible(testProgramChangeButton);
  testProgramChangeButton.setButtonText("Test Program Change");
  testProgramChangeButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colours::darkblue);
  testProgramChangeButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colours::white);
  testProgramChangeButton.onClick = [this]() {
    // IMMEDIATE VISUAL FEEDBACK
    testProgramChangeButton.setButtonText("Processing...");
    testProgramChangeButton.setColour(juce::TextButton::buttonColourId,
                                      juce::Colours::yellow);

    // Call logic
    int status = audioProcessor.sendTestProgramChange();

    // Update based on result
    if (status == 0) {
      testProgramChangeButton.setColour(juce::TextButton::buttonColourId,
                                        juce::Colours::green);
      testProgramChangeButton.setButtonText("Sent (OK)");
    } else if (status == 1) {
      testProgramChangeButton.setColour(juce::TextButton::buttonColourId,
                                        juce::Colours::red);
      testProgramChangeButton.setButtonText("Fail: Disconnected");
    } else {
      testProgramChangeButton.setColour(juce::TextButton::buttonColourId,
                                        juce::Colours::black);
      testProgramChangeButton.setButtonText("Fail: Null Relay");
    }
  };

  addAndMakeVisible(testContextUpdateButton);
  testContextUpdateButton.setButtonText("Test ContextUpdate");
  testContextUpdateButton.setColour(juce::TextButton::buttonColourId,
                                    juce::Colours::darkgreen);
  testContextUpdateButton.setColour(juce::TextButton::textColourOffId,
                                    juce::Colours::white);
  testContextUpdateButton.onClick = [this]() {
    audioProcessor.sendTestContextUpdate();
  };

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

  // Now place buttons in remaining space
  auto buttonArea = bounds.removeFromTop(35);
  testProgramChangeButton.setBounds(buttonArea.removeFromLeft(180));

  buttonArea.removeFromLeft(10); // Spacing

  testContextUpdateButton.setBounds(buttonArea.removeFromLeft(180));
}

void FiddleAudioProcessorEditor::timerCallback() { repaint(); }
