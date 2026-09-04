#pragma once

#include <juce_core/juce_core.h>

namespace fiddle {

/// Typed command boundary for the permanent Master audio stage.
class MasterAudioCommands {
public:
  virtual ~MasterAudioCommands() = default;

  [[nodiscard]] virtual juce::var state() const = 0;
  virtual bool addInsert(int pluginUid) = 0;
  virtual bool removeInsert(const juce::String &slotId) = 0;
  virtual bool moveInsert(const juce::String &slotId, int newIndex) = 0;
  virtual bool setInsertBypassed(const juce::String &slotId, bool bypassed) = 0;
  virtual bool showInsertEditor(const juce::String &slotId) = 0;
  virtual bool setGainDb(float gainDb) = 0;
};

} // namespace fiddle
