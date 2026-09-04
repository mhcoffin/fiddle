#include "../MasterAudioEngine.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                     \
                << "]: " << #condition << std::endl;                           \
    }                                                                          \
  } while (false)

void checkNear(float actual, float expected, int line) {
  if (std::abs(actual - expected) <= 1.0e-5f) {
    ++passed;
    return;
  }
  ++failed;
  std::cerr << "FAIL [" << __FILE__ << ':' << line << "]: expected " << expected
            << ", got " << actual << std::endl;
}

#define CHECK_NEAR(actual, expected) checkNear((actual), (expected), __LINE__)

class ArithmeticEffect final : public juce::AudioProcessor {
public:
  enum class Operation { multiply, add };

  ArithmeticEffect(Operation operation, float amount, int latency = 0)
      : juce::AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
        operation_(operation), amount_(amount) {
    setLatencySamples(latency);
  }

  const juce::String getName() const override { return "Arithmetic Effect"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &audio,
                    juce::MidiBuffer &) override {
    if (operation_ == Operation::multiply)
      audio.applyGain(amount_);
    else
      for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        juce::FloatVectorOperations::add(audio.getWritePointer(channel),
                                         amount_, audio.getNumSamples());
  }
  void processBlock(juce::AudioBuffer<double> &, juce::MidiBuffer &) override {}
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &state) override {
    state.append(&amount_, sizeof(amount_));
  }
  void setStateInformation(const void *data, int size) override {
    if (data && size == static_cast<int>(sizeof(amount_)))
      std::memcpy(&amount_, data, sizeof(amount_));
  }
  bool isBusesLayoutSupported(const BusesLayout &layout) const override {
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
  }

private:
  Operation operation_;
  float amount_;
};

class VolatileStateEffect final : public juce::AudioProcessor {
public:
  VolatileStateEffect()
      : juce::AudioProcessor(
            BusesProperties()
                .withInput("Input", juce::AudioChannelSet::stereo(), true)
                .withOutput("Output", juce::AudioChannelSet::stereo(), true)) {
    amount_ = new juce::AudioParameterFloat(
        {"amount", 1}, "Amount", juce::NormalisableRange<float>(0.0f, 1.0f),
        0.5f);
    addParameter(amount_);
  }

  void setParameterWithoutNotification(float value) {
    static_cast<juce::AudioProcessorParameter *>(amount_)->setValue(value);
  }

  void setParameterWithNotification(float value) {
    amount_->setValueNotifyingHost(value);
  }

  void setParameterWithGesture(float value) {
    amount_->beginChangeGesture();
    setParameterWithNotification(value);
    amount_->endChangeGesture();
  }

  const juce::String getName() const override { return "Volatile State"; }
  void prepareToPlay(double, int) override {}
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override {}
  void processBlock(juce::AudioBuffer<double> &, juce::MidiBuffer &) override {}
  double getTailLengthSeconds() const override { return 0.0; }
  bool acceptsMidi() const override { return false; }
  bool producesMidi() const override { return false; }
  bool isMidiEffect() const override { return false; }
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return {}; }
  void changeProgramName(int, const juce::String &) override {}
  void getStateInformation(juce::MemoryBlock &state) override {
    ++serializationCounter_;
    state.append(&serializationCounter_, sizeof(serializationCounter_));
  }
  void setStateInformation(const void *, int) override {}
  bool isBusesLayoutSupported(const BusesLayout &layout) const override {
    return layout.getMainInputChannelSet() == juce::AudioChannelSet::stereo() &&
           layout.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
  }

private:
  juce::AudioParameterFloat *amount_ = nullptr;
  uint32_t serializationCounter_ = 0;
};

fiddle::MasterInsertSnapshot makeSlot(const char *id, int uid,
                                      const char *name) {
  fiddle::MasterInsertSnapshot slot;
  slot.slotId = id;
  slot.description.name = name;
  slot.description.pluginFormatName = "Test";
  slot.description.uniqueId = uid;
  slot.description.numInputChannels = 2;
  slot.description.numOutputChannels = 2;
  slot.description.isInstrument = false;
  return slot;
}

void fill(juce::AudioBuffer<float> &audio, float value) {
  for (int channel = 0; channel < audio.getNumChannels(); ++channel)
    juce::FloatVectorOperations::fill(audio.getWritePointer(channel), value,
                                      audio.getNumSamples());
}

void testOrderedMasterRack() {
  fiddle::MasterAudioEngine master;
  master.prepareToPlay(48000.0, 64);
  int changes = 0;
  master.setOnChanged([&] { ++changes; });

  CHECK(
      master.insertProcessor(makeSlot("gain", 1, "Gain"), 0,
                             std::make_unique<ArithmeticEffect>(
                                 ArithmeticEffect::Operation::multiply, 2.0f)));
  CHECK(
      master.insertProcessor(makeSlot("offset", 2, "Offset"), 1,
                             std::make_unique<ArithmeticEffect>(
                                 ArithmeticEffect::Operation::add, 1.0f, 12)));
  CHECK(master.insertCount() == 2);
  CHECK(master.latencySamples() == 12);

  juce::AudioBuffer<float> audio(2, 64);
  fill(audio, 1.0f);
  master.processBlock(audio);
  CHECK_NEAR(audio.getSample(0, 0), 3.0f);
  CHECK_NEAR(audio.getSample(1, 0), 3.0f);

  CHECK(master.move("offset", 0));
  fill(audio, 1.0f);
  master.processBlock(audio);
  CHECK_NEAR(audio.getSample(0, 0), 4.0f);

  CHECK(master.setBypassed("offset", true));
  fill(audio, 1.0f);
  master.processBlock(audio);
  CHECK_NEAR(audio.getSample(0, 0), 2.0f);

  master.setGainDb(-6.0206f);
  fill(audio, 1.0f);
  master.processBlock(audio);
  CHECK_NEAR(audio.getSample(0, 0), 1.0f);

  const auto snapshot = master.snapshotAll();
  CHECK(snapshot.inserts.size() == 2);
  CHECK(snapshot.inserts[0].slotId == "offset");
  CHECK(snapshot.inserts[0].bypassed);
  CHECK(snapshot.inserts[0].pluginState.getSize() == sizeof(float));
  CHECK(changes >= 4);

  const auto json = master.toJson();
  CHECK(json.getProperty("id", {}).toString() == "master");
  CHECK(json.getProperty("inserts", {}).getArray()->size() == 2);

  CHECK(master.remove("offset"));
  CHECK(master.insertCount() == 1);
  master.clear();
  CHECK(master.insertCount() == 0);
}

void testVolatileOpaqueStateDoesNotCreateFalseChanges() {
  fiddle::MasterAudioEngine master;
  auto processor = std::make_unique<VolatileStateEffect>();
  auto *processorPointer = processor.get();
  CHECK(master.insertProcessor(makeSlot("volatile", 3, "Volatile"), 0,
                               std::move(processor), false));

  int changes = 0;
  master.setOnChanged([&] { ++changes; });
  CHECK(!master.refreshPluginStateCaches());
  CHECK(!master.refreshPluginStateCaches());
  CHECK(changes == 0);

  processorPointer->setParameterWithoutNotification(0.75f);
  CHECK(master.refreshPluginStateCaches());
  CHECK(changes == 1);
  CHECK(!master.refreshPluginStateCaches());
}

void testPlaybackNotificationsDoNotDirtyMasterEffects() {
  fiddle::MasterAudioEngine master;
  auto processor = std::make_unique<VolatileStateEffect>();
  auto *processorPointer = processor.get();
  CHECK(master.insertProcessor(makeSlot("playback", 4, "Playback"), 0,
                               std::move(processor), false));

  int changes = 0;
  master.setOnChanged([&] { ++changes; });

  processorPointer->setParameterWithNotification(0.6f);
  CHECK(!master.consumePluginChanges(true));
  CHECK(changes == 0);

  processorPointer->setParameterWithGesture(0.7f);
  CHECK(master.consumePluginChanges(true));
  CHECK(changes == 1);

  processorPointer->setParameterWithNotification(0.8f);
  CHECK(master.consumePluginChanges(false));
  CHECK(changes == 2);
}

} // namespace

int main() {
  const juce::ScopedJuceInitialiser_GUI juceInitialiser;
  testOrderedMasterRack();
  testVolatileOpaqueStateDoesNotCreateFalseChanges();
  testPlaybackNotificationsDoNotDirtyMasterEffects();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
