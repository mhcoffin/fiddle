#pragma once

#include "MidiTcpServer.h"
#include "NoteStreamTracker.h"
#include "ScriptEngine.h"
#include "SubnoteGenerator.h"
#include "midi_event.pb.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class MainComponent : public juce::Component, private juce::Timer {
public:
  MainComponent();
  ~MainComponent() override;

  void paint(juce::Graphics &) override;
  void resized() override;

private:
  juce::File uiDir;
  juce::WebBrowserComponent webComponent;
  std::unique_ptr<fiddle::MidiTcpServer> server;
  ExpressionMap expressionMap;
  NoteStreamTracker noteTracker;
  SubnoteGenerator subnoteGenerator;
  std::unique_ptr<ScriptEngine> scriptEngine;

  uint64_t lastSampleTime = 0;
  uint32_t lastSystemTime = 0;

  void timerCallback() override;
  void setupWebView();
  void pushLogMessage(const juce::String &msg, bool isError = false);
  static juce::String escapeForJS(const juce::String &str);

  void pushEventToWebView(const fiddle::MidiEvent &event);
  void pushSubnoteToWebView(const fiddle::Subnote &subnote);
  std::optional<juce::WebBrowserComponent::Resource>
  getResource(const juce::String &url);

  std::mutex logMutex;
  std::vector<std::pair<juce::String, bool>> logQueue;
  bool webViewLoaded = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace fiddle
