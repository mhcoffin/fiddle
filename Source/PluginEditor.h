#pragma once

#include "PluginProcessor.h"
#include <juce_graphics/juce_graphics.h>
#include <juce_gui_basics/juce_gui_basics.h>

using namespace juce;

class MidiLoggerAudioProcessorEditor : public juce::AudioProcessorEditor {
public:
  MidiLoggerAudioProcessorEditor(MidiLoggerAudioProcessor &);
  ~MidiLoggerAudioProcessorEditor() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  MidiLoggerAudioProcessor &audioProcessor;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLoggerAudioProcessorEditor)
};
