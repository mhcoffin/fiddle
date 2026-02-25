#include "StateManager.h"
#include <iostream>

namespace fiddle {

StateManager::StateManager() = default;

StateManager::~StateManager() { threadPool_.removeAllJobs(true, 2000); }

void StateManager::initialize() {
  sharedMemory_ = std::make_unique<StateSharedMemory>(true /* producer */);
  std::cerr << "[StateManager] Initialized" << std::endl;
}

void StateManager::markDirty() {
  dirty_.store(true, std::memory_order_release);
}

juce::MemoryBlock StateManager::buildStateBlob(MixerModel &mixer) {
  juce::MemoryBlock blob;

  // Header
  uint32_t magic = kBlobMagic;
  uint32_t version = kBlobVersion;
  uint32_t totalSizePlaceholder = 0;
  blob.append(&magic, 4);
  blob.append(&version, 4);
  blob.append(&totalSizePlaceholder, 4); // will be filled at end

  // Config name
  juce::String cfgName = getConfigName();
  std::string cfgNameUtf8 = cfgName.toStdString();
  uint32_t cfgNameLen = (uint32_t)cfgNameUtf8.size();
  blob.append(&cfgNameLen, 4);
  blob.append(cfgNameUtf8.data(), cfgNameLen);

  // Dirty flag
  uint8_t dirtyFlag = isDirty() ? 1 : 0;
  blob.append(&dirtyFlag, 1);

  // Get all strips
  auto strips = mixer.getAllStrips();
  uint32_t stripCount = (uint32_t)strips.size();
  blob.append(&stripCount, 4);

  for (auto *strip : strips) {
    // Build strip JSON
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", strip->id);
    obj->setProperty("library", strip->library);
    obj->setProperty("family", strip->family);
    obj->setProperty("isSolo", strip->isSolo);
    obj->setProperty("inputPort", strip->inputPort);
    obj->setProperty("inputChannel", strip->inputChannel);
    obj->setProperty("pluginUid", strip->pluginUid);
    obj->setProperty("gainDb",
                     (double)strip->gainDb.load(std::memory_order_relaxed));
    obj->setProperty("expressionMap",
                     strip->expressionMap
                         ? juce::String(strip->expressionMap->entityID)
                         : juce::String());

    juce::String json = juce::JSON::toString(juce::var(obj), true);
    std::string jsonUtf8 = json.toStdString();
    uint32_t jsonLen = (uint32_t)jsonUtf8.size();
    blob.append(&jsonLen, 4);
    blob.append(jsonUtf8.data(), jsonLen);

    // Plugin BLOB
    if (strip->pluginInstance) {
      juce::MemoryBlock pluginState;
      strip->pluginInstance->getStateInformation(pluginState);
      uint32_t blobSize = (uint32_t)pluginState.getSize();
      blob.append(&blobSize, 4);
      if (blobSize > 0)
        blob.append(pluginState.getData(), blobSize);
    } else {
      uint32_t zero = 0;
      blob.append(&zero, 4);
    }
  }

  // Fill in total size (excluding the 12-byte header)
  uint32_t totalSize = (uint32_t)(blob.getSize() - 12);
  std::memcpy(static_cast<uint8_t *>(blob.getData()) + 8, &totalSize, 4);

  std::cerr << "[StateManager] Built blob: " << blob.getSize() << " bytes, "
            << stripCount << " strips" << std::endl;

  return blob;
}

void StateManager::publishBlob(const juce::MemoryBlock &blob) {
  if (sharedMemory_)
    sharedMemory_->pushState(blob);
}

void StateManager::scheduleRebuild(std::function<juce::MemoryBlock()> buildFn) {
  // Coalesce: if a rebuild is already pending, skip
  bool expected = false;
  if (!rebuildPending_.compare_exchange_strong(expected, true))
    return;

  threadPool_.addJob([this, fn = std::move(buildFn)]() {
    auto blob = fn();
    publishBlob(blob);
    rebuildPending_.store(false, std::memory_order_release);
  });
}

// ── Deserialization ──────────────────────────────────────────────────

std::optional<StateManager::RestoredState>
StateManager::deserializeBlob(const void *data, size_t size) {
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

  auto readString = [&](uint32_t len) -> std::string {
    if (offset + len > size)
      return {};
    std::string s((const char *)(bytes + offset), len);
    offset += len;
    return s;
  };

  // Header
  uint32_t magic = readU32();
  uint32_t version = readU32();
  uint32_t totalSize = readU32();
  (void)totalSize;

  if (magic != kBlobMagic || version != kBlobVersion)
    return std::nullopt;

  // Config name
  uint32_t cfgNameLen = readU32();
  std::string cfgName = readString(cfgNameLen);

  // Dirty flag
  uint8_t dirtyFlag = readU8();

  // Strip count
  uint32_t stripCount = readU32();

  RestoredState state;
  state.configName = juce::String(cfgName);
  state.dirty = dirtyFlag != 0;

  for (uint32_t i = 0; i < stripCount && offset < size; ++i) {
    // Strip JSON
    uint32_t jsonLen = readU32();
    std::string jsonStr = readString(jsonLen);

    RestoredStrip strip;
    juce::var parsed = juce::JSON::parse(juce::String(jsonStr));
    if (auto *obj = parsed.getDynamicObject()) {
      strip.id = obj->getProperty("id").toString();
      strip.library = obj->getProperty("library").toString();
      strip.family = obj->getProperty("family").toString();
      strip.isSolo = (bool)obj->getProperty("isSolo");
      strip.inputPort = (int)obj->getProperty("inputPort");
      strip.inputChannel = (int)obj->getProperty("inputChannel");
      strip.pluginUid = (int)obj->getProperty("pluginUid");
      strip.gainDb = (float)(double)obj->getProperty("gainDb");
      strip.expressionMapEntityID =
          obj->getProperty("expressionMap").toString().toStdString();
    }

    // Plugin BLOB
    uint32_t blobSize = readU32();
    if (blobSize > 0 && offset + blobSize <= size) {
      strip.pluginState.append(bytes + offset, blobSize);
      offset += blobSize;
    }

    state.strips.push_back(std::move(strip));
  }

  std::cerr << "[StateManager] Deserialized blob: " << state.strips.size()
            << " strips, config='" << state.configName << "'" << std::endl;

  return state;
}

} // namespace fiddle
