#include "../HostedPluginSlot.h"

#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                    \
                << "]: " << #condition << std::endl;                          \
    }                                                                          \
  } while (false)

class StatefulTestProcessor final : public juce::AudioProcessor {
public:
  enum class Kind { instrument, multiOutputInstrument, effect, monoEffect };

  explicit StatefulTestProcessor(Kind kind,
                                 std::atomic<int> *destructionCount = nullptr)
      : juce::AudioProcessor(makeBuses(kind)), kind_(kind),
        destructionCount_(destructionCount) {
    gain_ = new juce::AudioParameterFloat(
        juce::ParameterID{"gain", 1}, "Gain",
        juce::NormalisableRange<float>(0.0f, 2.0f), 1.0f);
    addParameter(gain_);
  }

  ~StatefulTestProcessor() override {
    if (destructionCount_)
      destructionCount_->fetch_add(1, std::memory_order_relaxed);
  }

  const juce::String getName() const override { return "Stateful test"; }
  void prepareToPlay(double sampleRate, int blockSize) override {
    preparedSampleRate = sampleRate;
    preparedBlockSize = blockSize;
  }
  void releaseResources() override { released = true; }
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override {
    return kind_ == Kind::instrument || kind_ == Kind::multiOutputInstrument;
  }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "Default"; }
  void changeProgramName(int, const juce::String &) override {}

  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    playbackCounter_.fetch_add(1, std::memory_order_relaxed);
    audio.applyGain(gain_->get());
  }

  void getStateInformation(juce::MemoryBlock &destination) override {
    const float value = gain_->get();
    destination.append(&value, sizeof(value));
    const auto playbackCounter =
        playbackCounter_.load(std::memory_order_relaxed);
    destination.append(&playbackCounter, sizeof(playbackCounter));
  }

  void setStateInformation(const void *data, int sizeInBytes) override {
    if (data == nullptr || sizeInBytes < static_cast<int>(sizeof(float)))
      return;
    float value = 0.0f;
    std::memcpy(&value, data, sizeof(value));
    setGain(value);
  }

  bool isBusesLayoutSupported(const BusesLayout &layout) const override {
    const auto output = layout.getMainOutputChannelSet();
    if (kind_ == Kind::instrument)
      return layout.getMainInputChannelSet().isDisabled() &&
             output == juce::AudioChannelSet::stereo();
    if (kind_ == Kind::multiOutputInstrument)
      return layout.getMainInputChannelSet().isDisabled() &&
             output == juce::AudioChannelSet::stereo() &&
             layout.outputBuses.size() == 2 &&
             layout.outputBuses[1] == juce::AudioChannelSet::stereo();
    if (kind_ == Kind::monoEffect)
      return layout.getMainInputChannelSet() == juce::AudioChannelSet::mono() &&
             output == juce::AudioChannelSet::mono();
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           output == juce::AudioChannelSet::stereo();
  }

  void setGain(float value) {
    gain_->setValueNotifyingHost(gain_->convertTo0to1(value));
  }
  void setGainWithGesture(float value) {
    gain_->beginChangeGesture();
    setGain(value);
    gain_->endChangeGesture();
  }
  void notifyNonParameterStateChanged() {
    updateHostDisplay(
        ChangeDetails{}.withNonParameterStateChanged(true));
  }
  [[nodiscard]] float gain() const noexcept { return gain_->get(); }

  double preparedSampleRate = 0.0;
  int preparedBlockSize = 0;
  bool released = false;

private:
  static BusesProperties makeBuses(Kind kind) {
    if (kind == Kind::instrument)
      return BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true);
    if (kind == Kind::multiOutputInstrument)
      return BusesProperties()
          .withOutput("Main Output", juce::AudioChannelSet::stereo(), true)
          .withOutput("Aux Output", juce::AudioChannelSet::stereo(), true);
    const auto channels = kind == Kind::monoEffect
                              ? juce::AudioChannelSet::mono()
                              : juce::AudioChannelSet::stereo();
    return BusesProperties()
        .withInput("Input", channels, true)
        .withOutput("Output", channels, true);
  }

  Kind kind_;
  std::atomic<int> *destructionCount_ = nullptr;
  juce::AudioParameterFloat *gain_ = nullptr;
  std::atomic<uint32_t> playbackCounter_{0};
};

juce::PluginDescription makeDescription(int uid, bool isInstrument,
                                        int inputs, int outputs) {
  juce::PluginDescription description;
  description.name = isInstrument ? "Test Instrument" : "Test Effect";
  description.pluginFormatName = "Test";
  description.uniqueId = uid;
  description.isInstrument = isInstrument;
  description.numInputChannels = inputs;
  description.numOutputChannels = outputs;
  return description;
}

void testCompatibilityMetadata() {
  const auto instrument = makeDescription(1, true, 0, 2);
  const auto instrumentCompatibility =
      fiddle::PluginCompatibility::fromDescription(
          instrument, fiddle::PluginSlotRole::instrument);
  CHECK(instrumentCompatibility.compatible);
  CHECK(instrumentCompatibility.isInstrument);
  CHECK(!instrumentCompatibility.hasStereoInput);
  CHECK(instrumentCompatibility.hasStereoOutput);

  const auto effect = makeDescription(2, false, 2, 2);
  const auto effectCompatibility =
      fiddle::PluginCompatibility::fromDescription(
          effect, fiddle::PluginSlotRole::effect);
  CHECK(effectCompatibility.compatible);
  CHECK(!effectCompatibility.isInstrument);
  CHECK(effectCompatibility.hasStereoInput);
  CHECK(effectCompatibility.hasStereoOutput);

  CHECK(!fiddle::PluginCompatibility::fromDescription(
             effect, fiddle::PluginSlotRole::instrument)
             .compatible);
  CHECK(!fiddle::PluginCompatibility::fromDescription(
             instrument, fiddle::PluginSlotRole::effect)
             .compatible);
  CHECK(!fiddle::PluginCompatibility::fromDescription(
             makeDescription(3, false, 1, 1),
             fiddle::PluginSlotRole::effect)
             .compatible);

  const auto deferredInstrument =
      fiddle::PluginCompatibility::fromDescription(
          makeDescription(4, true, 0, 0),
          fiddle::PluginSlotRole::instrument);
  CHECK(deferredInstrument.compatible);
  CHECK(deferredInstrument.requiresRuntimeValidation);
}

void testEffectLifecycleStateBypassAndNotification() {
  fiddle::HostedPluginSlot slot(fiddle::PluginSlotRole::effect);
  slot.setId({"master:insert:0"});
  const auto description = makeDescription(42, false, 2, 2);
  auto processor = std::make_unique<StatefulTestProcessor>(
      StatefulTestProcessor::Kind::effect);
  auto *processorPointer = processor.get();
  juce::String error;

  CHECK(slot.installProcessor(description, std::move(processor), 48000.0, 256,
                              error));
  CHECK(error.isEmpty());
  CHECK(slot.id().value == "master:insert:0");
  CHECK(slot.status() == fiddle::HostedPluginStatus::loaded);
  CHECK(slot.hasProcessor());
  CHECK(slot.pluginUid() == 42);
  CHECK(slot.compatibility().compatible);
  CHECK(processorPointer->preparedSampleRate == 48000.0);
  CHECK(processorPointer->preparedBlockSize == 256);

  processorPointer->setGain(0.25f);
  CHECK(slot.consumeChangeNotification());
  CHECK(!slot.consumeExplicitEditNotification());
  CHECK(!slot.consumeChangeNotification());
  slot.refreshStateCache();
  const auto saved = slot.cachedState();
  CHECK(saved.getSize() == sizeof(float) + sizeof(uint32_t));

  processorPointer->setGain(1.5f);
  CHECK(slot.applyState(saved.getData(), static_cast<int>(saved.getSize())));
  CHECK(std::abs(processorPointer->gain() - 0.25f) < 1.0e-6f);

  juce::AudioBuffer<float> audio(2, 16);
  juce::MidiBuffer midi;
  audio.clear();
  for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    juce::FloatVectorOperations::fill(audio.getWritePointer(channel), 1.0f,
                                      audio.getNumSamples());
  CHECK(slot.processBlock(audio, midi));
  CHECK(std::abs(audio.getSample(0, 0) - 0.25f) < 1.0e-6f);

  slot.setBypassed(true);
  CHECK(slot.isBypassed());
  for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    juce::FloatVectorOperations::fill(audio.getWritePointer(channel), 1.0f,
                                      audio.getNumSamples());
  CHECK(slot.processBlock(audio, midi));
  CHECK(std::abs(audio.getSample(0, 0) - 1.0f) < 1.0e-6f);
  slot.setBypassed(false);
  CHECK(!slot.isBypassed());

  slot.markMissing(description, saved, "Plug-in file not found");
  CHECK(!slot.hasProcessor());
  CHECK(slot.status() == fiddle::HostedPluginStatus::missing);
  CHECK(slot.pluginUid() == 42);
  CHECK(slot.missingState().has_value());
  CHECK(slot.missingState()->state == saved);
  CHECK(slot.lastError() == "Plug-in file not found");
  slot.reclaimRetiredRuntimes();
}

void testParameterFingerprintIgnoresVolatilePlaybackState() {
  fiddle::HostedPluginSlot slot(fiddle::PluginSlotRole::instrument);
  auto processor = std::make_unique<StatefulTestProcessor>(
      StatefulTestProcessor::Kind::instrument);
  auto *processorPointer = processor.get();
  juce::String error;
  CHECK(slot.installProcessor(makeDescription(43, true, 0, 2),
                              std::move(processor), 48000.0, 64, error));

  const auto originalFingerprint = slot.parameterFingerprint();
  juce::MemoryBlock stateBeforePlayback;
  CHECK(slot.captureState(stateBeforePlayback));

  juce::AudioBuffer<float> audio(2, 64);
  juce::MidiBuffer midi;
  audio.clear();
  CHECK(slot.processBlock(audio, midi));
  CHECK(slot.processBlock(audio, midi));

  juce::MemoryBlock stateAfterPlayback;
  CHECK(slot.captureState(stateAfterPlayback));
  CHECK(stateAfterPlayback != stateBeforePlayback);
  CHECK(slot.parameterFingerprint() == originalFingerprint);

  processorPointer->setGain(0.75f);
  CHECK(slot.parameterFingerprint() != originalFingerprint);

  CHECK(slot.consumeChangeNotification());
  CHECK(!slot.consumeExplicitEditNotification());
  processorPointer->setGainWithGesture(0.5f);
  CHECK(slot.consumeChangeNotification());
  CHECK(slot.consumeExplicitEditNotification());

  processorPointer->notifyNonParameterStateChanged();
  CHECK(slot.consumeChangeNotification());
  CHECK(!slot.consumeExplicitEditNotification());
  CHECK(slot.consumeNonParameterStateChangeNotification());
}

void testLayoutRejectionAndDeferredReclamation() {
  fiddle::HostedPluginSlot slot(fiddle::PluginSlotRole::effect);
  juce::String error;

  CHECK(!slot.installProcessor(
      makeDescription(1, true, 0, 2),
      std::make_unique<StatefulTestProcessor>(
          StatefulTestProcessor::Kind::instrument),
      44100.0, 64, error));
  CHECK(error.contains("cannot occupy effect"));

  error.clear();
  CHECK(!slot.installProcessor(
      makeDescription(2, false, 2, 2),
      std::make_unique<StatefulTestProcessor>(
          StatefulTestProcessor::Kind::monoEffect),
      44100.0, 64, error));
  CHECK(error.contains("rejected a stereo"));

  std::atomic<int> destructionCount{0};
  error.clear();
  CHECK(slot.installProcessor(
      makeDescription(3, false, 2, 2),
      std::make_unique<StatefulTestProcessor>(
          StatefulTestProcessor::Kind::effect, &destructionCount),
      44100.0, 64, error));
  auto *originalProcessor = slot.activeProcessor();

  error.clear();
  CHECK(!slot.installProcessor(
      makeDescription(30, true, 0, 2),
      std::make_unique<StatefulTestProcessor>(
          StatefulTestProcessor::Kind::instrument),
      44100.0, 64, error));
  CHECK(slot.status() == fiddle::HostedPluginStatus::loaded);
  CHECK(slot.pluginUid() == 3);
  CHECK(slot.activeProcessor() == originalProcessor);
  CHECK(slot.lastError().contains("cannot occupy effect"));

  {
    auto audioRead = slot.readRuntime();
    CHECK(audioRead.get() != nullptr);
    CHECK(slot.installProcessor(
        makeDescription(4, false, 2, 2),
        std::make_unique<StatefulTestProcessor>(
            StatefulTestProcessor::Kind::effect, &destructionCount),
        44100.0, 64, error));
    slot.reclaimRetiredRuntimes();
    CHECK(destructionCount.load(std::memory_order_relaxed) == 0);
  }

  slot.reclaimRetiredRuntimes();
  CHECK(destructionCount.load(std::memory_order_relaxed) == 1);
  slot.unload();
  slot.reclaimRetiredRuntimes();
  CHECK(destructionCount.load(std::memory_order_relaxed) == 2);
}

void testMultiOutputInstrumentLayoutIsPreserved() {
  fiddle::HostedPluginSlot slot(fiddle::PluginSlotRole::instrument);
  auto processor = std::make_unique<StatefulTestProcessor>(
      StatefulTestProcessor::Kind::multiOutputInstrument);
  auto *processorPointer = processor.get();
  juce::String error;

  CHECK(slot.installProcessor(makeDescription(5, true, 0, 4),
                              std::move(processor), 48000.0, 128, error));
  CHECK(error.isEmpty());
  CHECK(processorPointer->getTotalNumOutputChannels() == 4);
  CHECK(processorPointer->getChannelCountOfBus(false, 1) == 2);

  {
    auto runtime = slot.readRuntime();
    CHECK(runtime.get() != nullptr);
    CHECK(runtime.get()->scratchBuffer.getNumChannels() == 4);
  }
  slot.unload();
  slot.reclaimRetiredRuntimes();
}

void testInstalledEffect(const juce::String &pluginPath, bool openEditor) {
  juce::VST3PluginFormat format;
  juce::OwnedArray<juce::PluginDescription> descriptions;
  format.findAllTypesForFile(descriptions, pluginPath);

  juce::PluginDescription *effectDescription = nullptr;
  for (auto *description : descriptions) {
    if (fiddle::PluginCompatibility::fromDescription(
            *description, fiddle::PluginSlotRole::effect)
            .compatible) {
      effectDescription = description;
      break;
    }
  }
  CHECK(effectDescription != nullptr);
  if (effectDescription == nullptr)
    return;

  juce::AudioPluginFormatManager manager;
  manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
  juce::String creationError;
  auto instance = manager.createPluginInstance(*effectDescription, 48000.0, 256,
                                               creationError);
  CHECK(instance != nullptr);
  CHECK(creationError.isEmpty());
  if (!instance)
    return;

  fiddle::HostedPluginSlot slot(fiddle::PluginSlotRole::effect);
  slot.setId({"manual-smoke:insert:0"});
  juce::String installError;
  CHECK(slot.installProcessor(*effectDescription, std::move(instance), 48000.0,
                              256, installError));
  CHECK(installError.isEmpty());

  juce::MemoryBlock saved;
  CHECK(slot.captureState(saved));
  if (!saved.isEmpty())
    CHECK(slot.applyState(saved.getData(), static_cast<int>(saved.getSize())));

  slot.setBypassed(true);
  CHECK(slot.isBypassed());
  juce::AudioBuffer<float> bypassAudio(2, 32);
  juce::MidiBuffer bypassMidi;
  for (int channel = 0; channel < bypassAudio.getNumChannels(); ++channel)
    juce::FloatVectorOperations::fill(
        bypassAudio.getWritePointer(channel), 0.125f,
        bypassAudio.getNumSamples());
  CHECK(slot.processBlock(bypassAudio, bypassMidi));
  CHECK(std::abs(bypassAudio.getSample(0, 0) - 0.125f) < 1.0e-6f);
  slot.setBypassed(false);
  CHECK(!slot.isBypassed());

  if (openEditor) {
    CHECK(slot.activeProcessor()->hasEditor());
    slot.showEditor("Fiddle effect-slot smoke test");
    slot.closeEditor();
  }

  std::cout << "Installed effect: " << effectDescription->name << '\n';
  slot.unload();
  slot.reclaimRetiredRuntimes();
}

void testInstalledInstrument(const juce::String &pluginPath) {
  juce::VST3PluginFormat format;
  juce::OwnedArray<juce::PluginDescription> descriptions;
  format.findAllTypesForFile(descriptions, pluginPath);

  juce::PluginDescription *instrumentDescription = nullptr;
  for (auto *description : descriptions) {
    if (fiddle::PluginCompatibility::fromDescription(
            *description, fiddle::PluginSlotRole::instrument)
            .compatible) {
      instrumentDescription = description;
      break;
    }
  }
  CHECK(instrumentDescription != nullptr);
  if (instrumentDescription == nullptr)
    return;

  juce::AudioPluginFormatManager manager;
  manager.addFormat(std::make_unique<juce::VST3PluginFormat>());
  juce::String creationError;
  auto instance = manager.createPluginInstance(
      *instrumentDescription, 48000.0, 256, creationError);
  CHECK(instance != nullptr);
  CHECK(creationError.isEmpty());
  if (!instance)
    return;

  fiddle::HostedPluginSlot slot(fiddle::PluginSlotRole::instrument);
  slot.setId({"manual-smoke:instrument"});
  juce::String installError;
  CHECK(slot.installProcessor(*instrumentDescription, std::move(instance),
                              48000.0, 256, installError));
  CHECK(installError.isEmpty());
  if (!slot.hasProcessor())
    return;

  const int channels = juce::jmax(
      slot.activeProcessor()->getTotalNumInputChannels(),
      slot.activeProcessor()->getTotalNumOutputChannels(), 2);
  juce::AudioBuffer<float> audio(channels, 256);
  juce::MidiBuffer midi;
  audio.clear();
  CHECK(slot.processBlock(audio, midi));

  std::cout << "Installed instrument: " << instrumentDescription->name
            << " (" << channels << " active channels)\n";
  slot.unload();
  slot.reclaimRetiredRuntimes();
}

} // namespace

int main(int argc, char **argv) {
  const juce::ScopedJuceInitialiser_GUI juceInitialiser;
  testCompatibilityMetadata();
  testEffectLifecycleStateBypassAndNotification();
  testParameterFingerprintIgnoresVolatilePlaybackState();
  testLayoutRejectionAndDeferredReclamation();
  testMultiOutputInstrumentLayoutIsPreserved();
  if (argc >= 2) {
    const bool instrument =
        argc >= 3 && juce::String(argv[2]) == "--instrument";
    const bool openEditor =
        argc >= 3 && juce::String(argv[2]) == "--with-editor";
    if (instrument)
      testInstalledInstrument(argv[1]);
    else
      testInstalledEffect(argv[1], openEditor);
  }
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
