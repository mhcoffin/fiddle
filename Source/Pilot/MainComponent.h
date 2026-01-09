#pragma once

#include "MidiPlayer.h"
#include "PluginHost.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class MainComponent : public juce::AudioAppComponent, public juce::Timer {
public:
  MainComponent();
  ~MainComponent() override;

  void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
  void
  getNextAudioBlock(const juce::AudioSourceChannelInfo &bufferToFill) override;
  void releaseResources() override;

  void paint(juce::Graphics &g) override;
  void resized() override;
  void timerCallback() override;

private:
  void openFile();
  void updateButtons();

  // Logic
  MidiPlayer player;
  PluginHost host;
  bool wasConnected = false;

  // UI
  juce::TextButton openButton{"Open MIDI File..."};
  juce::TextButton playButton{"Play"};
  juce::TextButton pauseButton{"Pause"};
  juce::TextButton rewindButton{"Rewind"};

  juce::Label statusLabel;
  juce::Label connectionLabel;

  std::unique_ptr<juce::FileChooser> chooser;
  double currentSampleRate = 44100.0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace fiddle
