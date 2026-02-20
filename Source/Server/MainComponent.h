#pragma once

#include "../AudioSharedMemory.h"
#include "DoricoInstrumentBrowser.h"
#include "InstrumentMapper.h"
#include "MasterInstrumentList.h"
#include "MidiTcpServer.h"
#include "MixerModel.h"
#include "NoteStreamTracker.h"
#include "PluginHost.h"
#include "PluginScanner.h"
#include "ScriptEngine.h"
#include "SubnoteGenerator.h"
#include "midi_event.pb.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class MainComponent : public juce::Component,
                      private juce::Timer,
                      public juce::AudioIODeviceCallback {
public:
  MainComponent(const juce::File &configFile);
  ~MainComponent() override;

  /// Save current state to the active config file
  void saveConfig();

  /// Save current state to a new named config file, switch to it
  void saveConfigAs(const juce::File &newFile);

  void paint(juce::Graphics &) override;
  void resized() override;

  void audioDeviceIOCallbackWithContext(
      const float *const *inputChannelData, int numInputChannels,
      float *const *outputChannelData, int numOutputChannels, int numSamples,
      const juce::AudioIODeviceCallbackContext &context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice *device) override;
  void audioDeviceStopped() override;

private:
  juce::File uiDir;
  juce::WebBrowserComponent webComponent;
  juce::AudioDeviceManager deviceManager;
  DoricoInstrumentBrowser instrumentBrowser_;
  MasterInstrumentList masterList_;
  std::unique_ptr<fiddle::MidiTcpServer> server;
  ExpressionMap expressionMap;
  NoteStreamTracker noteTracker;
  SubnoteGenerator subnoteGenerator;
  InstrumentMapper instrumentMapper_;
  PluginScanner pluginScanner_;
  PluginHost pluginHost_;
  MixerModel mixer_;
  std::unique_ptr<ScriptEngine> scriptEngine;
  AudioSharedMemory audioSharedMemory_{true}; // True = Producer

  uint64_t lastSampleTime = 0;
  uint32_t lastSystemTime = 0;

  juce::File currentConfigFile;

  void timerCallback() override;
  void setupWebView();
  void pushLogMessage(const juce::String &msg, bool isError = false);
  void pushMixerState();
  static juce::String escapeForJS(const juce::String &str);

  void pushEventToWebView(const fiddle::MidiEvent &event);
  void pushSubnoteToWebView(const fiddle::Subnote &subnote);
  std::optional<juce::WebBrowserComponent::Resource>
  getResource(const juce::String &url);

  std::mutex logMutex;
  std::vector<std::pair<juce::String, bool>> logQueue;
  bool webViewLoaded = false;
  juce::String cachedInstrCall_; // Cached escaped JS call for instruments

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace fiddle
