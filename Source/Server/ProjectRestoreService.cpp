#include "ProjectRestoreService.h"
#include "LuaPlugin.h"
#include "BridgeMapDocument.h"
#include "LegacyStripConversion.h"
#include "ChairBusMigration.h"

#include <map>
#include <set>
#include <utility>

namespace fiddle {

namespace {

/// Check that a version's audio buses, chair buses and legacy layer routing
/// are well formed and only name saved audio buses. Fills each saved chair
/// bus's sends.
std::optional<std::string> validateBusRouting(
    const versioning::FiddleState &state,
    const std::map<std::string, versioning::StripBlob> &blobs,
    std::map<std::string, std::vector<AudioSend>> &chairSends) {
  std::set<std::string> busIds;
  for (const auto &bus : state.globalState.groupBuses)
    if (bus.id.empty() || !busIds.insert(bus.id).second)
      return "Invalid or duplicate audio bus ID";
  for (const auto &[hash, blob] : blobs) {
    if (!blob.directOutputBusId.empty() && !busIds.count(blob.directOutputBusId))
      return "A saved audio bus destination is missing";
    std::vector<AudioSend> sends;
    if (!blob.sendsJson.empty() && !audioSendsFromJson(juce::JSON::parse(blob.sendsJson), sends))
      return "Invalid saved post-fader sends";
    for (const auto &send : sends)
      if (!busIds.count(send.busId.toStdString()))
        return "A saved send destination is missing";
  }
  for (const auto &chair : state.globalState.chairBuses) {
    if (chair.chairId.empty() || chairSends.count(chair.chairId))
      return "Invalid or duplicate chair bus";
    auto &sends = chairSends[chair.chairId];
    if (!chair.directOutputBusId.empty() && !busIds.count(chair.directOutputBusId))
      return "A saved chair output is missing";
    if (!chair.sendsJson.empty() && !audioSendsFromJson(juce::JSON::parse(chair.sendsJson), sends))
      return "Invalid saved chair sends";
    for (const auto &send : sends)
      if (!busIds.count(send.busId.toStdString()))
        return "A saved chair send destination is missing";
  }
  return std::nullopt;
}

LegacyLayerRouting legacyRoutingOf(const versioning::StripBlob &blob) {
  LegacyLayerRouting routing{blob.directOutputBusId, {}};
  if (!blob.sendsJson.empty())
    audioSendsFromJson(juce::JSON::parse(blob.sendsJson), routing.sends);
  return routing;
}

} // namespace

struct ProjectRestoreService::Session {
  bool cancelled = false;
  bool finished = false;
  int pending = 1; // Construction barrier, including synchronous failures.
  MixerModel *mixer = nullptr;
  Callbacks callbacks;

  static void complete(const std::shared_ptr<Session> &session) {
    if (session->cancelled || --session->pending != 0)
      return;
    juce::MessageManager::callAsync([session] {
      if (session->cancelled)
        return;
      session->mixer->refreshAudioRouting();
      session->finished = true;
      if (session->callbacks.finished)
        session->callbacks.finished();
    });
  }

  static auto completion(const std::shared_ptr<Session> &session) {
    ++session->pending;
    // insert() may reject without scheduling a load. Also tolerate an API
    // reporting the rejection through its callback before returning false.
    // HostedPluginSlot deliberately discards callbacks for cancelled loads;
    // release the barrier when that last callback copy is destroyed as well.
    struct Ticket {
      std::shared_ptr<Session> session;
      bool delivered = false;
      void finish() {
        if (!std::exchange(delivered, true))
          Session::complete(session);
      }
      ~Ticket() { finish(); }
    };
    auto ticket = std::make_shared<Ticket>();
    ticket->session = session;
    return [ticket](bool, const juce::String &) {
      ticket->finish();
    };
  }
};

ProjectRestoreService::ProjectRestoreService(
    MixerModel &mixer, versioning::VersionStore &versions,
    LibraryRoutingRepository *repository, Callbacks callbacks)
    : mixer_(mixer), versions_(versions), repository_(repository),
      callbacks_(std::move(callbacks)) {}

ProjectRestoreService::~ProjectRestoreService() {
  if (session_)
    session_->cancelled = true;
}

bool ProjectRestoreService::isLoading() const {
  return session_ && !session_->cancelled && !session_->finished;
}

ProjectRestoreService::Result
ProjectRestoreService::restore(const versioning::FiddleState &state) {
  BridgeMapProject bridgeProject;
  juce::String bridgeError;
  if (!BridgeMapProject::parse(juce::String(state.globalState.projectSettings.bridgeMapsJson), bridgeProject, bridgeError))
    return {false, false, "Invalid saved bridge project: " + bridgeError.toStdString()};
  const bool topology = state.routingState.schemaVersion >= 1;
  if (state.routingState.schemaVersion > 3 || (topology && !repository_))
    return {false, false, "Unsupported or unavailable routing schema"};

  // Resolve every blob before touching either the live mixer or session DB.
  std::map<std::string, versioning::StripBlob> blobs;
  auto fetch = [&](const std::string &hash) {
    if (blobs.find(hash) != blobs.end())
      return true;
    auto blob = versions_.getStripBlob(hash);
    if (!blob)
      return false;
    blobs.emplace(hash, std::move(*blob));
    return true;
  };
  for (const auto &hash : state.stripHashes)
    if (!fetch(hash))
      return {false, false, "A saved strip blob is missing"};
  for (const auto &layer : state.routingState.layers)
    if (!fetch(layer.stripHash))
      return {false, false, "A saved layer blob is missing"};

  std::map<std::string, std::vector<AudioSend>> chairSends;
  if (auto error = validateBusRouting(state, blobs, chairSends))
    return {false, false, *error};

  std::vector<ChairRow> chairs;
  std::vector<LayerRow> layers;
  std::map<std::string, std::string> layerHashes;
  // A version saved before chairs existed is converted to chairs, layers and
  // generated libraries (LegacyStripConversion.h). Without a repository (some
  // tests) its strips are restored as they were saved.
  const bool convertLegacy = !topology && repository_;
  std::vector<std::string> generatedLibraries;
  if (convertLegacy) {
    std::vector<std::pair<versioning::Hash, versioning::StripBlob>> strips;
    for (const auto &hash : state.stripHashes)
      strips.emplace_back(hash, blobs.at(hash));
    auto conversion = convertLegacyStrips(strips, repository_->listChairs(),
                                          [] { return juce::Uuid().toString().toStdString(); });
    // Libraries are catalog data, written before the project topology. They
    // stay if the topology is then rejected; they are valid either way.
    for (const auto &generated : conversion.libraries) {
      const auto merged = mergeRestoredLibrary(repository_->captureLibrary(generated.id), generated);
      if (repository_->restoreLibrary(merged) != PatchReplaceResult::replaced)
        return {false, false, "Could not create the library \"" + generated.name + "\" for this version's strips"};
      generatedLibraries.push_back(generated.name);
    }
    chairs = std::move(conversion.chairs);
    layers = std::move(conversion.layers);
    layerHashes = std::move(conversion.layerHashes);
  }
  for (const auto &saved : state.routingState.chairs) {
    chairs.push_back({saved.id, saved.instrumentEntityId, saved.name, saved.family,
                      saved.isSolo ? DoricoRole::solo : DoricoRole::section,
                      saved.ordinal, saved.displayOrder, saved.flatIndex,
                      saved.kitId, saved.kitName});
  }
  for (const auto &saved : state.routingState.layers) {
    const auto &blob = blobs.at(saved.stripHash);
    layers.push_back({saved.id, saved.chairId, saved.patchId, saved.patchName,
                      saved.libraryId, saved.libraryName, saved.position,
                      blob.active, blob.muted, blob.soloed, blob.gainDb,
                      blob.pluginUid, blob.pluginState, blob.expressionMapEntityId,
                      saved.sourcePatchRevision, saved.pluginStateEdited});
    if (const auto map = compilePercussionMap(juce::String::fromUTF8(blob.percussionMapSourceXml.c_str()))) {
      layers.back().percussionMapId = map->entityId;
      layers.back().percussionPlayAs = blob.percussionPlayAs;
    }
  }
  // replaceTopology validates all identities/references before its atomic
  // transaction. Failure must not silently fall back to a different project.
  if ((topology || convertLegacy) && !repository_->replaceTopology(chairs, layers))
    return {false, false, "Could not restore chair/layer topology"};

  auto session = beginSession();
  mixer_.clear();
  // Also invalidate session verification when restoring an identical registry.
  if (mixer_.onBridgeMapsChanged)
    mixer_.onBridgeMapsChanged(state.globalState.projectSettings.bridgeMapsJson);
  mixer_.setProjectSettings(state.globalState.projectSettings);
  mixer_.masterAudio().clear(false);

  auto restoreRack = [&](StripAudioEngine &engine, const std::vector<uint8_t> &bytes) {
    restoreAudioRack(engine, bytes, session);
  };
  restoreBuses(state, chairSends, session);

  // Versions saved before chair buses kept outputs and sends on each layer.
  // Gather them per chair; they move to the chair bus once all layers exist.
  std::map<juce::String, std::vector<LegacyLayerRouting>> legacyRouting;
  std::map<juce::String, juce::String> chairNames;
  auto restoreStrip = [&](std::unique_ptr<MixerStrip> strip,
                          const versioning::StripBlob &blob) {
    auto *current = strip.get();
    const auto id = strip->id;
    strip->setActive(blob.active);
    strip->setMuted(blob.muted);
    strip->setSoloed(blob.soloed);
    strip->setGainDb(blob.gainDb);
    strip->pluginUid = blob.pluginUid;
    const auto key = MixerModel::chairBusKey(*strip);
    legacyRouting[key].push_back(legacyRoutingOf(blob));
    if (!chairNames.count(key))
      chairNames[key] = strip->layerName.isNotEmpty() ? strip->layerName : strip->library;
    mixer_.insertStripAt(std::move(strip), mixer_.size());
    if (callbacks_.stripCreated)
      callbacks_.stripCreated(*current);
    if (!blob.expressionMapEntityId.empty() && callbacks_.loadMap) {
      const auto sourceXml = juce::String::fromUTF8(
          blob.expressionMapSourceXml.data(),
          static_cast<int>(blob.expressionMapSourceXml.size()));
      auto map = callbacks_.loadMap(blob.expressionMapEntityId, sourceXml);
      if (map)
        current->setExpressionMapAssignment(
            {std::move(map),
             juce::String::fromUTF8(blob.expressionMapPath.c_str()),
             sourceXml});
    }
    if (auto map = compilePercussionMap(juce::String::fromUTF8(blob.percussionMapSourceXml.c_str())))
      current->setPercussionMapAssignment(
          keepingPlayAs({std::move(map), juce::String::fromUTF8(blob.percussionMapSourceXml.c_str())},
                        blob.percussionPlayAs));
    for (const auto &name : blob.luaPluginFileNames) {
      const auto path = callbacks_.resolveLua ? callbacks_.resolveLua(name) : "";
      auto plugin = std::make_shared<LuaPlugin>(path.empty() ? name : path);
      if (!path.empty())
        (void)plugin->load();
      // Preserve the ordered reference even if its source is currently missing
      // or invalid. The unloaded placeholder is inert and remains visible in
      // the UI, so a later save cannot silently erase project intent.
      current->addLuaPlugin(std::move(plugin));
    }
    restoreRack(current->audioEngine(), blob.audioInsertState);
    if (blob.pluginUid == 0)
      return;
    const juce::MemoryBlock bytes(blob.pluginState.data(), blob.pluginState.size());
    const auto description = callbacks_.findInstrument
                                 ? callbacks_.findInstrument(blob.pluginUid)
                                 : std::nullopt;
    if (!description) {
      current->markPluginMissing(blob.pluginUid, bytes, "Plug-in is not present in the scanned catalog");
      return;
    }
    auto done = Session::completion(session);
    current->loadPlugin(*description, mixer_.getFormatManager(),
                        [session, current, id, uid = blob.pluginUid, bytes, done](bool success) {
      if (!session->cancelled && session->mixer->getStrip(id) == current) {
        if (success) {
          if (!bytes.isEmpty())
            current->applyPluginState(bytes.getData(), static_cast<int>(bytes.getSize()));
          (void)current->consumePluginChangeNotification();
          (void)current->consumePluginExplicitEditNotification();
          (void)current->consumePluginNonParameterStateChangeNotification();
          if (session->callbacks.instrumentReady)
            session->callbacks.instrumentReady(*current);
        } else {
          current->markPluginMissing(uid, bytes, "Plug-in could not be instantiated");
        }
      }
      done(success, {});
    });
  };

  if (topology || convertLegacy) {
    for (const auto &saved : state.routingState.layers)
      layerHashes.emplace(saved.id, saved.stripHash);
    // Repository ordering is the same as ordinary chair/layer materialization.
    for (const auto &chair : repository_->listChairs()) {
      chairNames[juce::String(chair.id)] = juce::String(chair.name);
      for (const auto &layer : repository_->listLayers(chair.id)) {
        const auto patch = repository_->getPatch(layer.patchId);
        auto strip = std::make_unique<MixerStrip>();
        strip->id = layer.id;
        strip->chairId = chair.id;
        strip->setInstrumentEntityId(chair.instrumentEntityId);
        strip->patchId = layer.patchId;
        strip->libraryId = layer.libraryId;
        strip->layerName = !layer.patchName.empty() ? layer.patchName
                            : patch ? patch->name : "Missing catalog patch";
        strip->library = !layer.libraryName.empty() ? layer.libraryName
            : patch ? versions_.getStorage().getLibraryName(patch->libraryId).value_or("Missing library")
                    : "Missing library";
        strip->missingPatchReference = !patch;
        strip->family = chair.family;
        strip->isSolo = chair.role == DoricoRole::solo;
        strip->setInputAssignment(chair.flatIndex / 16, chair.flatIndex % 16);
        restoreStrip(std::move(strip), blobs.at(layerHashes.at(layer.id)));
      }
    }
  } else {
    for (const auto &hash : state.stripHashes) {
      const auto &blob = blobs.at(hash);
      auto strip = std::make_unique<MixerStrip>();
      strip->library = blob.library;
      strip->family = blob.family;
      strip->isSolo = blob.isSolo;
      strip->setInputAssignment(blob.inputPort, blob.inputChannel);
      restoreStrip(std::move(strip), blob);
    }
  }

  auto inexactChairRouting = migrateLegacyRouting(legacyRouting, chairSends, chairNames);

  auto &master = mixer_.masterAudio();
  master.setGainDb(state.globalState.masterGainDb, false);
  for (const auto &saved : state.globalState.masterInserts) {
    MasterInsertSnapshot insert;
    insert.slotId = saved.slotId;
    insert.description.pluginFormatName = saved.formatName;
    insert.description.uniqueId = saved.uniqueId;
    insert.description.fileOrIdentifier = saved.fileOrIdentifier;
    insert.description.manufacturerName = saved.manufacturer;
    insert.description.name = saved.name;
    insert.description.category = saved.category;
    insert.description.version = saved.pluginVersion;
    insert.description.numInputChannels = saved.numInputChannels;
    insert.description.numOutputChannels = saved.numOutputChannels;
    insert.bypassed = saved.bypassed;
    insert.pluginState = juce::MemoryBlock(saved.pluginState.data(), saved.pluginState.size());
    auto done = Session::completion(session);
    if (!master.insert(insert, master.insertCount(), mixer_.getFormatManager(), done, false))
      done(false, "Insert rejected");
  }
  mixer_.refreshAudioRouting();
  Session::complete(session);
  return {true, topology || convertLegacy, {}, std::move(generatedLibraries),
          std::move(inexactChairRouting)};
}

std::shared_ptr<ProjectRestoreService::Session> ProjectRestoreService::beginSession() {
  if (session_)
    session_->cancelled = true;
  auto session = std::make_shared<Session>();
  session_ = session;
  session->mixer = &mixer_;
  session->callbacks = callbacks_;
  return session;
}

void ProjectRestoreService::restoreAudioRack(StripAudioEngine &engine,
                                             const std::vector<uint8_t> &bytes,
                                             const std::shared_ptr<Session> &session) {
  if (bytes.empty())
    return;
  const auto snapshot = deserializeStripAudioSnapshot(bytes.data(), bytes.size());
  engine.setBalance(snapshot.balance);
  auto insertRack = [&](const auto &rack, StripInsertPosition position) {
    for (int i = 0; i < static_cast<int>(rack.size()); ++i) {
      auto done = Session::completion(session);
      if (!engine.insert(rack[static_cast<size_t>(i)], position, i,
                         mixer_.getFormatManager(), done, false))
        done(false, "Insert rejected");
    }
  };
  insertRack(snapshot.preFaderInserts, StripInsertPosition::preFader);
  insertRack(snapshot.postFaderInserts, StripInsertPosition::postFader);
}

void ProjectRestoreService::restoreBuses(
    const versioning::FiddleState &state,
    const std::map<std::string, std::vector<AudioSend>> &chairSends,
    const std::shared_ptr<Session> &session) {
  for (const auto &saved : state.globalState.groupBuses) {
    auto bus = std::make_unique<GroupBus>();
    bus->id = saved.id;
    bus->name = saved.name;
    bus->setGainDb(saved.gainDb);
    bus->setMuted(saved.muted);
    bus->setSoloed(saved.soloed);
    auto *restored = bus.get();
    mixer_.insertGroupBusAt(std::move(bus), static_cast<int>(mixer_.getAllGroupBuses().size()));
    if (callbacks_.busCreated)
      callbacks_.busCreated(*restored);
    restoreAudioRack(restored->audioEngine(), saved.audioInsertState, session);
  }

  for (const auto &saved : state.globalState.chairBuses) {
    auto *chair = mixer_.ensureChairBus(juce::String(saved.chairId));
    chair->setGainDb(saved.gainDb);
    chair->setMuted(saved.muted);
    chair->setSoloed(saved.soloed);
    chair->directOutputBusId = saved.directOutputBusId;
    chair->sends = chairSends.at(saved.chairId);
    restoreAudioRack(chair->audioEngine(), saved.audioInsertState, session);
  }
}

std::vector<std::string> ProjectRestoreService::migrateLegacyRouting(
    const std::map<juce::String, std::vector<LegacyLayerRouting>> &legacyRouting,
    const std::map<std::string, std::vector<AudioSend>> &chairSends,
    const std::map<juce::String, juce::String> &chairNames) {
  std::vector<std::string> inexactChairRouting;
  for (const auto &[key, layers] : legacyRouting) {
    if (chairSends.count(key.toStdString()) ||
        std::all_of(layers.begin(), layers.end(), [](const auto &layer) {
          return layer.output.isEmpty() && layer.sends.empty();
        }))
      continue;
    const auto migrated = migrateChairRouting(layers, [] { return juce::Uuid().toString(); });
    auto *chair = mixer_.ensureChairBus(key);
    chair->directOutputBusId = migrated.output;
    chair->sends = migrated.sends;
    if (!migrated.exact) {
      const auto name = chairNames.find(key);
      inexactChairRouting.push_back(
          (name != chairNames.end() ? name->second : key).toStdString());
    }
  }
  return inexactChairRouting;
}

ProjectRestoreService::Result
ProjectRestoreService::restoreBusRouting(const versioning::FiddleState &state) {
  std::map<std::string, versioning::StripBlob> blobs;
  for (const auto &layer : state.routingState.layers) {
    auto blob = versions_.getStripBlob(layer.stripHash);
    if (!blob)
      return {false, false, "A saved layer blob is missing"};
    blobs.emplace(layer.stripHash, std::move(*blob));
  }
  std::map<std::string, std::vector<AudioSend>> chairSends;
  if (auto error = validateBusRouting(state, blobs, chairSends))
    return {false, false, *error};

  auto session = beginSession();
  for (auto *bus : mixer_.getAllGroupBuses()) {
    const auto id = bus->id;
    mixer_.removeGroupBus(id);
  }
  restoreBuses(state, chairSends, session);

  // Layers already in the mixer whose saved record still carries routing
  // (a version saved before chair buses) move it to their chair bus.
  std::map<juce::String, std::vector<LegacyLayerRouting>> legacyRouting;
  for (const auto &layer : state.routingState.layers)
    if (auto *strip = mixer_.getStrip(juce::String(layer.id)))
      legacyRouting[MixerModel::chairBusKey(*strip)].push_back(
          legacyRoutingOf(blobs.at(layer.stripHash)));
  std::map<juce::String, juce::String> chairNames;
  for (const auto &chair : state.routingState.chairs)
    chairNames[juce::String(chair.id)] = juce::String(chair.name);
  auto inexactChairRouting = migrateLegacyRouting(legacyRouting, chairSends, chairNames);

  mixer_.refreshAudioRouting();
  Session::complete(session);
  return {true, false, {}, {}, std::move(inexactChairRouting)};
}

} // namespace fiddle
