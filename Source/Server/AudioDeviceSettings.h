#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <functional>

namespace fiddle {
class AudioDeviceSettings final : private juce::ChangeListener {
public:
  struct Controls {
    std::function<int()> getPlaybackDelayMs;
    std::function<void(int)> setPlaybackDelayMs;
    std::function<void()> beginPlaybackDelayGesture;
    std::function<void()> endPlaybackDelayGesture;
    std::function<int()> getRequestedRenderWorkers;
    std::function<int()> getActiveRenderWorkers;
    std::function<int()> getMaximumRenderWorkers;
    std::function<bool()> canChangeRenderWorkers;
    std::function<void(int)> setRenderWorkers;
  };

  AudioDeviceSettings(juce::AudioDeviceManager &manager, juce::File file,
                      Controls controls = {});
  ~AudioDeviceSettings() override;
  juce::String initialise();
  void show();
  juce::var diagnostics() const;
private:
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  juce::AudioDeviceManager &manager_;
  juce::File file_;
  Controls controls_;
  juce::String warning_;
  std::unique_ptr<juce::DocumentWindow> window_;
};
} // namespace fiddle
