#pragma once

#include "AudioInsertSnapshot.h"

#include <juce_core/juce_core.h>

namespace fiddle {

class StripAudioCommands {
public:
  virtual ~StripAudioCommands() = default;

  [[nodiscard]] virtual juce::var state(const juce::String &stripId) const = 0;
  virtual bool addInsert(const juce::String &stripId, int pluginUid,
                         StripInsertPosition position) = 0;
  virtual bool removeInsert(const juce::String &stripId,
                            const juce::String &slotId) = 0;
  virtual bool moveInsert(const juce::String &stripId,
                          const juce::String &slotId,
                          StripInsertPosition position, int newIndex) = 0;
  virtual bool setInsertBypassed(const juce::String &stripId,
                                 const juce::String &slotId,
                                 bool bypassed) = 0;
  virtual bool showInsertEditor(const juce::String &stripId,
                                const juce::String &slotId) = 0;
  virtual bool toggleInsertEditor(const juce::String &stripId,
                                  const juce::String &slotId) = 0;
};

} // namespace fiddle
