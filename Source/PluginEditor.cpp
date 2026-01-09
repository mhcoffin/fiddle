#include "PluginEditor.h"
#include "PluginProcessor.h"

MidiLoggerAudioProcessorEditor::MidiLoggerAudioProcessorEditor(
    MidiLoggerAudioProcessor &p)
    : AudioProcessorEditor(&p), audioProcessor(p) {
  setSize(400, 300);
}

MidiLoggerAudioProcessorEditor::~MidiLoggerAudioProcessorEditor() {}

void MidiLoggerAudioProcessorEditor::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

  g.setColour(juce::Colours::white);
  g.setFont(15.0f);
  g.drawFittedText("MIDI Logger VSTi\nLogging to /tmp/juce_midi_log.txt",
                   getLocalBounds(), juce::Justification::centred, 2);
}

void MidiLoggerAudioProcessorEditor::resized() {}
