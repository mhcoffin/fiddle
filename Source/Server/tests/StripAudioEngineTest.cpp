#include "../StripAudioEngine.h"

#include <cmath>
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

} // namespace

int main() {
  const juce::ScopedJuceInitialiser_GUI juceInitialiser;
  std::cout << "===== Strip Audio Engine Tests =====\n";
  testGainRunsThroughIndependentGraphs();
  testZeroGainSilencesOnlyItsOwnPath();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
