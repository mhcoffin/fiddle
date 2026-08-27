#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace fiddle {

inline constexpr uint32_t kStateBlobMagic = 0x46444C53; // "FDLS"
inline constexpr uint32_t kStateBlobVersion = 3;

struct RestoredStripState {
  juce::String id, library, family;
  bool isSolo = true;
  bool active = true;
  int inputPort = -1, inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityID;
  juce::MemoryBlock pluginState;
  std::vector<std::string> luaPluginFileNames;
};

struct RestoredProjectState {
  juce::String configName;
  juce::String configVersion;
  bool dirty = false;
  std::string stateHash;
  std::vector<std::string> ancestorHashes;
  std::vector<RestoredStripState> strips;
};

std::optional<RestoredProjectState> deserializeStateBlob(const void *data,
                                                         size_t size);

} // namespace fiddle
