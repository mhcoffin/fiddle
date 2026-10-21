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
        durationsGroup_("durations", "Dorico playback options"),
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

    for (auto &field : doricoFields()) {
      field.label->setText(field.text, juce::dontSendNotification);
      field.label->setJustificationType(juce::Justification::centredRight);
      auto &slider = *field.slider;
      slider.setSliderStyle(juce::Slider::IncDecButtons);
      slider.setIncDecButtonsMode(juce::Slider::incDecButtonsDraggable_Vertical);
      slider.setTextBoxStyle(juce::Slider::TextBoxLeft, false, 60, 26);
      slider.setRange(field.low, field.high, field.step);
      slider.setTextValueSuffix(field.suffix);
      slider.setTooltip("Same value as Dorico's Library > Playback Options for this score");
      slider.onValueChange = [this] {
        if (refreshing_ || !controls_.setDoricoPlayback || !controls_.getDoricoPlayback)
          return;
        auto settings = controls_.getDoricoPlayback();
        for (auto &f : doricoFields())
          f.write(settings, f.slider->getValue());
        controls_.setDoricoPlayback(settings);
      };
      addAndMakeVisible(*field.label);
      addAndMakeVisible(slider);
    }
    durationsHelp_.setText(
        "Copy these from this score's Library > Playback Options (Timing, Dynamics) in Dorico. "
        "Bridge maps make Dorico send written lengths and linear dynamics; Fiddle applies these "
        "values to each layer unless its expression map overrides them. Saved with the project.",
        juce::dontSendNotification);
    durationsHelp_.setJustificationType(juce::Justification::topLeft);

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
    addAndMakeVisible(durationsGroup_);
    addAndMakeVisible(durationsHelp_);
    addAndMakeVisible(renderingGroup_);
    addAndMakeVisible(workersLabel_);
    addAndMakeVisible(workers_);
    addAndMakeVisible(workerStatus_);
    addAndMakeVisible(workerGuidance_);
    addAndMakeVisible(deviceGroup_);
    addAndMakeVisible(selector_);
    setSize(780, 1060);
    refresh();
    startTimerHz(4);
  }

  void refresh() {
    const juce::ScopedValueSetter<bool> guard(refreshing_, true);
    if (controls_.getPlaybackDelayMs)
      delay_.setValue(controls_.getPlaybackDelayMs(), juce::dontSendNotification);
    else
      delay_.setEnabled(false);
    const auto dorico = controls_.getDoricoPlayback ? controls_.getDoricoPlayback()
                                                    : DoricoPlaybackSettings{};
    for (auto &field : doricoFields()) {
      field.slider->setValue(field.read(dorico), juce::dontSendNotification);
      field.slider->setEnabled(static_cast<bool>(controls_.setDoricoPlayback));
    }

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
    auto &fields = doricoFields();
    const int rows = static_cast<int>((fields.size() + 2) / 3);
    auto durations = area.removeFromTop(78 + 32 * rows);
    durationsGroup_.setBounds(durations);
    durations = durations.reduced(14, 10);
    durations.removeFromTop(18);
    juce::Rectangle<int> line;
    for (size_t i = 0; i < fields.size(); ++i) {
      if (i % 3 == 0)
        line = durations.removeFromTop(32);
      auto slot = line.removeFromLeft((durations.getWidth()) / 3).reduced(4, 2);
      fields[i].label->setBounds(slot.removeFromLeft(118));
      fields[i].slider->setBounds(slot.removeFromLeft(104));
    }
    durationsHelp_.setBounds(durations.removeFromTop(44));

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
  juce::GroupComponent playbackGroup_, durationsGroup_, renderingGroup_, deviceGroup_;
  juce::Label durationsHelp_;

  /// One labelled value in the Dorico playback group.
  struct DoricoField {
    std::unique_ptr<juce::Label> label = std::make_unique<juce::Label>();
    std::unique_ptr<juce::Slider> slider = std::make_unique<juce::Slider>();
    juce::String text, suffix;
    double low, high, step;
    std::function<double(const DoricoPlaybackSettings &)> read;
    std::function<void(DoricoPlaybackSettings &, double)> write;
  };
  std::vector<DoricoField> doricoFields_;
  std::vector<DoricoField> &doricoFields() {
    if (!doricoFields_.empty())
      return doricoFields_;
    using S = DoricoPlaybackSettings;
    const auto add = [this](juce::String text, juce::String suffix, double low, double high, double step,
                            std::function<double(const S &)> read, std::function<void(S &, double)> write) {
      DoricoField field;
      field.text = std::move(text); field.suffix = std::move(suffix);
      field.low = low; field.high = high; field.step = step;
      field.read = std::move(read); field.write = std::move(write);
      doricoFields_.push_back(std::move(field));
    };
    for (size_t i = 0; i < kDurationCategoryCount; ++i)
      add(durationCategoryLabels()[i], "%", kMinDurationPercent, kMaxDurationPercent, 1,
          [i](const S &s) { return s.durations[i]; },
          [i](S &s, double v) { s.durations[i] = static_cast<int>(v); });
    add("Switches before", " ticks", 0, kMaxSwitchLeadTicks, 1,
        [](const S &s) { return s.switchLeadTicks; },
        [](S &s, double v) { s.switchLeadTicks = static_cast<int>(v); });
    add("Curve power", "", 0.1, 8.0, 0.05,
        [](const S &s) { return s.dynamics.lowerPower; },
        [](S &s, double v) { s.dynamics.lowerPower = s.dynamics.upperPower = v; });
    add("Min level", "", -12, 0, 1,
        [](const S &s) { return s.dynamics.minLevel; },
        [](S &s, double v) { s.dynamics.minLevel = static_cast<int>(v); });
    add("Max level", "", 0, 12, 1,
        [](const S &s) { return s.dynamics.maxLevel; },
        [](S &s, double v) { s.dynamics.maxLevel = static_cast<int>(v); });
    add("Accents +", "", 0, 6, 0.05,
        [](const S &s) { return s.dynamics.accent; },
        [](S &s, double v) { s.dynamics.accent = v; });
    add("Marcato +", "", 0, 6, 0.05,
        [](const S &s) { return s.dynamics.marcato; },
        [](S &s, double v) { s.dynamics.marcato = v; });
    add("Tenuto +", "", 0, 6, 0.05,
        [](const S &s) { return s.dynamics.tenuto; },
        [](S &s, double v) { s.dynamics.tenuto = v; });
    add("Ghost notes −", "", 0, 6, 0.05,
        [](const S &s) { return s.dynamics.ghost; },
        [](S &s, double v) { s.dynamics.ghost = v; });
    return doricoFields_;
  }
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
