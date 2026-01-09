#include "MainComponent.h"

namespace fiddle {

MainComponent::MainComponent() {
  addAndMakeVisible(openButton);
  addAndMakeVisible(playButton);
  addAndMakeVisible(pauseButton);
  addAndMakeVisible(rewindButton);
  addAndMakeVisible(statusLabel);
  addAndMakeVisible(connectionLabel);

  openButton.onClick = [this] { openFile(); };
  playButton.onClick = [this] { player.start(); };
  pauseButton.onClick = [this] { player.pause(); };
  rewindButton.onClick = [this] { player.rewind(); };

  statusLabel.setJustificationType(juce::Justification::centred);
  connectionLabel.setJustificationType(juce::Justification::centred);

  // Use internal instance of processor directly to simplify auto-load for now
  // This avoids having to find the .vst3 bundle path which varies by OS/Config
  host.useInternalInstance();

  setSize(600, 300);
  setAudioChannels(
      0, 2); // No audio inputs, 2 outputs (though we don't output sound)

  startTimer(200); // 5Hz UI update and connection check
}

MainComponent::~MainComponent() { shutdownAudio(); }

void MainComponent::prepareToPlay(int samplesPerBlockExpected,
                                  double sampleRate) {
  currentSampleRate = sampleRate;
}

void MainComponent::getNextAudioBlock(
    const juce::AudioSourceChannelInfo &bufferToFill) {
  juce::MidiBuffer midiMessages;
  player.getNextBlock(midiMessages, bufferToFill.numSamples, currentSampleRate);

  // Pass to plugin host (MidiLogger)
  host.processBlock(*bufferToFill.buffer, midiMessages);

  // Clear audio buffer since we are just a relay
  bufferToFill.clearActiveBufferRegion();
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() {
  auto area = getLocalBounds().reduced(20);
  auto topRow = area.removeFromTop(40);
  openButton.setBounds(topRow.removeFromLeft(150));

  area.removeFromTop(20);
  auto buttonRow = area.removeFromTop(40);
  int btnWidth = 100;
  int spacing = 10;
  playButton.setBounds(buttonRow.removeFromLeft(btnWidth));
  buttonRow.removeFromLeft(spacing);
  pauseButton.setBounds(buttonRow.removeFromLeft(btnWidth));
  buttonRow.removeFromLeft(spacing);
  rewindButton.setBounds(buttonRow.removeFromLeft(btnWidth));

  area.removeFromTop(20);
  statusLabel.setBounds(area.removeFromTop(30));
  connectionLabel.setBounds(area.removeFromTop(30));
}

void MainComponent::timerCallback() {
  bool connected = host.isConnected();

  // Connection Indicator
  if (connected) {
    connectionLabel.setText("CONNECTED TO SERVER", juce::dontSendNotification);
    connectionLabel.setColour(juce::Label::textColourId,
                              juce::Colours::lightgreen);
  } else {
    connectionLabel.setText("DISCONNECTED (Waiting for Server...)",
                            juce::dontSendNotification);
    connectionLabel.setColour(juce::Label::textColourId, juce::Colours::red);

    // Auto-pause if we were connected and lost it
    if (wasConnected && player.isPlaying()) {
      player.pause();
      statusLabel.setText("Paused: Lost connection to server",
                          juce::dontSendNotification);
    }
  }

  wasConnected = connected;

  if (player.getDuration() > 0) {
    juce::String status = juce::String::formatted(
        "Position: %.2f / %.2f s", player.getPosition(), player.getDuration());
    if (player.isPlaying())
      status += " (Playing)";
    else
      status += " (Stopped)";
    statusLabel.setText(status, juce::dontSendNotification);
  }

  updateButtons();
}

void MainComponent::openFile() {
  chooser = std::make_unique<juce::FileChooser>(
      "Select a MIDI file to play...",
      juce::File::getSpecialLocation(juce::File::userHomeDirectory),
      "*.mid;*.midi");

  auto flags = juce::FileBrowserComponent::openMode |
               juce::FileBrowserComponent::canSelectFiles;

  chooser->launchAsync(flags, [this](const juce::FileChooser &fc) {
    auto file = fc.getResult();
    if (file.existsAsFile()) {
      player.loadFile(file);
    }
  });
}

void MainComponent::updateButtons() {
  playButton.setEnabled(!player.isPlaying() && player.getDuration() > 0);
  pauseButton.setEnabled(player.isPlaying());
}

} // namespace fiddle
