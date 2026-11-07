#pragma once

#include "GroupBusCommands.h"

#include <functional>

namespace fiddle {
class MixerModel;
class PluginScanner;
class UndoManager;

class GroupBusCommandService final : public GroupBusCommands {
public:
  GroupBusCommandService(MixerModel &mixer, PluginScanner &scanner,
                         UndoManager &undoManager);
  bool addChairSend(const juce::String &chairId, const juce::String &busId) override;
  bool updateChairSend(const juce::String &chairId, const juce::String &sendId,
                       const juce::String &busId, float gainDb, bool enabled) override;
  bool removeChairSend(const juce::String &chairId, const juce::String &sendId) override;
  bool setChairSendVisible(const juce::String &chairId, const juce::String &sendId, bool visible) override;
  bool setChairSendPreFader(const juce::String &chairId, const juce::String &sendId, bool preFader) override;
  bool addGroupBus(const juce::String &name,
                   const std::vector<juce::String> &chairIds) override;
  bool removeGroupBus(const juce::String &busId) override;
  bool renameGroupBus(const juce::String &busId,
                      const juce::String &name) override;
  bool moveGroupBus(const juce::String &busId, int newIndex) override;
  bool setGroupBusGain(const juce::String &busId, float gainDb) override;
  bool setStripBalance(const juce::String &stripId, float balance) override;
  bool setGroupBusBalance(const juce::String &busId, float balance) override;
  bool setGroupBusMute(const juce::String &busId, bool muted) override;
  bool setGroupBusSolo(const juce::String &busId, bool soloed) override;
  bool addGroupBusInsert(const juce::String &busId, int pluginUid,
                         StripInsertPosition position) override;
  bool removeGroupBusInsert(const juce::String &busId,
                            const juce::String &slotId) override;
  bool moveGroupBusInsert(const juce::String &busId,
                          const juce::String &slotId,
                          StripInsertPosition position,
                          int newIndex) override;
  bool setGroupBusInsertBypassed(const juce::String &busId,
                                 const juce::String &slotId,
                                 bool bypassed) override;
  bool toggleGroupBusInsertEditor(const juce::String &busId,
                                  const juce::String &slotId) override;
  bool setChairOutput(const juce::String &chairId,
                      const juce::String &busId) override;
  bool setChairsOutput(const std::vector<juce::String> &chairIds,
                       const juce::String &busId) override;
  /// Names a chair (by its chair ID) for plug-in editor window titles.
  std::function<juce::String(const juce::String &chairId)> chairName;
private:
  MixerModel &mixer_;
  PluginScanner &scanner_;
  UndoManager &undoManager_;
};
} // namespace fiddle
