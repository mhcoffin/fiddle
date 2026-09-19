#pragma once

#include "RouteDelay.h"
#include <memory>
#include <vector>

namespace fiddle {

// Allocated when arming, touched only by the single render coordinator. Each
// bus is tapped after its rack/fader and delayed by Master latency so its file
// shares the final-output presentation timeline without Master processing.
struct BusPrintCapture {
  struct Bus {
    juce::String id;
    juce::AudioBuffer<float> audio;
    RouteDelay delay;
    bool found = false;
    Bus(juce::String busId, int blockSize, int masterLatency)
        : id(std::move(busId)), audio(2, blockSize), delay(masterLatency) {}
  };
  std::vector<std::unique_ptr<Bus>> buses;
  int masterLatency = 0;
  bool latencyChanged = false;

  void clear() noexcept {
    latencyChanged = false;
    for (auto &bus : buses) { bus->audio.clear(); bus->found = false; }
  }
  juce::AudioBuffer<float> *targetFor(const juce::String &id) noexcept {
    for (auto &bus : buses)
      if (bus->id == id) { bus->found = true; return &bus->audio; }
    return nullptr;
  }
};

} // namespace fiddle
