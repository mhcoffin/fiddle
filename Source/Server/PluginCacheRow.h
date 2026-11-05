#pragma once

#include <juce_core/juce_core.h>

namespace fiddle {

/// Cached plugin metadata from scanning (one row of the plugin_cache table).
/// modTime is the bundle's modification time when it was last described; a
/// different time on disk means the bundle changed and must be described again.
struct PluginCacheRow {
  juce::String path;
  int64_t modTime = 0;
  juce::String name, manufacturer, category, format;
  int uid = 0;
  int numInputs = 0, numOutputs = 0;
  bool isInstrument = false;
  bool valid = true;
};

} // namespace fiddle
