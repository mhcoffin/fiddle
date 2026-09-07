#include "../GroupBus.h"

#include <cmath>
#include <iostream>

namespace {

int failures = 0;

void check(bool condition, const char *expression, int line) {
  if (condition)
    return;
  ++failures;
  std::cerr << "FAIL line " << line << ": " << expression << '\n';
}

void checkNear(float actual, float expected, float tolerance,
               const char *expression, int line) {
  if (std::abs(actual - expected) <= tolerance)
    return;
  ++failures;
  std::cerr << "FAIL line " << line << ": " << expression << " (expected "
            << expected << ", got " << actual << ")\n";
}

#define CHECK(expression) check((expression), #expression, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance)                              \
  checkNear((actual), (expected), (tolerance), #actual, __LINE__)

void fill(juce::AudioBuffer<float> &buffer, float value) {
  for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), value,
                                      buffer.getNumSamples());
}

void testSummingGainMuteAndMeter() {
  constexpr int blockSize = 64;
  fiddle::GroupBus bus;
  bus.id = "strings";
  bus.name = "Strings";
  bus.prepareToPlay(48000.0, blockSize);

  juce::AudioBuffer<float> master(2, blockSize);
  master.clear();
  bus.beginBlock(blockSize);
  fill(bus.inputBuffer(), 0.5f);
  bus.processTo(master, true);

  CHECK_NEAR(master.getSample(0, 0), 0.5f, 0.0001f);
  CHECK_NEAR(master.getSample(1, 63), 0.5f, 0.0001f);
  CHECK_NEAR(bus.peakDb(), juce::Decibels::gainToDecibels(0.5f), 0.01f);

  master.clear();
  bus.setGainDb(-6.0f);
  bus.beginBlock(blockSize);
  fill(bus.inputBuffer(), 1.0f);
  bus.processTo(master, true);
  CHECK_NEAR(master.getSample(0, 0),
             juce::Decibels::decibelsToGain(-6.0f), 0.0001f);

  master.clear();
  bus.setMuted(true);
  bus.beginBlock(blockSize);
  fill(bus.inputBuffer(), 1.0f);
  bus.processTo(master, true);
  CHECK_NEAR(master.getMagnitude(0, blockSize), 0.0f, 0.0001f);
}

void testBlockResetAndPublishedState() {
  constexpr int blockSize = 32;
  fiddle::GroupBus bus;
  bus.id = "brass";
  bus.name = "Brass";
  bus.prepareToPlay(44100.0, blockSize);
  fill(bus.inputBuffer(), 0.75f);
  bus.beginBlock(blockSize);
  CHECK_NEAR(bus.inputBuffer().getMagnitude(0, blockSize), 0.0f, 0.0001f);

  bus.setSoloed(true);
  bus.setGainDb(3.0f);
  const auto json = bus.toJson();
  const auto *object = json.getDynamicObject();
  CHECK(object != nullptr);
  CHECK(object->getProperty("id").toString() == "brass");
  CHECK(object->getProperty("name").toString() == "Brass");
  CHECK(static_cast<bool>(object->getProperty("soloed")));
  CHECK_NEAR(static_cast<float>(object->getProperty("gainDb")), 3.0f,
             0.0001f);
}

} // namespace

int main() {
  juce::ScopedJuceInitialiser_GUI juce;
  testSummingGainMuteAndMeter();
  testBlockResetAndPublishedState();
  if (failures == 0) {
    std::cout << "GroupBusTest passed\n";
    return 0;
  }
  std::cerr << failures << " GroupBusTest assertion(s) failed\n";
  return 1;
}
