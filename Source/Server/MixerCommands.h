#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace fiddle {

/// Message-thread operations exposed by the mixer command boundary.
///
/// The interface keeps JavaScript payload parsing separate from MixerModel and
/// makes the handler adapter testable without constructing the audio engine.
class MixerCommands {
public:
  virtual ~MixerCommands() = default;

  virtual bool addStrip() = 0;
  virtual bool duplicateStrip(const juce::String &stripId) = 0;
  virtual bool removeStrip(const juce::String &stripId) = 0;
  virtual bool setLibrary(const juce::String &stripId,
                          const juce::String &library) = 0;
  virtual bool setInput(const juce::String &stripId, int port, int channel) = 0;
  virtual bool setGain(const juce::String &stripId, float gainDb) = 0;
  virtual bool setMute(const juce::String &stripId, bool muted) = 0;
  virtual bool setSolo(const juce::String &stripId, bool soloed) = 0;
  virtual bool toggleLibraryActive(const juce::String &library) = 0;
  virtual bool setProgram(const juce::String &stripId, int programIndex) = 0;

  virtual bool setGroupGainDelta(const std::vector<juce::String> &stripIds,
                                 float deltaDb) = 0;
  virtual bool setGroupGainAbsolute(const std::vector<juce::String> &stripIds,
                                    float gainDb) = 0;
  virtual bool setGroupLibrary(const std::vector<juce::String> &stripIds,
                               const juce::String &library) = 0;
  virtual bool removeGroupStrips(
      const std::vector<juce::String> &stripIds) = 0;
};

} // namespace fiddle
