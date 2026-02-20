#pragma once

#include "AudioSharedMemory.h"
#include "MidiTcpRelay.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace fiddle {
class FiddleVST3Extensions;
}

using namespace juce;

class FiddleAudioProcessor : public juce::AudioProcessor,
                             private juce::Timer,
                             public juce::AudioProcessorParameter::Listener {
public:
  static constexpr int kParamIdProgram = 1000;
  static constexpr int kParamIdBankMSB = 1001;
  static constexpr int kParamIdBankLSB = 1002;

  //==============================================================================
  FiddleAudioProcessor();
  ~FiddleAudioProcessor() override;

  //==============================================================================
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override;

  bool isBusesLayoutSupported(const BusesLayout &layouts) const override;

  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;

  //==============================================================================
  juce::AudioProcessorEditor *createEditor() override;
  bool hasEditor() const override;

  bool isConnected() const {
    return tcpRelay != nullptr && tcpRelay->isConnected();
  }

  //==============================================================================
  const juce::String getName() const override;

  bool acceptsMidi() const override;
  bool producesMidi() const override;
  bool isMidiEffect() const override;
  double getTailLengthSeconds() const override;

  //==============================================================================
  int getNumPrograms() override;
  int getCurrentProgram() override;
  void setCurrentProgram(int index) override;
  const juce::String getProgramName(int index) override;
  void changeProgramName(int index, const juce::String &newName) override;

  // Supported Parameter Listener Methods
  void parameterValueChanged(int parameterIndex, float newValue) override;
  void parameterGestureChanged(int parameterIndex,
                               bool gestureIsStarting) override;

  // Host Info
  void updateTrackProperties(const TrackProperties &properties) override;

  void getStateInformation(juce::MemoryBlock &destData) override;
  void setStateInformation(const void *data, int sizeInBytes) override;

  // Fiddle config routing
  juce::String getConfigPath() const { return currentConfigPath; }
  void setConfigPath(const juce::String &path);

  /// Read the active config path written by FiddleServer (line 1)
  juce::String getActiveServerConfig() const {
    auto f =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Fiddle")
            .getChildFile("active_config.txt");
    if (f.existsAsFile()) {
      auto lines = juce::StringArray::fromLines(f.loadFileAsString());
      if (lines.size() > 0)
        return lines[0].trim();
    }
    return {};
  }

  /// Read the playback delay in ms written by FiddleServer (line 2)
  int getActiveServerDelay() const {
    auto f =
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Fiddle")
            .getChildFile("active_config.txt");
    if (f.existsAsFile()) {
      auto lines = juce::StringArray::fromLines(f.loadFileAsString());
      if (lines.size() > 1)
        return lines[1].trim().getIntValue();
    }
    return 1000; // default
  }

  // Test methods
  int sendTestProgramChange();
  int sendTestContextUpdate();

  // Helper
  fiddle::MidiTcpRelay *getTcpRelay() { return tcpRelay.get(); }

  juce::VST3ClientExtensions *getVST3ClientExtensions() override;

private:
  // Track bank and program for each MIDI channel (0-15)
  struct ChannelState {
    int program = 0;
    int bankMSB = 0;
    int bankLSB = 0;
    juce::String instrumentName = "Unknown";
  };
  std::array<ChannelState, 16> channelStates;

  //==============================================================================
  std::unique_ptr<fiddle::FiddleVST3Extensions> vst3Extensions;
  std::unique_ptr<fiddle::MidiTcpRelay> tcpRelay;
  fiddle::AudioSharedMemory audioSharedMemory_{false}; // False = Consumer

  juce::AudioParameterInt *programParam = nullptr;
  juce::AudioParameterInt *bankMSBParam = nullptr;
  juce::AudioParameterInt *bankLSBParam = nullptr;

  bool wasPlaying = false;
  juce::String currentConfigPath;

  // Track sequential channel assignment for setCurrentProgram() calls.
  // Dorico calls setCurrentProgram() once per instrument in channel order,
  // but the VST3 API is channel-agnostic. We assign channels 1, 2, 3...
  int nextProgramChangeChannel = 1;

  // Delay polling
  int lastKnownDelayMs_ = 1000;
  double cachedSampleRate_ = 44100.0;

  void timerCallback() override {
    int newDelay = getActiveServerDelay();
    if (newDelay != lastKnownDelayMs_) {
      lastKnownDelayMs_ = newDelay;
      setLatencySamples(
          static_cast<int>(cachedSampleRate_ * newDelay / 1000.0));
      updateHostDisplay(
          AudioProcessor::ChangeDetails().withLatencyChanged(true));
    }
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FiddleAudioProcessor)
};
