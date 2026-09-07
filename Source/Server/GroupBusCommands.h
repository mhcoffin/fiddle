#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

class GroupBusCommands {
public:
  virtual ~GroupBusCommands() = default;
  virtual bool addGroupBus(const juce::String &name,
                           const std::vector<juce::String> &stripIds) = 0;
  virtual bool removeGroupBus(const juce::String &busId) = 0;
  virtual bool renameGroupBus(const juce::String &busId,
                              const juce::String &name) = 0;
  virtual bool moveGroupBus(const juce::String &busId, int newIndex) = 0;
  virtual bool setGroupBusGain(const juce::String &busId, float gainDb) = 0;
  virtual bool setGroupBusMute(const juce::String &busId, bool muted) = 0;
  virtual bool setGroupBusSolo(const juce::String &busId, bool soloed) = 0;
  virtual bool setStripDirectOutput(const juce::String &stripId,
                                    const juce::String &busId) = 0;
};

} // namespace fiddle
