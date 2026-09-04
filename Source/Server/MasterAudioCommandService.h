#pragma once

#include "MasterAudioCommands.h"

namespace fiddle {

class MixerModel;
class PluginScanner;
class UndoManager;

class MasterAudioCommandService final : public MasterAudioCommands {
public:
  MasterAudioCommandService(MixerModel &mixer, PluginScanner &scanner,
                            UndoManager &undoManager);

  [[nodiscard]] juce::var state() const override;
  bool addInsert(int pluginUid) override;
  bool removeInsert(const juce::String &slotId) override;
  bool moveInsert(const juce::String &slotId, int newIndex) override;
  bool setInsertBypassed(const juce::String &slotId, bool bypassed) override;
  bool showInsertEditor(const juce::String &slotId) override;
  bool setGainDb(float gainDb) override;

private:
  MixerModel &mixer_;
  PluginScanner &scanner_;
  UndoManager &undoManager_;
};

} // namespace fiddle
