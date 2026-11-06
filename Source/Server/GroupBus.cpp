#include "GroupBus.h"

namespace fiddle {

GroupBus::GroupBus() = default;
GroupBus::~GroupBus() = default;

float GroupBus::gainDb() const noexcept {
  return gainDb_.load(std::memory_order_relaxed);
}

void GroupBus::setGainDb(float gain) noexcept {
  gainDb_.store(juce::jlimit(-120.0f, maximumGainDb(), gain),
                std::memory_order_relaxed);
}

bool GroupBus::hasDefaultChairSettings() const {
  return gainDb() == 0.0f && !isMuted() && !isSoloed() &&
         directOutputBusId.isEmpty() && sends.empty() &&
         audioEngine_.balance() == 0.0f &&
         audioEngine_.insertCount(StripInsertPosition::preFader) == 0 &&
         audioEngine_.insertCount(StripInsertPosition::postFader) == 0;
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
  return meter_.peak();
}

float GroupBus::peakHoldDb() const noexcept {
  return meter_.hold();
}

void GroupBus::prepareToPlay(double sampleRate, int blockSize) {
  AudioProcessingGate::Control control;
  sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  blockSize = juce::jmax(1, blockSize);
  sampleRate_.store(sampleRate, std::memory_order_relaxed);
  inputStorage_.setSize(2, blockSize, false, true, false);
  inputStorage_.clear();
  inputBuffer_.setDataToReferTo(inputStorage_.getArrayOfWritePointers(), 2, blockSize);
  audioEngine_.prepareToPlay(sampleRate, blockSize);
}

void GroupBus::beginBlock(int numSamples) noexcept {
  const auto size = juce::jlimit(0, inputStorage_.getNumSamples(), numSamples);
  inputBuffer_.setDataToReferTo(inputStorage_.getArrayOfWritePointers(), 2, size);
  inputBuffer_.clear();
}

bool GroupBus::processInPlace(int numSamples, bool audible,
                              juce::AudioBuffer<float> *preFaderTap) noexcept {
  if (preFaderTap) preFaderTap->clear();
  if (numSamples <= 0 || numSamples > inputBuffer_.getNumSamples())
    return false;
  const float db = gainDb_.load(std::memory_order_relaxed);
  const bool busAudible = audible && !isMuted();
  const bool faderAudible = busAudible && db > -120.0f;
  const float effectiveGain =
      faderAudible ? juce::Decibels::decibelsToGain(db, -120.0f) : 0.0f;
  audioEngine_.processBlock(inputBuffer_, effectiveGain, busAudible ? preFaderTap : nullptr);
  meter_.process(faderAudible ? &inputBuffer_ : nullptr, numSamples, sampleRate_.load());
  return faderAudible;
}

void GroupBus::processTo(juce::AudioBuffer<float> &destination,
                         bool audible, juce::AudioBuffer<float> *printOutput) noexcept {
  if (printOutput) printOutput->clear();
  const int numSamples = destination.getNumSamples();
  if (numSamples <= 0 || numSamples > inputBuffer_.getNumSamples())
    return;

  const float db = gainDb_.load(std::memory_order_relaxed);
  const bool faderAudible = audible && !isMuted() && db > -120.0f;
  const float effectiveGain =
      faderAudible ? juce::Decibels::decibelsToGain(db, -120.0f) : 0.0f;
  audioEngine_.processBlock(inputBuffer_, effectiveGain);

  const int channels = juce::jmin(2, destination.getNumChannels());
  if (faderAudible) {
    for (int channel = 0; channel < channels; ++channel) {
      destination.addFrom(channel, 0, inputBuffer_, channel, 0, numSamples);
      if (printOutput)
        printOutput->copyFrom(channel, 0, inputBuffer_, channel, 0, numSamples);
    }
  }

  meter_.process(faderAudible ? &inputBuffer_ : nullptr, numSamples, sampleRate_.load());
}

juce::var GroupBus::toJson() const {
  auto *object = new juce::DynamicObject();
  object->setProperty("id", id);
  object->setProperty("name", name);
  object->setProperty("role", isChairBus() ? "chair" : "group");
  if (isChairBus()) {
    object->setProperty("chairId", chairKey);
    object->setProperty("directOutputBusId", directOutputBusId);
    object->setProperty("sends", audioSendsToJson(sends));
  }
  object->setProperty("gainDb", static_cast<double>(gainDb()));
  object->setProperty("muted", isMuted());
  object->setProperty("soloed", isSoloed());
  object->setProperty("peakDb", static_cast<double>(peakDb()));
  object->setProperty("peakHoldDb", static_cast<double>(peakHoldDb()));
  object->setProperty("audio", audioEngine_.toJson());
  return juce::var(object);
}

} // namespace fiddle
