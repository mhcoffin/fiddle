#include "../StripAudioEngine.h"

#include <cmath>
#include <cstring>
#include <iostream>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition) {                                                           \
      ++passed;                                                                \
    } else {                                                                   \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                    \
                << "]: " #condition << std::endl;                             \
    }                                                                          \
  } while (false)

bool near(float actual, float expected, float tolerance = 0.0001f) {
  return std::abs(actual - expected) <= tolerance;
}

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

fiddle::AudioInsertSnapshot makeSlot(const char *id, int uid,
                                     const char *name) {
  fiddle::AudioInsertSnapshot slot;
  slot.slotId = id;
  slot.description.name = name;
  slot.description.pluginFormatName = "Test";
  slot.description.uniqueId = uid;
  slot.description.numInputChannels = 2;
  slot.description.numOutputChannels = 2;
  slot.description.isInstrument = false;
  return slot;
}

void fillStereo(juce::AudioBuffer<float> &buffer, float left, float right) {
  for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
    buffer.setSample(0, sample, left);
    buffer.setSample(1, sample, right);
  }
}

void testGainRunsThroughIndependentGraphs() {
  fiddle::StripAudioEngine first;
  fiddle::StripAudioEngine second;
  first.prepareToPlay(48000.0, 64);
  second.prepareToPlay(48000.0, 64);

  juce::AudioBuffer<float> firstAudio(2, 64);
  juce::AudioBuffer<float> secondAudio(2, 64);
  fillStereo(firstAudio, 1.0f, 0.5f);
  fillStereo(secondAudio, 0.25f, -0.5f);

  first.processBlock(firstAudio, 0.5f);
  second.processBlock(secondAudio, 0.25f);

  CHECK(near(firstAudio.getSample(0, 10), 0.5f));
  CHECK(near(firstAudio.getSample(1, 10), 0.25f));
  CHECK(near(secondAudio.getSample(0, 10), 0.0625f));
  CHECK(near(secondAudio.getSample(1, 10), -0.125f));
  CHECK(first.latencySamples() == 0);
  CHECK(second.latencySamples() == 0);
  CHECK(near(static_cast<float>(first.latencyMs()), 0.0f));
}

void testZeroGainSilencesOnlyItsOwnPath() {
  fiddle::StripAudioEngine muted;
  fiddle::StripAudioEngine audible;
  muted.prepareToPlay(44100.0, 32);
  audible.prepareToPlay(44100.0, 32);

  juce::AudioBuffer<float> mutedAudio(2, 32);
  juce::AudioBuffer<float> audibleAudio(2, 32);
  fillStereo(mutedAudio, 0.75f, 0.75f);
  fillStereo(audibleAudio, 0.25f, 0.5f);

  muted.processBlock(mutedAudio, 0.0f);
  audible.processBlock(audibleAudio, 1.0f);

  CHECK(near(mutedAudio.getMagnitude(0, 0, 32), 0.0f));
  CHECK(near(mutedAudio.getMagnitude(1, 0, 32), 0.0f));
  CHECK(near(audibleAudio.getSample(0, 5), 0.25f));
  CHECK(near(audibleAudio.getSample(1, 5), 0.5f));
}

void testPreAndPostFaderOrderingAndBypass() {
  fiddle::StripAudioEngine engine;
  engine.prepareToPlay(48000.0, 32);
  int changes = 0;
  engine.setOnChanged([&] { ++changes; });

  CHECK(engine.insertProcessor(
      makeSlot("multiply", 1, "Multiply"),
      fiddle::StripInsertPosition::preFader, 0,
      std::make_unique<ArithmeticEffect>(
          ArithmeticEffect::Operation::multiply, 2.0f)));
  CHECK(engine.insertProcessor(
      makeSlot("add", 2, "Add"), fiddle::StripInsertPosition::postFader, 0,
      std::make_unique<ArithmeticEffect>(ArithmeticEffect::Operation::add,
                                         1.0f, 12)));

  juce::AudioBuffer<float> audio(2, 32);
  fillStereo(audio, 1.0f, 1.0f);
  engine.processBlock(audio, 0.5f);
  CHECK(near(audio.getSample(0, 20), 2.0f)); // ((1 * 2) * .5) + 1
  CHECK(engine.latencySamples() == 12);
  CHECK(engine.insertCount(fiddle::StripInsertPosition::preFader) == 1);
  CHECK(engine.insertCount(fiddle::StripInsertPosition::postFader) == 1);

  CHECK(engine.setBypassed("add", true));
  fillStereo(audio, 1.0f, 1.0f);
  engine.processBlock(audio, 0.5f);
  CHECK(near(audio.getSample(0, 20), 1.0f));

  CHECK(engine.move("add", fiddle::StripInsertPosition::preFader, 0));
  CHECK(engine.indexOf("add", fiddle::StripInsertPosition::preFader) == 0);
  CHECK(changes >= 4);
}

void testSnapshotSerializationRoundTrip() {
  fiddle::StripAudioSnapshot original;
  auto pre = makeSlot("pre", 11, "Pre Effect");
  pre.bypassed = true;
  const uint8_t bytes[] = {1, 2, 3, 4};
  pre.pluginState.append(bytes, sizeof(bytes));
  original.preFaderInserts.push_back(pre);
  original.postFaderInserts.push_back(makeSlot("post", 12, "Post Effect"));

  const auto encoded = fiddle::serializeStripAudioSnapshot(original);
  const auto restored = fiddle::deserializeStripAudioSnapshot(
      encoded.getData(), encoded.getSize());
  CHECK(restored.preFaderInserts.size() == 1);
  CHECK(restored.postFaderInserts.size() == 1);
  CHECK(restored.preFaderInserts[0].slotId == "pre");
  CHECK(restored.preFaderInserts[0].bypassed);
  CHECK(restored.preFaderInserts[0].pluginState.getSize() == sizeof(bytes));
  CHECK(restored.postFaderInserts[0].description.name == "Post Effect");
}

} // namespace

int main() {
  const juce::ScopedJuceInitialiser_GUI juceInitialiser;
  std::cout << "===== Strip Audio Engine Tests =====\n";
  testGainRunsThroughIndependentGraphs();
  testZeroGainSilencesOnlyItsOwnPath();
  testPreAndPostFaderOrderingAndBypass();
  testSnapshotSerializationRoundTrip();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
