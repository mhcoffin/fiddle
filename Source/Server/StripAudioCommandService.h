#pragma once

#include "StripAudioCommands.h"

namespace fiddle {

class MixerModel;
class PluginScanner;
class UndoManager;

class StripAudioCommandService final : public StripAudioCommands {
public:
  StripAudioCommandService(MixerModel &mixer, PluginScanner &scanner,
                           UndoManager &undoManager);

  [[nodiscard]] juce::var state(const juce::String &stripId) const override;
  bool addInsert(const juce::String &stripId, int pluginUid,
                 StripInsertPosition position) override;
  bool removeInsert(const juce::String &stripId,
                    const juce::String &slotId) override;
  bool moveInsert(const juce::String &stripId, const juce::String &slotId,
                  StripInsertPosition position, int newIndex) override;
  bool setInsertBypassed(const juce::String &stripId,
                         const juce::String &slotId, bool bypassed) override;
  bool showInsertEditor(const juce::String &stripId,
                        const juce::String &slotId) override;
  bool toggleInsertEditor(const juce::String &stripId,
                          const juce::String &slotId) override;

private:
  MixerModel &mixer_;
  PluginScanner &scanner_;
  UndoManager &undoManager_;
};

} // namespace fiddle
