#pragma once
#include "BridgeReachability.h"

namespace fiddle {
// Pure, versioned proposal. Never changes the active map/decoder or writes a
// Dorico library. Export is a review document, NOT an installable profile.
class BridgeRepairPlan {
public:
  struct Options {
    bool preserveIdentities = true;
    std::set<std::string> additions;
  };
  static juce::var build(const std::vector<ExpressionMap::Dimension> &decoder,
                        const std::set<int> &reservedControllers,
                        const std::vector<BridgeReachability::Assignment> &assignments,
                        const juce::var &catalog, const Options &options,
                        std::function<bool()> cancelled = {});
};
} // namespace fiddle
