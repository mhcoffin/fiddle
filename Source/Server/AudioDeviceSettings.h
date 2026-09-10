#pragma once
#include <juce_audio_utils/juce_audio_utils.h>

namespace fiddle {
class AudioDeviceSettings final : private juce::ChangeListener {
public:
  AudioDeviceSettings(juce::AudioDeviceManager &manager, juce::File file);
  ~AudioDeviceSettings() override;
  juce::String initialise();
  void show();
  juce::var diagnostics() const;
private:
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  juce::AudioDeviceManager &manager_;
  juce::File file_;
  juce::String warning_;
  std::unique_ptr<juce::DocumentWindow> window_;
};
} // namespace fiddle
