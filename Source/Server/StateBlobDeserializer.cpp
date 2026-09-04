#include "StateBlobTypes.h"

#include <cstring>

namespace fiddle {

std::optional<RestoredProjectState> deserializeStateBlob(const void *data,
                                                         size_t size) {
  if (size < 12)
    return std::nullopt;

  auto *bytes = static_cast<const uint8_t *>(data);
  size_t offset = 0;

  auto readU32 = [&]() -> uint32_t {
    if (offset + 4 > size)
      return 0;
    uint32_t val;
    std::memcpy(&val, bytes + offset, 4);
    offset += 4;
    return val;
  };

  auto readU8 = [&]() -> uint8_t {
    if (offset + 1 > size)
      return 0;
    return bytes[offset++];
  };

  auto readFloat = [&]() -> float {
    if (offset + sizeof(float) > size)
      return 0.0f;
    float value = 0.0f;
    std::memcpy(&value, bytes + offset, sizeof(value));
    offset += sizeof(value);
    return value;
  };

  auto readString = [&](uint32_t len) -> std::string {
    if (offset + len > size)
      return {};
    std::string s((const char *)(bytes + offset), len);
    offset += len;
    return s;
  };

  uint32_t magic = readU32();
  uint32_t version = readU32();
  uint32_t totalSize = readU32();
  (void)totalSize;

  if (magic != kStateBlobMagic || version < 1 || version > kStateBlobVersion)
    return std::nullopt;

  uint32_t cfgNameLen = readU32();
  std::string cfgName = readString(cfgNameLen);

  std::string cfgVersion;
  if (version >= 2) {
    uint32_t cfgVerLen = readU32();
    cfgVersion = readString(cfgVerLen);
  }

  uint8_t dirtyFlag = readU8();

  RestoredProjectState state;

  if (version >= 4) {
    state.audioSchemaVersion = static_cast<int>(readU32());
    state.masterGainDb = readFloat();
    const uint32_t insertCount = readU32();
    if (insertCount > 1024)
      return std::nullopt;
    for (uint32_t index = 0; index < insertCount; ++index) {
      const uint32_t jsonLength = readU32();
      const auto json = readString(jsonLength);
      const auto parsed = juce::JSON::parse(juce::String(json));
      auto *object = parsed.getDynamicObject();
      if (!object)
        return std::nullopt;
      RestoredMasterInsertState insert;
      insert.slotId = object->getProperty("slotId").toString();
      insert.formatName = object->getProperty("formatName").toString();
      insert.pluginUid = static_cast<int>(object->getProperty("pluginUid"));
      insert.fileOrIdentifier =
          object->getProperty("fileOrIdentifier").toString();
      insert.manufacturer = object->getProperty("manufacturer").toString();
      insert.name = object->getProperty("name").toString();
      insert.category = object->getProperty("category").toString();
      insert.pluginVersion =
          object->getProperty("pluginVersion").toString();
      insert.numInputChannels =
          static_cast<int>(object->getProperty("numInputChannels"));
      insert.numOutputChannels =
          static_cast<int>(object->getProperty("numOutputChannels"));
      insert.bypassed = static_cast<bool>(object->getProperty("bypassed"));
      const uint32_t stateSize = readU32();
      if (offset + stateSize > size)
        return std::nullopt;
      if (stateSize > 0) {
        insert.pluginState.append(bytes + offset, stateSize);
        offset += stateSize;
      }
      state.masterInserts.push_back(std::move(insert));
    }
  }

  if (version >= 3) {
    uint32_t shLen = readU32();
    state.stateHash = readString(shLen);

    uint32_t ancestorCount = readU32();
    for (uint32_t i = 0; i < ancestorCount; ++i) {
      uint32_t hLen = readU32();
      state.ancestorHashes.push_back(readString(hLen));
    }
  }

  uint32_t stripCount = readU32();

  state.configName = juce::String(cfgName);
  state.configVersion = juce::String(cfgVersion);
  state.dirty = dirtyFlag != 0;

  for (uint32_t i = 0; i < stripCount && offset < size; ++i) {
    uint32_t jsonLen = readU32();
    std::string jsonStr = readString(jsonLen);

    RestoredStripState strip;
    juce::var parsed = juce::JSON::parse(juce::String(jsonStr));
    if (auto *obj = parsed.getDynamicObject()) {
      strip.id = obj->getProperty("id").toString();
      strip.library = obj->getProperty("library").toString();
      strip.family = obj->getProperty("family").toString();
      strip.isSolo = (bool)obj->getProperty("isSolo");
      if (obj->hasProperty("active"))
        strip.active = (bool)obj->getProperty("active");
      if (obj->hasProperty("muted"))
        strip.muted = (bool)obj->getProperty("muted");
      if (obj->hasProperty("soloed"))
        strip.soloed = (bool)obj->getProperty("soloed");
      strip.inputPort = (int)obj->getProperty("inputPort");
      strip.inputChannel = (int)obj->getProperty("inputChannel");
      strip.pluginUid = (int)obj->getProperty("pluginUid");
      strip.gainDb = (float)(double)obj->getProperty("gainDb");
      strip.expressionMapEntityID =
          obj->getProperty("expressionMap").toString().toStdString();

      auto luaVar = obj->getProperty("luaPlugins");
      if (auto *luaArr = luaVar.getArray()) {
        for (const auto &item : *luaArr)
          strip.luaPluginFileNames.push_back(item.toString().toStdString());
      }
    }

    uint32_t blobSize = readU32();
    if (blobSize > 0 && offset + blobSize <= size) {
      strip.pluginState.append(bytes + offset, blobSize);
      offset += blobSize;
    }

    state.strips.push_back(std::move(strip));
  }

  return state;
}

} // namespace fiddle
