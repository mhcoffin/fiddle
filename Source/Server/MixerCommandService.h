#pragma once

#include "MixerCommands.h"

namespace fiddle {

class MixerModel;
class UndoManager;

/// Applies mixer commands and records undoable changes.
class MixerCommandService final : public MixerCommands {
public:
  MixerCommandService(MixerModel &mixer, UndoManager &undoManager);

  bool addStrip() override;
  bool duplicateStrip(const juce::String &stripId) override;
  bool removeStrip(const juce::String &stripId) override;
  bool setLibrary(const juce::String &stripId,
                  const juce::String &library) override;
  bool setInput(const juce::String &stripId, int port, int channel) override;
  bool setGain(const juce::String &stripId, float gainDb) override;
  bool setMute(const juce::String &stripId, bool muted) override;
  bool setSolo(const juce::String &stripId, bool soloed) override;
  bool toggleLibraryActive(const juce::String &library) override;
  bool setProgram(const juce::String &stripId, int programIndex) override;

  bool setGroupGainDelta(const std::vector<juce::String> &stripIds,
                         float deltaDb) override;
  bool setGroupGainAbsolute(const std::vector<juce::String> &stripIds,
                            float gainDb) override;
  bool setGroupLibrary(const std::vector<juce::String> &stripIds,
                       const juce::String &library) override;
  bool removeGroupStrips(
      const std::vector<juce::String> &stripIds) override;

private:
  MixerModel &mixer_;
  UndoManager &undoManager_;
};

} // namespace fiddle
