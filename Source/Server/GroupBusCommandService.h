#pragma once

#include "GroupBusCommands.h"

namespace fiddle {
class MixerModel;
class PluginScanner;
class UndoManager;

class GroupBusCommandService final : public GroupBusCommands {
public:
  GroupBusCommandService(MixerModel &mixer, PluginScanner &scanner,
                         UndoManager &undoManager);
  bool addGroupBus(const juce::String &name,
                   const std::vector<juce::String> &stripIds) override;
  bool removeGroupBus(const juce::String &busId) override;
  bool renameGroupBus(const juce::String &busId,
                      const juce::String &name) override;
  bool moveGroupBus(const juce::String &busId, int newIndex) override;
  bool setGroupBusGain(const juce::String &busId, float gainDb) override;
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
  bool setStripDirectOutput(const juce::String &stripId,
                            const juce::String &busId) override;
  bool setGroupStripDirectOutput(const std::vector<juce::String> &stripIds,
                                 const juce::String &busId) override;
private:
  MixerModel &mixer_;
  PluginScanner &scanner_;
  UndoManager &undoManager_;
};
} // namespace fiddle
