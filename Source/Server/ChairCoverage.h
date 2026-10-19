#pragma once
#include "BridgeReachability.h"

namespace fiddle {

// Semantic requests, not MIDI allocation or a simulation of Dorico selection.
// All inputs are immutable snapshots; safe to call on the catalog worker.
class ChairCoverage {
public:
  struct Limits {
    int statesPerChair = 4096;
    int resolverCalls = 100000; // shared by the entire report
    int witnessesPerChair = 16;
    int witnessBytes = 2 * 1024 * 1024; // shared output budget
    double milliseconds = 5000; // cooperative, checked between resolver calls
  };
  static juce::var analyze(const std::vector<BridgeReachability::Assignment>&,
                           const juce::var& catalog, const Limits&,
                           std::function<bool()> cancelled = {});
};
} // namespace fiddle
