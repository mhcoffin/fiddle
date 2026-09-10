#include "AudioDeviceSettings.h"
#include "AudioDeviceSettingsStore.h"

namespace fiddle {
namespace {
class SettingsContent final : public juce::Component {
public:
  explicit SettingsContent(juce::AudioDeviceManager &manager)
      : selector_(manager, 0, 0, 2, 2, false, false, true, false) {
    note_.setText("Stop Dorico playback before changing settings. Changes apply immediately "
                  "and are remembered on this Mac, not in project versions. Match Dorico's "
                  "sample rate; this connection does not resample audio.", juce::dontSendNotification);
    note_.setFont(juce::Font(juce::FontOptions(16.0f)));
    note_.setJustificationType(juce::Justification::topLeft);
    selector_.setItemHeight(36);
    addAndMakeVisible(note_);
    addAndMakeVisible(selector_);
    setSize(720, 540);
  }
  void resized() override {
    auto area = getLocalBounds().reduced(24);
    note_.setBounds(area.removeFromTop(96));
    selector_.setBounds(area);
  }
private:
  juce::Label note_;
  juce::AudioDeviceSelectorComponent selector_;
};
class SettingsWindow final : public juce::DocumentWindow {
public:
  explicit SettingsWindow(juce::AudioDeviceManager &manager)
      : DocumentWindow("Fiddle Audio Settings", juce::Colour(0xff182638),
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(new SettingsContent(manager), true);
    centreWithSize(getWidth(), getHeight());
  }
  void closeButtonPressed() override { setVisible(false); }
};
}

AudioDeviceSettings::AudioDeviceSettings(juce::AudioDeviceManager &manager, juce::File file)
    : manager_(manager), file_(std::move(file)) {}
AudioDeviceSettings::~AudioDeviceSettings() {
  manager_.removeChangeListener(this);
  window_.reset();
}

juce::String AudioDeviceSettings::initialise() {
  auto saved = AudioDeviceSettingsStore::load(file_, warning_);
  auto error = manager_.initialise(0, 2, saved.get(), false);
  if (error.isNotEmpty() && saved) {
    warning_ = "Saved audio device unavailable (" + error + "). Using the default device; "
               "the saved choice is retained until you change settings.";
    error = manager_.initialiseWithDefaultDevices(0, 2);
  }
  manager_.addChangeListener(this);
  return error;
}

void AudioDeviceSettings::show() {
  if (!window_) window_ = std::make_unique<SettingsWindow>(manager_);
  window_->setVisible(true);
  window_->setMinimised(false);
  window_->toFront(true);
}

void AudioDeviceSettings::changeListenerCallback(juce::ChangeBroadcaster *) {
  // JUCE only produces XML for explicitly chosen/restored settings, not defaults.
  // Device discovery and telemetry never create a new preference themselves.
  if (auto xml = manager_.createStateXml()) {
    if (!AudioDeviceSettingsStore::save(file_, *xml))
      warning_ = "Audio settings are active, but could not be saved to " + file_.getFullPathName();
    else if (window_ && window_->isVisible())
      warning_.clear();
  }
}

juce::var AudioDeviceSettings::diagnostics() const {
  auto *data = new juce::DynamicObject();
  data->setProperty("type", manager_.getCurrentAudioDeviceType());
  data->setProperty("settingsFile", file_.getFullPathName());
  data->setProperty("warning", warning_);
  if (auto *device = manager_.getCurrentAudioDevice()) {
    data->setProperty("name", device->getName());
    data->setProperty("sampleRate", device->getCurrentSampleRate());
    data->setProperty("bufferSize", device->getCurrentBufferSizeSamples());
    data->setProperty("outputLatencySamples", device->getOutputLatencyInSamples());
  }
  return juce::var(data);
}
} // namespace fiddle
