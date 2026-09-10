#pragma once
#include "ProjectSettings.h"

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fiddle {

inline constexpr uint32_t kStateBlobMagic = 0x46444C53; // "FDLS"
// Strip insert state lives inside the existing per-strip JSON envelope, so it
// is an additive v4 field rather than a binary-layout change. Keeping v4 lets
// an already-installed Fiddle plug-in continue to read blobs from a newly
// built server.
inline constexpr uint32_t kStateBlobVersion = 4;

struct RestoredMasterInsertState {
  juce::String slotId;
  juce::String formatName;
  int pluginUid = 0;
  juce::String fileOrIdentifier;
  juce::String manufacturer;
  juce::String name;
  juce::String category;
  juce::String pluginVersion;
  int numInputChannels = 0;
  int numOutputChannels = 0;
  bool bypassed = false;
  juce::MemoryBlock pluginState;
};

struct RestoredStripState {
  juce::String id, library, family;
  bool isSolo = true;
  bool active = true;
  bool muted = false;
  bool soloed = false;
  int inputPort = -1, inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityID;
  juce::MemoryBlock pluginState;
  juce::MemoryBlock audioInsertState;
  std::vector<std::string> luaPluginFileNames;
};

struct RestoredProjectState {
  ProjectSettings projectSettings;
  juce::String configName;
  juce::String configVersion;
  bool dirty = false;
  std::string stateHash;
  std::vector<std::string> ancestorHashes;
  int audioSchemaVersion = 1;
  float masterGainDb = 0.0f;
  std::vector<RestoredMasterInsertState> masterInserts;
  std::vector<RestoredStripState> strips;
};

std::optional<RestoredProjectState> deserializeStateBlob(const void *data,
                                                         size_t size);

} // namespace fiddle
