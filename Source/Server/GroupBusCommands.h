#pragma once

#include "AudioInsertSnapshot.h"

#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

class GroupBusCommands {
public:
  virtual ~GroupBusCommands() = default;
  // Chair routing (docs/chair-buses.md). `chairId` is a chair bus ID, or the
  // ID of one of the chair's layers, which stands for its chair.
  virtual bool addChairSend(const juce::String &chairId, const juce::String &busId) = 0;
  virtual bool updateChairSend(const juce::String &chairId, const juce::String &sendId,
                               const juce::String &busId, float gainDb, bool enabled) = 0;
  virtual bool removeChairSend(const juce::String &chairId, const juce::String &sendId) = 0;
  virtual bool setChairSendVisible(const juce::String &chairId, const juce::String &sendId, bool visible) = 0;
  virtual bool setChairSendPreFader(const juce::String &chairId, const juce::String &sendId, bool preFader) = 0;
  /// Adds an audio bus and routes the given chairs (or layers' chairs) to it.
  virtual bool addGroupBus(const juce::String &name,
                           const std::vector<juce::String> &chairIds) = 0;
  virtual bool removeGroupBus(const juce::String &busId) = 0;
  virtual bool renameGroupBus(const juce::String &busId,
                              const juce::String &name) = 0;
  virtual bool moveGroupBus(const juce::String &busId, int newIndex) = 0;
  virtual bool setGroupBusGain(const juce::String &busId, float gainDb) = 0;
  virtual bool setStripBalance(const juce::String &stripId, float balance) = 0;
  virtual bool setGroupBusBalance(const juce::String &busId, float balance) = 0;
  virtual bool setGroupBusMute(const juce::String &busId, bool muted) = 0;
  virtual bool setGroupBusSolo(const juce::String &busId, bool soloed) = 0;
  virtual bool addGroupBusInsert(const juce::String &busId, int pluginUid,
                                 StripInsertPosition position) = 0;
  virtual bool removeGroupBusInsert(const juce::String &busId,
                                    const juce::String &slotId) = 0;
  virtual bool moveGroupBusInsert(const juce::String &busId,
                                  const juce::String &slotId,
                                  StripInsertPosition position,
                                  int newIndex) = 0;
  virtual bool setGroupBusInsertBypassed(const juce::String &busId,
                                         const juce::String &slotId,
                                         bool bypassed) = 0;
  virtual bool toggleGroupBusInsertEditor(const juce::String &busId,
                                          const juce::String &slotId) = 0;
  virtual bool setChairOutput(const juce::String &chairId,
                              const juce::String &busId) = 0;
  /// One undo step; a chair named more than once changes once.
  virtual bool setChairsOutput(const std::vector<juce::String> &chairIds,
                               const juce::String &busId) = 0;
};

} // namespace fiddle
