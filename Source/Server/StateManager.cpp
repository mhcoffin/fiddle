#include "StateManager.h"
#include <iostream>

namespace fiddle {

namespace {

versioning::PluginSlotBlob
makePluginSlotBlob(const MasterInsertSnapshot &snapshot) {
  versioning::PluginSlotBlob result;
  result.slotId = snapshot.slotId.toStdString();
  result.formatName = snapshot.description.pluginFormatName.toStdString();
  result.uniqueId = snapshot.description.uniqueId;
  result.fileOrIdentifier =
      snapshot.description.fileOrIdentifier.toStdString();
  result.manufacturer = snapshot.description.manufacturerName.toStdString();
  result.name = snapshot.description.name.toStdString();
  result.category = snapshot.description.category.toStdString();
  result.pluginVersion = snapshot.description.version.toStdString();
  result.numInputChannels = snapshot.description.numInputChannels;
  result.numOutputChannels = snapshot.description.numOutputChannels;
  result.bypassed = snapshot.bypassed;
  if (!snapshot.pluginState.isEmpty()) {
    const auto *data =
        static_cast<const uint8_t *>(snapshot.pluginState.getData());
    result.pluginState.assign(data, data + snapshot.pluginState.getSize());
  }
  return result;
}

versioning::GlobalState makeGlobalState(const MasterAudioSnapshot &master) {
  versioning::GlobalState result;
  result.audioSchemaVersion = 1;
  result.masterGainDb = master.gainDb;
  result.masterInserts.reserve(master.inserts.size());
  for (const auto &insert : master.inserts)
    result.masterInserts.push_back(makePluginSlotBlob(insert));
  return result;
}

void appendMasterState(juce::MemoryBlock &blob,
                       const MasterAudioSnapshot &master) {
  const uint32_t audioSchemaVersion = 1;
  blob.append(&audioSchemaVersion, sizeof(audioSchemaVersion));
  blob.append(&master.gainDb, sizeof(master.gainDb));
  const uint32_t insertCount =
      static_cast<uint32_t>(master.inserts.size());
  blob.append(&insertCount, sizeof(insertCount));

  for (const auto &insert : master.inserts) {
    auto *object = new juce::DynamicObject();
    object->setProperty("slotId", insert.slotId);
    object->setProperty("formatName", insert.description.pluginFormatName);
    object->setProperty("pluginUid", insert.description.uniqueId);
    object->setProperty("fileOrIdentifier",
                        insert.description.fileOrIdentifier);
    object->setProperty("manufacturer",
                        insert.description.manufacturerName);
    object->setProperty("name", insert.description.name);
    object->setProperty("category", insert.description.category);
    object->setProperty("pluginVersion", insert.description.version);
    object->setProperty("numInputChannels",
                        insert.description.numInputChannels);
    object->setProperty("numOutputChannels",
                        insert.description.numOutputChannels);
    object->setProperty("bypassed", insert.bypassed);
    const auto json = juce::JSON::toString(juce::var(object), false);
    const auto utf8 = json.toStdString();
    const uint32_t jsonSize = static_cast<uint32_t>(utf8.size());
    blob.append(&jsonSize, sizeof(jsonSize));
    blob.append(utf8.data(), utf8.size());
    const uint32_t stateSize =
        static_cast<uint32_t>(insert.pluginState.getSize());
    blob.append(&stateSize, sizeof(stateSize));
    if (stateSize > 0)
      blob.append(insert.pluginState.getData(), stateSize);
  }
}

} // namespace

StateManager::StateManager() = default;

StateManager::~StateManager() = default;

void StateManager::captureRoutingState(
    versioning::FiddleState &state,
    const std::map<std::string, versioning::Hash> &stripHashesById,
    bool persistFallbackBlobs) const {
  if (!routingRepository_)
    return;

  state.routingState.schemaVersion = 2;
  for (const auto &chair : routingRepository_->listChairs()) {
    versioning::ChairSnapshot saved;
    saved.id = chair.id;
    saved.instrumentEntityId = chair.instrumentEntityId;
    saved.name = chair.name;
    saved.family = chair.family;
    saved.isSolo = chair.role == DoricoRole::solo;
    saved.ordinal = chair.ordinal;
    saved.displayOrder = chair.displayOrder;
    saved.flatIndex = chair.flatIndex;
    state.routingState.chairs.push_back(std::move(saved));

    for (const auto &layer : routingRepository_->listLayers(chair.id)) {
      versioning::LayerSnapshot savedLayer;
      savedLayer.id = layer.id;
      savedLayer.chairId = layer.chairId;
      savedLayer.patchId = layer.patchId;
      savedLayer.patchName = layer.patchName;
      savedLayer.libraryId = layer.libraryId;
      savedLayer.libraryName = layer.libraryName;
      savedLayer.position = layer.position;
      savedLayer.sourcePatchRevision = layer.sourcePatchRevision;
      savedLayer.pluginStateEdited = layer.pluginStateEdited;
      // Backfill display metadata for layers created before it was copied
      // into the routing row.
      if (auto patch = routingRepository_->getPatch(layer.patchId)) {
        if (savedLayer.patchName.empty())
          savedLayer.patchName = patch->name;
        if (savedLayer.libraryId.empty())
          savedLayer.libraryId = patch->libraryId;
        if (savedLayer.libraryName.empty() && versionStore_) {
          if (auto name = versionStore_->getStorage().getLibraryName(
                  patch->libraryId))
            savedLayer.libraryName = *name;
        }
      }

      if (const auto found = stripHashesById.find(layer.id);
          found != stripHashesById.end()) {
        savedLayer.stripHash = found->second;
      } else {
        // A legacy-version checkout may have rebuilt free-standing strips
        // while the explicit routing rows remain available. Preserve each
        // layer from its durable row instead of emitting an unusable empty
        // strip reference.
        versioning::StripBlob fallback;
        fallback.libraryId = layer.id;
        fallback.library = savedLayer.libraryName;
        fallback.family = chair.family;
        fallback.isSolo = chair.role == DoricoRole::solo;
        fallback.active = layer.active;
        fallback.muted = layer.muted;
        fallback.soloed = layer.soloed;
        fallback.inputPort = chair.flatIndex / 16;
        fallback.inputChannel = chair.flatIndex % 16;
        fallback.pluginUid = layer.pluginUid;
        fallback.gainDb = layer.gainDb;
        fallback.expressionMapEntityId = layer.expressionMapId;
        fallback.pluginState = layer.pluginState;
        savedLayer.stripHash = fallback.computeHash();
        state.stripHashes.push_back(savedLayer.stripHash);
        if (persistFallbackBlobs && versionStore_)
          versionStore_->getStorage().putStripBlob(savedLayer.stripHash,
                                                    fallback);
      }
      state.routingState.layers.push_back(std::move(savedLayer));
    }
  }
}

void StateManager::initialize() {
  sharedMemory_ = std::make_unique<StateSharedMemory>(true /* producer */);
  // std::cerr << "[StateManager] Initialized" << std::endl;
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

  // Config version (new in blob v2)
  juce::String cfgVer = getConfigVersion();
  std::string cfgVerUtf8 = cfgVer.toStdString();
  uint32_t cfgVerLen = (uint32_t)cfgVerUtf8.size();
  blob.append(&cfgVerLen, 4);
  blob.append(cfgVerUtf8.data(), cfgVerLen);

  // Dirty flag
  uint8_t dirtyFlag = isDirty() ? 1 : 0;
  blob.append(&dirtyFlag, 1);

  const auto master = mixer.masterAudio().snapshotAll();
  appendMasterState(blob, master);

  // Generate the state identity embedded in the compatibility blob. Only an
  // actual version commit persists this state in the version object store;
  // persisting every shadow-blob rebuild leaked thousands of transient states.
  versioning::FiddleState state;
  state.globalState = makeGlobalState(master);

  auto strips = mixer.getAllStrips();
  std::map<std::string, versioning::Hash> stripHashesById;

  // Hash items and prep for writing into store.
  // Strip identity = (inputPort, inputChannel, libraryId).
  // Explicit layers use their immutable ID as the third identity component;
  // legacy free-standing strips retain the default-library identity.
  for (auto *strip : strips) {
    const auto realtime = strip->realtimeState();
    versioning::StripBlob sb;
    sb.libraryId = strip->chairId.isNotEmpty()
                       ? strip->id.toStdString()
                       : versioning::kDefaultLibraryId;
    sb.library = strip->library.toStdString();
    sb.family = strip->family.toStdString();
    sb.isSolo = strip->isSolo;
    sb.active = realtime.active;
    sb.muted = realtime.muted;
    sb.soloed = realtime.soloed;
    sb.inputPort = realtime.inputPort;
    sb.inputChannel = realtime.inputChannel;
    sb.pluginUid = strip->pluginUid;
    sb.gainDb = realtime.gainDb;
    sb.expressionMapEntityId =
        strip->expressionMap ? strip->expressionMap->entityID : "";
    sb.luaPluginFileNames = strip->getLuaPluginFileNames();

    const auto &cached = strip->cachedPluginState();
    if (cached.getSize() > 0) {
      const uint8_t *data = static_cast<const uint8_t *>(cached.getData());
      sb.pluginState.assign(data, data + cached.getSize());
    }

    const auto stripHash = sb.computeHash();
    state.stripHashes.push_back(stripHash);
    stripHashesById[strip->id.toStdString()] = stripHash;
  }

  captureRoutingState(state, stripHashesById, false);

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
    const auto realtime = strip->realtimeState();
    // Build strip JSON
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", strip->id);
    obj->setProperty("library", strip->library);
    obj->setProperty("family", strip->family);
    obj->setProperty("isSolo", strip->isSolo);
    obj->setProperty("active", realtime.active);
    obj->setProperty("muted", realtime.muted);
    obj->setProperty("soloed", realtime.soloed);
    obj->setProperty("inputPort", realtime.inputPort);
    obj->setProperty("inputChannel", realtime.inputChannel);
    obj->setProperty("pluginUid", strip->pluginUid);
    obj->setProperty("gainDb", (double)realtime.gainDb);
    obj->setProperty("expressionMap",
                     strip->expressionMap
                         ? juce::String(strip->expressionMap->entityID)
                         : juce::String());

    // Lua plugin filenames (stored as basenames for portability)
    juce::Array<juce::var> luaArr;
    for (const auto &name : strip->getLuaPluginFileNames())
      luaArr.add(juce::String(name));
    obj->setProperty("luaPlugins", luaArr);

    juce::String json = juce::JSON::toString(juce::var(obj), true);
    std::string jsonUtf8 = json.toStdString();
    uint32_t jsonLen = (uint32_t)jsonUtf8.size();
    blob.append(&jsonLen, 4);
    blob.append(jsonUtf8.data(), jsonLen);

    // Plugin BLOB — use cached state (updated on load/change detection)
    const auto &cached = strip->cachedPluginState();
    uint32_t blobSize = (uint32_t)cached.getSize();
    blob.append(&blobSize, 4);
    if (blobSize > 0)
      blob.append(cached.getData(), blobSize);
  }

  // Fill in total size (excluding the 12-byte header)
  uint32_t totalSize = (uint32_t)(blob.getSize() - 12);
  std::memcpy(static_cast<uint8_t *>(blob.getData()) + 8, &totalSize, 4);

  // std::cerr << "[StateManager] Built blob: " << blob.getSize() << " bytes, "
  //           << stripCount << " strips" << std::endl;

  return blob;
}

versioning::Hash StateManager::commitCurrentState(MixerModel &mixer,
                                                  const std::string &branchId) {
  if (!versionStore_)
    return "";

  versioning::FiddleState state;
  state.globalState = makeGlobalState(mixer.masterAudio().snapshotAll());

  auto strips = mixer.getAllStrips();
  std::map<std::string, versioning::Hash> stripHashesById;

  for (auto *strip : strips) {
    const auto realtime = strip->realtimeState();
    versioning::StripBlob sb;
    sb.libraryId = strip->chairId.isNotEmpty()
                       ? strip->id.toStdString()
                       : versioning::kDefaultLibraryId;
    sb.library = strip->library.toStdString();
    sb.family = strip->family.toStdString();
    sb.isSolo = strip->isSolo;
    sb.active = realtime.active;
    sb.muted = realtime.muted;
    sb.soloed = realtime.soloed;
    sb.inputPort = realtime.inputPort;
    sb.inputChannel = realtime.inputChannel;
    sb.pluginUid = strip->pluginUid;
    sb.gainDb = realtime.gainDb;
    sb.expressionMapEntityId =
        strip->expressionMap ? strip->expressionMap->entityID : "";
    sb.luaPluginFileNames = strip->getLuaPluginFileNames();

    const auto &cached = strip->cachedPluginState();
    if (cached.getSize() > 0) {
      const uint8_t *data = static_cast<const uint8_t *>(cached.getData());
      sb.pluginState.assign(data, data + cached.getSize());
    }

    const auto stripHash = sb.computeHash();
    state.stripHashes.push_back(stripHash);
    stripHashesById[strip->id.toStdString()] = stripHash;
    versionStore_->getStorage().putStripBlob(stripHash, sb);
  }

  captureRoutingState(state, stripHashesById, true);

  if (branchId.empty()) {
    // std::cerr << "[StateManager] Error: Cannot commit, branch ID is empty."
    //           << std::endl;
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

} // namespace fiddle
