#pragma once

#include "GroupBusCommands.h"

namespace fiddle {
class MixerModel;
class UndoManager;

class GroupBusCommandService final : public GroupBusCommands {
public:
  GroupBusCommandService(MixerModel &mixer, UndoManager &undoManager);
  bool addGroupBus(const juce::String &name,
                   const std::vector<juce::String> &stripIds) override;
  bool removeGroupBus(const juce::String &busId) override;
  bool renameGroupBus(const juce::String &busId,
                      const juce::String &name) override;
  bool moveGroupBus(const juce::String &busId, int newIndex) override;
  bool setGroupBusGain(const juce::String &busId, float gainDb) override;
  bool setGroupBusMute(const juce::String &busId, bool muted) override;
  bool setGroupBusSolo(const juce::String &busId, bool soloed) override;
  bool setStripDirectOutput(const juce::String &stripId,
                            const juce::String &busId) override;
private:
  MixerModel &mixer_;
  UndoManager &undoManager_;
};
} // namespace fiddle
