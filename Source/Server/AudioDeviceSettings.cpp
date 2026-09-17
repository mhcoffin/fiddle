#include "AudioDeviceSettings.h"
#include "AudioDeviceSettingsStore.h"

namespace fiddle {
namespace {
class SettingsContent final : public juce::Component, private juce::Timer {
public:
  SettingsContent(juce::AudioDeviceManager &manager,
                  AudioDeviceSettings::Controls controls)
      : controls_(std::move(controls)),
        playbackGroup_("playback", "Playback"),
        renderingGroup_("rendering", "Rendering"),
        deviceGroup_("device", "Audio output"),
        selector_(manager, 0, 0, 2, 2, false, false, true, false) {
    note_.setText(
        "Playback delay is saved with the project. Rendering and audio-output "
        "choices are remembered on this Mac. Stop Dorico playback before "
        "changing rendering or audio hardware.",
        juce::dontSendNotification);
    note_.setFont(juce::Font(juce::FontOptions(15.0f)));
    note_.setJustificationType(juce::Justification::topLeft);

    delayLabel_.setText("Delay", juce::dontSendNotification);
    delayLabel_.setJustificationType(juce::Justification::centredLeft);
    delay_.setSliderStyle(juce::Slider::LinearHorizontal);
    delay_.setRange(0.0, 5000.0, 50.0);
    delay_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 92, 28);
    delay_.setTextValueSuffix(" ms");
    delay_.setTooltip("Time available for Fiddle to prepare audio before Dorico plays it");
    delay_.onValueChange = [this] {
      if (!refreshing_ && controls_.setPlaybackDelayMs)
        controls_.setPlaybackDelayMs(static_cast<int>(delay_.getValue()));
    };
    delay_.onDragStart = [this] {
      if (controls_.beginPlaybackDelayGesture)
        controls_.beginPlaybackDelayGesture();
    };
    delay_.onDragEnd = [this] {
      if (controls_.endPlaybackDelayGesture)
        controls_.endPlaybackDelayGesture();
    };
    delayHelp_.setText("Saved with the project; Undo and Redo apply.",
                       juce::dontSendNotification);

    workersLabel_.setText("Render workers", juce::dontSendNotification);
    workersLabel_.setJustificationType(juce::Justification::centredLeft);
    workers_.setSliderStyle(juce::Slider::IncDecButtons);
    workers_.setIncDecButtonsMode(juce::Slider::incDecButtonsNotDraggable);
    workers_.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 70, 28);
    workers_.setNumDecimalPlacesToDisplay(0);
    const auto maximumWorkers = controls_.getMaximumRenderWorkers
                                    ? controls_.getMaximumRenderWorkers()
                                    : 64;
    workers_.setRange(1.0, static_cast<double>(maximumWorkers), 1.0);
    workers_.setTooltip("Total render threads, including the coordinator");
    workers_.onValueChange = [this] {
      if (!refreshing_ && controls_.setRenderWorkers)
        controls_.setRenderWorkers(static_cast<int>(workers_.getValue()));
    };
    workerStatus_.setJustificationType(juce::Justification::topLeft);
    workerGuidance_.setText(
        "The count includes the coordinator. Start with 2-4 and increase only "
        "if Audio Performance improves; excess threads add scheduling overhead. "
        "Maximum " +
            juce::String(maximumWorkers) + ".",
        juce::dontSendNotification);
    workerGuidance_.setJustificationType(juce::Justification::topLeft);

    selector_.setItemHeight(36);
    addAndMakeVisible(note_);
    addAndMakeVisible(playbackGroup_);
    addAndMakeVisible(delayLabel_);
    addAndMakeVisible(delay_);
    addAndMakeVisible(delayHelp_);
    addAndMakeVisible(renderingGroup_);
    addAndMakeVisible(workersLabel_);
    addAndMakeVisible(workers_);
    addAndMakeVisible(workerStatus_);
    addAndMakeVisible(workerGuidance_);
    addAndMakeVisible(deviceGroup_);
    addAndMakeVisible(selector_);
    setSize(780, 760);
    refresh();
    startTimerHz(4);
  }

  void refresh() {
    const juce::ScopedValueSetter<bool> guard(refreshing_, true);
    if (controls_.getPlaybackDelayMs)
      delay_.setValue(controls_.getPlaybackDelayMs(), juce::dontSendNotification);
    else
      delay_.setEnabled(false);

    const auto requested = controls_.getRequestedRenderWorkers
                               ? controls_.getRequestedRenderWorkers()
                               : 1;
    workers_.setValue(requested, juce::dontSendNotification);
    const bool allowed = controls_.canChangeRenderWorkers &&
                         controls_.canChangeRenderWorkers();
    workers_.setEnabled(allowed && static_cast<bool>(controls_.setRenderWorkers));
    const auto active = controls_.getActiveRenderWorkers
                            ? controls_.getActiveRenderWorkers()
                            : requested;
    workerStatus_.setText(
        allowed
            ? juce::String(active) + " active worker" + (active == 1 ? "" : "s") +
                  ". Remembered on this Mac, not in project versions."
            : "Stop Dorico playback and disarm printing to change workers.",
        juce::dontSendNotification);
  }

  void resized() override {
    auto area = getLocalBounds().reduced(24);
    note_.setBounds(area.removeFromTop(58));

    auto playback = area.removeFromTop(100);
    playbackGroup_.setBounds(playback);
    playback = playback.reduced(14, 10);
    playback.removeFromTop(18);
    delayLabel_.setBounds(playback.removeFromLeft(96).removeFromTop(30));
    delay_.setBounds(playback.removeFromTop(30));
    delayHelp_.setBounds(playback.removeFromTop(28));

    area.removeFromTop(10);
    auto rendering = area.removeFromTop(154);
    renderingGroup_.setBounds(rendering);
    rendering = rendering.reduced(14, 10);
    rendering.removeFromTop(18);
    workersLabel_.setBounds(rendering.removeFromLeft(120).removeFromTop(32));
    workers_.setBounds(rendering.removeFromTop(32).removeFromLeft(210));
    workerStatus_.setBounds(rendering.removeFromTop(28));
    workerGuidance_.setBounds(rendering.removeFromTop(56));

    area.removeFromTop(10);
    deviceGroup_.setBounds(area);
    selector_.setBounds(area.reduced(14, 28));
  }

private:
  void timerCallback() override { refresh(); }

  AudioDeviceSettings::Controls controls_;
  bool refreshing_ = false;
  juce::Label note_, delayLabel_, delayHelp_, workersLabel_, workerStatus_,
      workerGuidance_;
  juce::GroupComponent playbackGroup_, renderingGroup_, deviceGroup_;
  juce::Slider delay_;
  juce::Slider workers_;
  juce::AudioDeviceSelectorComponent selector_;
};
class SettingsWindow final : public juce::DocumentWindow {
public:
  SettingsWindow(juce::AudioDeviceManager &manager,
                 AudioDeviceSettings::Controls controls)
      : DocumentWindow("Fiddle Settings", juce::Colour(0xff182638),
                       DocumentWindow::closeButton) {
    setUsingNativeTitleBar(true);
    setContentOwned(new SettingsContent(manager, std::move(controls)), true);
    setResizable(true, false);
    centreWithSize(getWidth(), getHeight());
  }
  void closeButtonPressed() override { setVisible(false); }
};
}

AudioDeviceSettings::AudioDeviceSettings(juce::AudioDeviceManager &manager,
                                         juce::File file, Controls controls)
    : manager_(manager), file_(std::move(file)), controls_(std::move(controls)) {}
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
  if (!window_)
    window_ = std::make_unique<SettingsWindow>(manager_, controls_);
  if (auto *content =
          dynamic_cast<SettingsContent *>(window_->getContentComponent()))
    content->refresh();
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
