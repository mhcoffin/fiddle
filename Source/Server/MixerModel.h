#pragma once

#include "MixerStrip.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <mutex>
#include <vector>

namespace fiddle {

/// Manages an ordered list of MixerStrips. Owns a shared
/// AudioPluginFormatManager for plugin instantiation.
class MixerModel {
public:
  MixerModel() { formatManager_.addFormat(new juce::VST3PluginFormat()); }

  ~MixerModel() { clear(); }

  void clear() {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      strip->unloadPlugin();
    }
    strips_.clear();
  }

  /// Add a new empty strip. Returns its ID.
  juce::String addStrip() {
    auto strip = std::make_unique<MixerStrip>();
    strip->id = juce::Uuid().toString();
    strip->name = "Strip " + juce::String(nextStripNumber_++);

    std::lock_guard<std::mutex> lock(stripsMutex);
    strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
    strips_.push_back(std::move(strip));
    return strips_.back()->id;
  }

  /// Remove a strip by ID.
  bool removeStrip(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto it = strips_.begin(); it != strips_.end(); ++it) {
      if ((*it)->id == id) {
        (*it)->unloadPlugin();
        strips_.erase(it);
        return true;
      }
    }
    return false;
  }

  /// Find a strip by ID (nullptr if not found).
  MixerStrip *getStrip(const juce::String &id) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &s : strips_) {
      if (s->id == id)
        return s.get();
    }
    return nullptr;
  }

  /// Get the shared format manager (for plugin loading).
  juce::AudioPluginFormatManager &getFormatManager() { return formatManager_; }

  /// Get all strips count.
  int size() const {
    std::lock_guard<std::mutex> lock(stripsMutex);
    return static_cast<int>(strips_.size());
  }

  /// Serialize all strips to JSON array.
  juce::String toJson() const {
    juce::Array<juce::var> arr;
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (const auto &s : strips_)
      arr.add(s->toJson());
    return juce::JSON::toString(juce::var(arr), true);
  }

  /// Process the audio block for all strips
  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      strip->processBlock(audioBuffer, currentTime);
    }
  }

  void prepareToPlay(double sampleRate, int blockSize) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    currentSampleRate_ = sampleRate;
    currentBlockSize_ = blockSize;
    for (auto &strip : strips_) {
      strip->prepareToPlay(sampleRate, blockSize);
    }
  }

  /// Route incoming MIDI note event to matching strips
  void routeNoteEvent(int port, int channel, const juce::MidiMessage &msg,
                      double triggerTime) {
    std::lock_guard<std::mutex> lock(stripsMutex);
    for (auto &strip : strips_) {
      if (strip->inputPort == port && strip->inputChannel == channel) {
        strip->addDelayedMessage(triggerTime, msg);
      }
    }
  }

private:
  mutable std::mutex stripsMutex;
  std::vector<std::unique_ptr<MixerStrip>> strips_;
  juce::AudioPluginFormatManager formatManager_;
  int nextStripNumber_ = 1;
  double currentSampleRate_ = 44100.0;
  int currentBlockSize_ = 512;
  int playbackDelayMs_ = 1000;

public:
  int getPlaybackDelayMs() const { return playbackDelayMs_; }
  void setPlaybackDelayMs(int ms) { playbackDelayMs_ = ms; }
};

} // namespace fiddle
