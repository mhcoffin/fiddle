#pragma once

#include "MidiTcpRelay.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

using namespace juce;

class MidiLoggerAudioProcessor : public juce::AudioProcessor {
public:
  MidiLoggerAudioProcessor();
  ~MidiLoggerAudioProcessor() override;

  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  bool isConnected() const {
    return tcpRelay != nullptr && tcpRelay->isConnected();
  }

  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

private:
  std::unique_ptr<juce::FileLogger> logger;
  std::unique_ptr<fiddle::MidiTcpRelay> tcpRelay;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiLoggerAudioProcessor)
};
