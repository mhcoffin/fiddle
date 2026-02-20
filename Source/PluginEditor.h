#pragma once

#include "PluginProcessor.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

class FiddleAudioProcessorEditor : public juce::AudioProcessorEditor,
                                   private juce::Timer {
public:
  FiddleAudioProcessorEditor(FiddleAudioProcessor &);
  ~FiddleAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

  void timerCallback() override;

private:
  FiddleAudioProcessor &audioProcessor;

  std::unique_ptr<juce::FileChooser> fileChooser;
  juce::TextButton loadConfigButton;
  juce::Label configPathLabel;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleAudioProcessorEditor)
};
