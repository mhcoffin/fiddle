#pragma once
#include "ExpressionMap.h"
#include "ExpressionMapData.h"
#include <functional>

namespace fiddle {

// Immutable message-thread snapshots only. The worker never touches a strip,
// plug-in or repository. One bridge profile belongs to the entire chair;
// downstream instrument maps are intentionally still per-layer.
class BridgeReachability {
public:
  struct Assignment {
    juce::String chairId, chairName, layerId, layerName;
    ExpressionMapData map;
    bool assigned = true;
  };
  static juce::var audit(const std::vector<ExpressionMap::Dimension> &decoder,
                        const std::vector<Assignment> &assignments,
                        std::function<bool()> cancelled = {},
                        bool canonicalPreview = false);
};
} // namespace fiddle
