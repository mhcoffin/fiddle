#pragma once

#include "MasterInstrumentList.h"
#include "MixerStrip.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <mutex>
#include <set>
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

  /// Sync mixer strips to match the ensemble instrument list.
  /// Creates new strips for new instruments, removes strips whose
  /// port/channel no longer appears. Preserves existing plugin assignments.
  void syncStripsToInstruments(const MasterInstrumentList &masterList) {
    // Build the expected set of {port, channel} → label from the instrument
    // list, using the same flat-index assignment as getChannelMapAsJson.
    struct Entry {
      int port;
      int channel;
      juce::String label;
      juce::String family;
      bool isSolo;
    };
    std::vector<Entry> expected;
    std::set<std::pair<int, int>> expectedSet;

    // Count totals for numbering decisions
    std::map<juce::String, int> soloTotals, sectionTotals;
    for (const auto &slot : masterList.getSlots()) {
      soloTotals[slot.name] += slot.soloCount;
      sectionTotals[slot.name] += slot.sectionCount;
    }

    std::map<juce::String, int> soloCounters, sectionCounters;
    int flatIndex = 0;
    for (const auto &slot : masterList.getSlots()) {
      for (int i = 0; i < slot.soloCount; ++i) {
        int num = ++soloCounters[slot.name];
        juce::String label = slot.name;
        if (soloTotals[slot.name] > 1)
          label += " " + juce::String(num);

        int port = flatIndex / 16;
        int ch = flatIndex % 16;
        expected.push_back({port, ch, label, slot.family, true});
        expectedSet.insert({port, ch});
        ++flatIndex;
      }
      for (int i = 0; i < slot.sectionCount; ++i) {
        int num = ++sectionCounters[slot.name];
        juce::String label = slot.name;
        if (sectionTotals[slot.name] > 1)
          label += " " + juce::String(num);

        int port = flatIndex / 16;
        int ch = flatIndex % 16;
        expected.push_back({port, ch, label, slot.family, false});
        expectedSet.insert({port, ch});
        ++flatIndex;
      }
    }

    std::lock_guard<std::mutex> lock(stripsMutex);

    // Remove strips whose port/channel is no longer in the expected set
    for (auto it = strips_.begin(); it != strips_.end();) {
      auto key = std::make_pair((*it)->inputPort, (*it)->inputChannel);
      if (expectedSet.find(key) == expectedSet.end()) {
        (*it)->unloadPlugin();
        it = strips_.erase(it);
      } else {
        ++it;
      }
    }
    // Build a lookup from port/channel to expected entry
    std::map<std::pair<int, int>, const Entry *> expectedMap;
    for (const auto &e : expected)
      expectedMap[{e.port, e.channel}] = &e;

    // Update family/name on existing strips and build existing set
    std::set<std::pair<int, int>> existingSet;
    for (auto &s : strips_) {
      auto key = std::make_pair(s->inputPort, s->inputChannel);
      existingSet.insert(key);
      auto it2 = expectedMap.find(key);
      if (it2 != expectedMap.end()) {
        s->family = it2->second->family;
        s->isSolo = it2->second->isSolo;
        // Update name only if it looks auto-generated (not user-renamed)
        if (s->name.startsWith("Strip") || s->name.isEmpty())
          s->name = it2->second->label;
      }
    }

    // Add strips for new instruments
    for (const auto &entry : expected) {
      auto key = std::make_pair(entry.port, entry.channel);
      if (existingSet.find(key) == existingSet.end()) {
        auto strip = std::make_unique<MixerStrip>();
        strip->id = juce::Uuid().toString();
        strip->name = entry.label;
        strip->family = entry.family;
        strip->isSolo = entry.isSolo;
        strip->inputPort = entry.port;
        strip->inputChannel = entry.channel;
        strip->prepareToPlay(currentSampleRate_, currentBlockSize_);
        strips_.push_back(std::move(strip));
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
