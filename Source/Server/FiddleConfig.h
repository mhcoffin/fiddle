#pragma once

#include "../FiddlePaths.h"

#include <juce_core/juce_core.h>

namespace fiddle {

class FiddleConfig {
public:
  /// Root app data directory: ~/Library/Application Support/Fiddle/
  static juce::File getAppDataDir() {
    auto dir = getUserAppSupportDir().getChildFile("Fiddle");
    if (!dir.exists())
      dir.createDirectory();
    return dir;
  }
};

} // namespace fiddle
