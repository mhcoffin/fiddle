#include "GroupBus.h"

namespace fiddle {

GroupBus::GroupBus() = default;
GroupBus::~GroupBus() = default;

float GroupBus::gainDb() const noexcept {
  return gainDb_.load(std::memory_order_relaxed);
}

void GroupBus::setGainDb(float gain) noexcept {
  gainDb_.store(juce::jlimit(-120.0f, 12.0f, gain),
                std::memory_order_relaxed);
}

bool GroupBus::isMuted() const noexcept {
  return muted_.load(std::memory_order_relaxed);
}

void GroupBus::setMuted(bool muted) noexcept {
  muted_.store(muted, std::memory_order_relaxed);
}

bool GroupBus::isSoloed() const noexcept {
  return soloed_.load(std::memory_order_relaxed);
}

void GroupBus::setSoloed(bool soloed) noexcept {
  soloed_.store(soloed, std::memory_order_relaxed);
}

float GroupBus::peakDb() const noexcept {
  return peakDb_.load(std::memory_order_relaxed);
}

float GroupBus::peakHoldDb() const noexcept {
  return peakHoldDb_.load(std::memory_order_relaxed);
}

void GroupBus::prepareToPlay(double sampleRate, int blockSize) {
  sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  blockSize = juce::jmax(1, blockSize);
  sampleRate_.store(sampleRate, std::memory_order_relaxed);
  inputBuffer_.setSize(2, blockSize, false, true, false);
  inputBuffer_.clear();
  audioEngine_.prepareToPlay(sampleRate, blockSize);
}

void GroupBus::beginBlock(int numSamples) noexcept {
  jassert(numSamples <= inputBuffer_.getNumSamples());
  if (numSamples <= 0 || numSamples > inputBuffer_.getNumSamples())
    return;
  inputBuffer_.clear(0, numSamples);
}

void GroupBus::processTo(juce::AudioBuffer<float> &destination,
                         bool audible) noexcept {
  const int numSamples = destination.getNumSamples();
  if (numSamples <= 0 || numSamples > inputBuffer_.getNumSamples())
    return;

  const float db = gainDb_.load(std::memory_order_relaxed);
  const bool faderAudible = audible && !isMuted() && db > -120.0f;
  const float effectiveGain =
      faderAudible ? juce::Decibels::decibelsToGain(db, -120.0f) : 0.0f;
  audioEngine_.processBlock(inputBuffer_, effectiveGain);

  const int channels = juce::jmin(2, destination.getNumChannels());
  float blockPeak = 0.0f;
  if (faderAudible) {
    for (int channel = 0; channel < channels; ++channel) {
      destination.addFrom(channel, 0, inputBuffer_, channel, 0, numSamples);
      blockPeak = juce::jmax(
          blockPeak, inputBuffer_.getMagnitude(channel, 0, numSamples));
    }
  }

  const float rate = static_cast<float>(
      sampleRate_.load(std::memory_order_relaxed));
  const float peakDecay = rate > 0.0f ? 20.0f * numSamples / rate : 0.0f;
  const float holdDecay = rate > 0.0f ? 3.0f * numSamples / rate : 0.0f;
  const float blockDb = faderAudible
                            ? juce::Decibels::gainToDecibels(blockPeak, -120.0f)
                            : -120.0f;
  const float previousPeak = peakDb_.load(std::memory_order_relaxed);
  const float previousHold = peakHoldDb_.load(std::memory_order_relaxed);
  peakDb_.store(juce::jmax(blockDb, previousPeak - peakDecay),
                std::memory_order_relaxed);
  peakHoldDb_.store(juce::jmax(blockDb, previousHold - holdDecay),
                    std::memory_order_relaxed);
}

juce::var GroupBus::toJson() const {
  auto *object = new juce::DynamicObject();
  object->setProperty("id", id);
  object->setProperty("name", name);
  object->setProperty("role", "group");
  object->setProperty("gainDb", static_cast<double>(gainDb()));
  object->setProperty("muted", isMuted());
  object->setProperty("soloed", isSoloed());
  object->setProperty("peakDb", static_cast<double>(peakDb()));
  object->setProperty("peakHoldDb", static_cast<double>(peakHoldDb()));
  object->setProperty("audio", audioEngine_.toJson());
  return juce::var(object);
}

} // namespace fiddle
