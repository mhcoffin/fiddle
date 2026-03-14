#include "StateManager.h"
#include <iostream>

namespace fiddle {

StateManager::StateManager() = default;

StateManager::~StateManager() = default;

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
  uint32_t version = kBlobVersion; // kBlobVersion is now 2
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

  // Config version (new in blob v2)
  juce::String cfgVer = getConfigVersion();
  std::string cfgVerUtf8 = cfgVer.toStdString();
  uint32_t cfgVerLen = (uint32_t)cfgVerUtf8.size();
  blob.append(&cfgVerLen, 4);
  blob.append(cfgVerUtf8.data(), cfgVerLen);

  // Dirty flag
  uint8_t dirtyFlag = isDirty() ? 1 : 0;
  blob.append(&dirtyFlag, 1);

  // Generate State properties
  std::vector<versioning::StripBlob> stripBlobs;
  versioning::FiddleState state;
  state.globalState.masterGainDb = 0.0f; // TODO: implement master gain

  auto strips = mixer.getAllStrips();

  // Hash items and prep for writing into store.
  // Strip identity = (inputPort, inputChannel, libraryId).
  // libraryId defaults to kDefaultLibraryId until MixerStrip carries it.
  for (auto *strip : strips) {
    versioning::StripBlob sb;
    sb.libraryId = versioning::kDefaultLibraryId;
    sb.library = strip->library.toStdString();
    sb.family = strip->family.toStdString();
    sb.isSolo = strip->isSolo;
    sb.inputPort = strip->inputPort;
    sb.inputChannel = strip->inputChannel;
    sb.pluginUid = strip->pluginUid;
    sb.gainDb = strip->gainDb.load(std::memory_order_relaxed);
    sb.expressionMapEntityId =
        strip->expressionMap ? strip->expressionMap->entityID : "";

    const auto &cached = strip->cachedPluginState_;
    if (cached.getSize() > 0) {
      const uint8_t *data = static_cast<const uint8_t *>(cached.getData());
      sb.pluginState.assign(data, data + cached.getSize());
    }

    stripBlobs.push_back(sb);
    state.stripHashes.push_back(sb.computeHash());
  }

  std::string stateHash = state.computeHash();
  std::vector<std::string> ancestorVersionIds;

  if (versionStore_) {
    // Find branch head using the current branch UUID (not the config name).
    std::string branchId = getCurrentBranchId();
    if (!branchId.empty()) {
      auto head = versionStore_->getBranchHead(branchId);
      if (head) {
        ancestorVersionIds = versionStore_->getAncestorChain(*head);
      }
    }
    // Insert new uncommitted state into DB so it persists for immediate
    // restoration
    versionStore_->getStorage().putFiddleState(stateHash, state);
    for (const auto &sb : stripBlobs) {
      versionStore_->getStorage().putStripBlob(sb.computeHash(), sb);
    }
  }

  // --- V3 Extensions ---
  // State Hash (32 chars)
  uint32_t shLen = (uint32_t)stateHash.size();
  blob.append(&shLen, 4);
  blob.append(stateHash.data(), shLen);

  // Ancestor Version IDs (stored in blob as the ancestor chain)
  uint32_t ancestorCount = (uint32_t)ancestorVersionIds.size();
  blob.append(&ancestorCount, 4);
  for (const auto &h : ancestorVersionIds) {
    uint32_t hLen = (uint32_t)h.size();
    blob.append(&hLen, 4);
    blob.append(h.data(), hLen);
  }
  // ---------------------

  // Get all strips
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

    // Plugin BLOB — use cached state (updated on load/change detection)
    const auto &cached = strip->cachedPluginState_;
    uint32_t blobSize = (uint32_t)cached.getSize();
    blob.append(&blobSize, 4);
    if (blobSize > 0)
      blob.append(cached.getData(), blobSize);
  }

  // Fill in total size (excluding the 12-byte header)
  uint32_t totalSize = (uint32_t)(blob.getSize() - 12);
  std::memcpy(static_cast<uint8_t *>(blob.getData()) + 8, &totalSize, 4);

  std::cerr << "[StateManager] Built blob: " << blob.getSize() << " bytes, "
            << stripCount << " strips" << std::endl;

  return blob;
}

versioning::Hash StateManager::commitCurrentState(MixerModel &mixer,
                                                  const std::string &branchId) {
  if (!versionStore_)
    return "";

  versioning::FiddleState state;
  state.globalState.masterGainDb = 0.0f;

  auto strips = mixer.getAllStrips();

  for (auto *strip : strips) {
    versioning::StripBlob sb;
    sb.libraryId = versioning::kDefaultLibraryId;
    sb.library = strip->library.toStdString();
    sb.family = strip->family.toStdString();
    sb.isSolo = strip->isSolo;
    sb.inputPort = strip->inputPort;
    sb.inputChannel = strip->inputChannel;
    sb.pluginUid = strip->pluginUid;
    sb.gainDb = strip->gainDb.load(std::memory_order_relaxed);
    sb.expressionMapEntityId =
        strip->expressionMap ? strip->expressionMap->entityID : "";

    const auto &cached = strip->cachedPluginState_;
    if (cached.getSize() > 0) {
      const uint8_t *data = static_cast<const uint8_t *>(cached.getData());
      sb.pluginState.assign(data, data + cached.getSize());
    }

    state.stripHashes.push_back(sb.computeHash());
    versionStore_->getStorage().putStripBlob(sb.computeHash(), sb);
  }

  if (branchId.empty()) {
    std::cerr << "[StateManager] Error: Cannot commit, branch ID is empty."
              << std::endl;
    return "";
  }

  return versionStore_->commitVersion(branchId, state);
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

  juce::MessageManager::callAsync([this, fn = std::move(buildFn)]() {
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

  if (magic != kBlobMagic || version < 1 || version > kBlobVersion)
    return std::nullopt;

  // Config name
  uint32_t cfgNameLen = readU32();
  std::string cfgName = readString(cfgNameLen);

  // Config version (new in blob v2, absent in v1)
  std::string cfgVersion;
  if (version >= 2) {
    uint32_t cfgVerLen = readU32();
    cfgVersion = readString(cfgVerLen);
  }

  // Dirty flag
  uint8_t dirtyFlag = readU8();

  RestoredState state;

  if (version >= 3) {
    uint32_t shLen = readU32();
    state.stateHash = readString(shLen);

    uint32_t ancestorCount = readU32();
    for (uint32_t i = 0; i < ancestorCount; ++i) {
      uint32_t hLen = readU32();
      state.ancestorHashes.push_back(readString(hLen));
    }
  }

  // Strip count
  uint32_t stripCount = readU32();

  state.configName = juce::String(cfgName);
  state.configVersion = juce::String(cfgVersion);
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
