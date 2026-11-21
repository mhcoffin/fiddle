// Agent control (docs/mcp-control.md): the requests the MCP adapter sends
// through AgentControlServer. Every request runs on the message thread and
// edits go through the same Undo histories as the UI: project edits on
// undoManager_, catalog (library and map) edits on libraryUndoManager_.
#include "MainComponent.h"

#include "AgentSetupRequests.h"
#include "BridgeMapCommands.h"
#include "ChairActions.h"
#include "ChairLayerActions.h"
#include "FiddleConfig.h"
#include "LibraryCatalogAction.h"
#include "LuaParamJson.h"
#include "MixerCommandService.h"
#include "PercussionMap.h"
#include "PluginJsHandlers.h"
#include "ProjectRestoreService.h"
#include "UndoActions.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>

namespace fiddle {
namespace {

using Response = AgentControlServer::Response;

juce::String text(const juce::var &params, const char *name) {
  return params.isObject() ? params[name].toString().trim() : juce::String();
}

bool flag(const juce::var &params, const char *name, bool fallback = false) {
  if (!params.isObject() || !params.hasProperty(name)) return fallback;
  const auto value = params[name];
  return value.isBool() ? static_cast<bool>(value) : fallback;
}

int limitParam(const juce::var &params, const char *name, int fallback, int maximum) {
  if (!params.isObject() || !(params[name].isInt() || params[name].isInt64())) return fallback;
  return juce::jlimit(1, maximum, static_cast<int>(params[name]));
}

juce::var historyJson(const UndoManager &history) {
  auto *result = new juce::DynamicObject();
  result->setProperty("canUndo", history.canUndo());
  result->setProperty("canRedo", history.canRedo());
  result->setProperty("undoDescription", history.undoDescription());
  result->setProperty("redoDescription", history.redoDescription());
  return juce::var(result);
}

juce::var object(std::initializer_list<std::pair<const char *, juce::var>> properties) {
  auto *result = new juce::DynamicObject();
  for (const auto &[name, value] : properties) result->setProperty(name, value);
  return juce::var(result);
}

juce::String roleName(DoricoRole role) {
  return role == DoricoRole::solo ? "solo" : "section";
}

// Maps read from .doricolib files during one request, by kind and path.
class MapFiles {
public:
  const std::vector<LibraryMapCandidate> &read(LibraryMapKind kind, const juce::String &path) {
    auto &entry = files_[{kind, path}];
    if (!entry) {
      juce::String error;
      const juce::File file(path);
      entry = std::make_unique<std::vector<LibraryMapCandidate>>(
          file.existsAsFile() ? readLibraryMapFile(kind, file, error)
                              : std::vector<LibraryMapCandidate>{});
    }
    return *entry;
  }
  const LibraryMapCandidate *find(LibraryMapKind kind, const juce::String &path,
                                  const std::string &entityId) {
    const auto &maps = read(kind, path);
    const auto found = std::find_if(maps.begin(), maps.end(),
                                    [&](const auto &map) { return map.entityId == entityId; });
    return found == maps.end() ? nullptr : &*found;
  }

private:
  std::map<std::pair<LibraryMapKind, juce::String>,
           std::unique_ptr<std::vector<LibraryMapCandidate>>> files_;
};

// Undoable imports of the maps a catalog edit needs. A map Fiddle already has
// is kept as it is: other patches and layers may use it (docs/maps.md).
bool appendMapImports(LibraryRoutingRepository &repository, MapFiles &files,
                      const std::vector<agent::MapImport> &imports,
                      std::vector<std::unique_ptr<UndoableAction>> &actions,
                      juce::String &error) {
  std::set<std::pair<LibraryMapKind, std::string>> seen;
  for (const auto &import : imports) {
    if (!seen.insert({import.kind, import.entityId}).second ||
        repository.getMap(import.kind, import.entityId))
      continue;
    const auto *candidate = files.find(import.kind, import.path, import.entityId);
    if (!candidate) {
      error = "Map " + juce::String(import.entityId) + " was not found in " + import.path;
      return false;
    }
    LibraryMapRow row;
    row.kind = candidate->kind;
    row.entityId = candidate->entityId;
    row.name = candidate->name;
    row.xml = candidate->definitionXml.toStdString();
    row.sourcePath = candidate->file.getFullPathName().toStdString();
    row.sourceVersion = candidate->version;
    row.sourceHash = candidate->hash.toStdString();
    const auto noun = row.kind == LibraryMapKind::percussion ? "Import percussion map: "
                                                             : "Import expression map: ";
    actions.push_back(std::make_unique<LibraryMapCatalogAction>(
        repository, row.kind, row.entityId, row, noun + juce::String(row.name)));
  }
  return true;
}

} // namespace

void MainComponent::initAgentControl() {
  if (agentControlServer_)
    return;

  juce::Component::SafePointer<MainComponent> safeThis(this);
  agentControlServer_ = std::make_unique<AgentControlServer>(
      FiddleConfig::getAppDataDir().getChildFile("agent-control.json"),
      [safeThis](const juce::String &method, const juce::var &params,
                 AgentControlServer::Completion complete) {
        juce::MessageManager::callAsync(
            [safeThis, method, params,
             complete = std::move(complete)]() mutable {
              if (safeThis == nullptr) {
                complete(AgentControlServer::Response::failure(
                    "Fiddle is shutting down"));
                return;
              }
              complete(safeThis->handleAgentControlRequest(method, params));
            });
      });
  agentControlServer_->startThread();
}

AgentControlServer::Response MainComponent::handleAgentControlRequest(
    const juce::String &method, const juce::var &params) {
  if (!projectRestoreReady_ ||
      (projectRestoreService_ && projectRestoreService_->isLoading()))
    return Response::failure("Fiddle is still initializing");

  using Handler = std::function<Response(MainComponent &, const juce::var &)>;
  const auto member = [](Response (MainComponent::*function)(const juce::var &)) {
    return Handler([function](MainComponent &self, const juce::var &p) { return (self.*function)(p); });
  };
  const auto layerControl = [](const char *name) {
    return Handler([name](MainComponent &self, const juce::var &p) {
      return self.agentSetLayerControl(name, p);
    });
  };
  static const std::map<juce::String, Handler> handlers{
      {"status", [](MainComponent &self, const juce::var &) { return Response::success(self.agentStatus()); }},
      {"mixer.get", member(&MainComponent::agentGetMixer)},
      {"layer.setGain", layerControl("layer.setGain")},
      {"layer.setMute", layerControl("layer.setMute")},
      {"layer.setSolo", layerControl("layer.setSolo")},
      {"history.undo", [](MainComponent &self, const juce::var &p) { return self.agentUndoRedo(p, false); }},
      {"history.redo", [](MainComponent &self, const juce::var &p) { return self.agentUndoRedo(p, true); }},
      {"library.inspectSources", member(&MainComponent::agentInspectLibrarySources)},
      {"library.setup.create", member(&MainComponent::createAgentGuidedLibrary)},
      {"library.setup.get", [](MainComponent &self, const juce::var &p) {
         const auto id = text(p, "libraryId");
         if (id.isEmpty()) return Response::failure("libraryId is required");
         const auto libraries = self.db_.listLibraries();
         if (std::none_of(libraries.begin(), libraries.end(),
                          [&](const auto &library) { return library.id == id; }))
           return Response::failure("Library not found");
         return Response::success(self.agentLibrarySetupSnapshot(id));
       }},
      {"library.setup.open", member(&MainComponent::agentOpenGuidedLibrary)},
      {"library.list", member(&MainComponent::agentListLibraries)},
      {"library.get", member(&MainComponent::agentGetLibrary)},
      {"library.save", member(&MainComponent::agentSaveLibrary)},
      {"library.delete", member(&MainComponent::agentDeleteLibrary)},
      {"maps.list", member(&MainComponent::agentListMaps)},
      {"dorico.instruments.search", member(&MainComponent::agentSearchDoricoInstruments)},
      {"plugins.list", member(&MainComponent::agentListPlugins)},
      {"plugins.scan", member(&MainComponent::agentScanPlugins)},
      {"chairs.get", [](MainComponent &self, const juce::var &p) { return Response::success(self.agentChairsSnapshot(p)); }},
      {"patches.find", member(&MainComponent::agentFindPatches)},
      {"chairs.create", member(&MainComponent::agentCreateChairs)},
      {"chairs.update", member(&MainComponent::agentUpdateChair)},
      {"chairs.delete", member(&MainComponent::agentDeleteChair)},
      {"kits.create", member(&MainComponent::agentCreateKit)},
      {"kits.rename", member(&MainComponent::agentRenameKit)},
      {"kits.delete", member(&MainComponent::agentDeleteKit)},
      {"layers.add", member(&MainComponent::agentAddLayer)},
      {"layers.remove", member(&MainComponent::agentRemoveLayer)},
      {"template.install", member(&MainComponent::agentInstallTemplate)},
      {"layers.refresh", member(&MainComponent::agentRefreshLayers)},
      {"bridge.get", member(&MainComponent::agentGetBridgeMaps)},
      {"bridge.update", member(&MainComponent::agentUpdateBridgeMaps)},
      {"patch.player.open", member(&MainComponent::agentOpenPatchPlayer)},
      {"patch.player.status", member(&MainComponent::agentPatchPlayerStatus)},
      {"patch.player.capture", member(&MainComponent::agentCapturePatchPlayer)},
      {"patch.player.close", member(&MainComponent::agentClosePatchPlayer)},
  };
  const auto handler = handlers.find(method);
  if (handler == handlers.end())
    return Response::failure("Unknown control method: " + method);
  if (!params.isVoid() && !params.isObject())
    return Response::failure("Control parameters must be an object");
  return handler->second(*this, params);
}

juce::String MainComponent::agentSetupBlocker(bool catalog) const {
  if (isTransportStarted_.load(std::memory_order_relaxed))
    return "Stop Dorico playback before changing Fiddle's setup";
  if (catalog && libraryDraftDirty_)
    return "Library Manager has unsaved edits. Ask the user to save or discard "
           "them before the library catalog is changed.";
  return {};
}

juce::var MainComponent::agentStatus() const {
  auto *status = new juce::DynamicObject();
  status->setProperty("application", "FiddleServer");
  auto *application = juce::JUCEApplicationBase::getInstance();
  status->setProperty("version", application
                                     ? application->getApplicationVersion()
                                     : juce::String("unknown"));
  const bool ready = projectRestoreReady_ &&
                     (!projectRestoreService_ ||
                      !projectRestoreService_->isLoading());
  status->setProperty("ready", ready);
  status->setProperty("dirty", stateManager_.isDirty());
  status->setProperty(
      "transportPlaying",
      isTransportStarted_.load(std::memory_order_relaxed));
  status->setProperty("detached", isDetached_);
  status->setProperty("branchId", juce::String(currentBranchId_));
  status->setProperty("versionId", juce::String(currentVersionId_));
  status->setProperty("layerCount", mixer_.size());
  status->setProperty("doricoConnected", doricoConnected_);
  // What the user was told when the Dorico project connected.
  if (!restoreReport_.isVoid())
    status->setProperty("projectRestore", restoreReport_);
  if (connectionWarning_.isNotEmpty())
    status->setProperty("connectionWarning", connectionWarning_);

  auto *history = new juce::DynamicObject();
  history->setProperty("canUndo", undoManager_.canUndo());
  history->setProperty("canRedo", undoManager_.canRedo());
  history->setProperty("undoDescription", undoManager_.undoDescription());
  history->setProperty("redoDescription", undoManager_.redoDescription());
  status->setProperty("history", juce::var(history));
  status->setProperty("libraryHistory", historyJson(libraryUndoManager_));
  status->setProperty("libraryEditorHasUnsavedChanges", libraryDraftDirty_);
  status->setProperty("pluginScanRunning", pluginScanner_.isScanning());
  return juce::var(status);
}

juce::var MainComponent::agentLayerSnapshot(const MixerStrip &strip) const {
  auto *layer = new juce::DynamicObject();
  const auto state = strip.realtimeState();
  layer->setProperty("id", strip.id);
  layer->setProperty("name", strip.layerName);
  layer->setProperty("library", strip.library);
  layer->setProperty("family", strip.family);
  layer->setProperty("chairId", strip.chairId);
  layer->setProperty("patchId", strip.patchId);
  layer->setProperty("directOutputBusId", mixer_.stripOutputBusId(strip.id));
  layer->setProperty("active", state.active);
  layer->setProperty("muted", state.muted);
  layer->setProperty("soloed", state.soloed);
  // Numbered like fiddle_get_chairs: channel 1-16, or 0 with port -1 when
  // the layer has no MIDI input.
  const bool hasInput = state.inputPort >= 0 && state.inputChannel >= 0;
  layer->setProperty("midiPort", hasInput ? state.inputPort : -1);
  layer->setProperty("midiChannel", hasInput ? state.inputChannel + 1 : 0);
  layer->setProperty("gainDb", static_cast<double>(state.gainDb));
  layer->setProperty("peakDb", static_cast<double>(state.peakDb));
  layer->setProperty("pluginUid", strip.pluginUid);
  layer->setProperty("hasPlugin", strip.hasPlugin());
  layer->setProperty("pluginStatus",
                     HostedPluginSlot::statusName(strip.pluginStatus()));
  layer->setProperty("pluginError", strip.pluginError());
  layer->setProperty(
      "expressionMap",
      strip.expressionMap ? juce::String(strip.expressionMap->name) : "");

  juce::Array<juce::var> luaProcessors;
  for (const auto &processor : strip.luaPlugins) {
    auto *item = new juce::DynamicObject();
    item->setProperty("name", juce::String(processor->meta().name));
    item->setProperty("loaded", processor->isLoaded());
    // The layer runs the copy saved with the version; this says whether the
    // file on disk still matches it (docs/lua-scripts.md).
    item->setProperty("fileStatus", juce::String(LuaPlugin::fileStatusName(processor->fileStatus())));
    item->setProperty("enabled", processor->isEnabled());
    {
      auto midiLock = strip.lockMidiState();
      item->setProperty("params", luaParamsToVar(*processor));
    }
    luaProcessors.add(juce::var(item));
  }
  layer->setProperty("luaProcessors", juce::var(luaProcessors));
  return juce::var(layer);
}

juce::var MainComponent::agentMixerSnapshot() {
  auto *snapshot = new juce::DynamicObject();
  snapshot->setProperty("status", agentStatus());
  juce::Array<juce::var> layers;
  for (const auto *strip : mixer_.getAllStrips())
    layers.add(agentLayerSnapshot(*strip));
  snapshot->setProperty("layers", juce::var(layers));
  snapshot->setProperty("groupBuses",
                        juce::JSON::parse(mixer_.groupBusesToJson()));
  snapshot->setProperty("master", mixer_.masterAudio().toJson());
  return juce::var(snapshot);
}

// fiddle_get_mixer. Filters narrow the layers and leave out the buses and
// master, which an unfiltered call returns.
AgentControlServer::Response MainComponent::agentGetMixer(const juce::var &params) {
  const auto query = text(params, "query");
  const auto chairId = text(params, "chairId");
  const auto layerId = text(params, "layerId");
  if (query.isEmpty() && chairId.isEmpty() && layerId.isEmpty())
    return Response::success(agentMixerSnapshot());
  std::map<juce::String, juce::String> chairNames;
  if (auto *repository = db_.getLibraryRoutingRepository())
    for (const auto &chair : repository->listChairs())
      chairNames[juce::String(chair.id)] = juce::String(chair.name);
  auto snapshot = agentMixerSnapshot();
  juce::Array<juce::var> layers;
  for (const auto *strip : mixer_.getAllStrips()) {
    if (chairId.isNotEmpty() && strip->chairId != chairId) continue;
    if (layerId.isNotEmpty() && strip->id != layerId) continue;
    if (!agent::matchesSearch(strip->layerName + " " + strip->library + " " + strip->family + " " +
                                  chairNames[strip->chairId],
                              query))
      continue;
    layers.add(agentLayerSnapshot(*strip));
  }
  auto *filtered = snapshot.getDynamicObject();
  filtered->setProperty("layers", juce::var(layers));
  filtered->removeProperty("groupBuses");
  filtered->removeProperty("master");
  return Response::success(snapshot);
}

juce::var MainComponent::agentLayerResult(const juce::String &stripId,
                                          bool changed) {
  auto *result = new juce::DynamicObject();
  result->setProperty("changed", changed);
  result->setProperty("status", agentStatus());
  if (const auto *strip = mixer_.getStrip(stripId))
    result->setProperty("layer", agentLayerSnapshot(*strip));
  return juce::var(result);
}

void MainComponent::agentMixerChanged() {
  saveAllStripsToDB(false);
  pushMixerState();
  pushGroupBusState();
  pushMasterAudioState();
  scheduleStateRebuild();
}


bool MainComponent::applyAgentUndoRedo(bool redo) {
  const bool changed = redo ? undoManager_.redo() : undoManager_.undo();
  if (!changed)
    return false;
  agentMixerChanged();
  if (undoManager_.isAtSavePoint()) {
    stateManager_.clearDirty();
    broadcastMessage("setDirtyState", false);
    pushConfigStatus();
  }
  return true;
}

juce::var
MainComponent::agentLibrarySetupSnapshot(const juce::String &libraryId) {
  auto *result = new juce::DynamicObject();
  result->setProperty("libraryId", libraryId);
  auto libraries = db_.listLibraries();
  for (const auto &library : libraries) {
    if (library.id != libraryId)
      continue;
    result->setProperty("name", library.name);
    result->setProperty("vendor", library.vendor);
    result->setProperty("variant", library.variant);
    break;
  }

  juce::Array<juce::var> patchValues;
  int guidedCount = 0;
  int configuredCount = 0;
  if (auto *repository = db_.getLibraryRoutingRepository()) {
    for (const auto &patch :
         repository->listPatches(libraryId.toStdString())) {
      auto *item = new juce::DynamicObject();
      item->setProperty("id", juce::String(patch.id));
      item->setProperty("name", juce::String(patch.name));
      item->setProperty("instrumentEntityId",
                        juce::String(patch.instrumentEntityId));
      item->setProperty("family", juce::String(patch.family));
      item->setProperty("character", juce::String(patch.character));
      item->setProperty("pluginUid", patch.pluginUid);
      item->setProperty("expressionMapId",
                        juce::String(patch.expressionMapId));
      item->setProperty("expectedPresetName",
                        juce::String(patch.expectedPresetName));
      item->setProperty("setupComplete", patch.setupComplete);
      item->setProperty("hasPluginState", !patch.pluginState.empty());
      if (!patch.expectedPresetName.empty()) {
        ++guidedCount;
        if (patch.setupComplete)
          ++configuredCount;
      }
      patchValues.add(juce::var(item));
    }
  }
  result->setProperty("patches", juce::var(patchValues));
  result->setProperty("guidedPatchCount", guidedCount);
  result->setProperty("configuredPatchCount", configuredCount);
  result->setProperty("remainingPatchCount", guidedCount - configuredCount);
  result->setProperty("ready", guidedCount > 0 && guidedCount == configuredCount);
  return juce::var(result);
}

AgentControlServer::Response
MainComponent::createAgentGuidedLibrary(const juce::var &params) {
  if (!params.isObject())
    return AgentControlServer::Response::failure(
        "Library parameters must be an object");
  if (const auto blocker = agentSetupBlocker(true); blocker.isNotEmpty())
    return AgentControlServer::Response::failure(blocker);

  const auto name = params["name"].toString().trim();
  const auto pluginValue = params["pluginUid"];
  auto *patchArray = params["patches"].getArray();
  if (name.isEmpty())
    return AgentControlServer::Response::failure("Library name is required");
  if ((!pluginValue.isInt() && !pluginValue.isInt64()) ||
      static_cast<int>(pluginValue) == 0)
    return AgentControlServer::Response::failure(
        "pluginUid must identify an installed instrument plug-in");
  if (!patchArray || patchArray->isEmpty() || patchArray->size() > 256)
    return AgentControlServer::Response::failure(
        "patches must contain between 1 and 256 entries");
  const int pluginUid = static_cast<int>(pluginValue);
  bool pluginAvailable = false;
  for (const auto &description : pluginScanner_.getKnownPluginList().getTypes())
    if (description.uniqueId == pluginUid && description.isInstrument) {
      pluginAvailable = true;
      break;
    }
  if (!pluginAvailable)
    return AgentControlServer::Response::failure(
        "The requested instrument plug-in is not available");

  const auto requestedId = params["libraryId"].toString().trim();
  const auto libraryId =
      requestedId.isNotEmpty() ? requestedId : juce::Uuid().toString();
  for (const auto &library : db_.listLibraries())
    if (library.id == libraryId)
      return AgentControlServer::Response::failure(
          "A library with this ID already exists");

  std::set<std::string> patchIds;
  std::set<juce::String> sourceDirectories;
  std::vector<std::pair<juce::File, std::string>> mapSources;
  std::vector<fiddle::LibraryPatchRow> patches;
  patches.reserve(static_cast<std::size_t>(patchArray->size()));
  int position = 0;
  for (const auto &value : *patchArray) {
    auto *object = value.getDynamicObject();
    if (!object)
      return AgentControlServer::Response::failure(
          "Every patch must be an object");
    fiddle::LibraryPatchRow patch;
    patch.id = object->getProperty("id").toString().trim().toStdString();
    if (patch.id.empty())
      patch.id = juce::Uuid().toString().toStdString();
    if (!patchIds.insert(patch.id).second)
      return AgentControlServer::Response::failure(
          "Patch IDs must be unique");
    patch.libraryId = libraryId.toStdString();
    patch.position = position++;
    patch.name =
        object->getProperty("name").toString().trim().toStdString();
    patch.instrumentEntityId = object->getProperty("instrumentEntityId")
                                   .toString()
                                   .trim()
                                   .toStdString();
    patch.family =
        object->getProperty("family").toString().trim().toStdString();
    patch.character =
        object->getProperty("character").toString().trim().toStdString();
    patch.pluginUid = pluginUid;
    patch.expressionMapId = object->getProperty("expressionMapId")
                                .toString()
                                .trim()
                                .toStdString();
    patch.expectedPresetName = object->getProperty("expectedPresetName")
                                   .toString()
                                   .trim()
                                   .toStdString();
    patch.setupComplete = false;
    const auto mapPath =
        object->getProperty("expressionMapPath").toString().trim();
    if (patch.name.empty() || patch.expressionMapId.empty() ||
        patch.expectedPresetName.empty() || mapPath.isEmpty())
      return AgentControlServer::Response::failure(
          "Each patch needs a name, expressionMapId, expressionMapPath, and expectedPresetName");
    if (!patch.character.empty() && patch.character != "solo" &&
        patch.character != "section" && patch.character != "ensemble" &&
        patch.character != "overlay")
      return AgentControlServer::Response::failure(
          "Patch character must be solo, section, ensemble, overlay, or empty");

    if (!patch.instrumentEntityId.empty()) {
      const bool found = std::any_of(
          instrumentBrowser_.getInstruments().begin(),
          instrumentBrowser_.getInstruments().end(),
          [&patch](const auto &instrument) {
            return instrument.entityID.toStdString() ==
                   patch.instrumentEntityId;
          });
      if (!found)
        return AgentControlServer::Response::failure(
            "Unknown Dorico instrument ID: " +
            juce::String(patch.instrumentEntityId));
    }

    const juce::File mapFile(mapPath);
    if (!mapFile.existsAsFile() ||
        !mapFile.hasFileExtension("doricolib"))
      return AgentControlServer::Response::failure(
          "Expression map file is unavailable: " + mapPath);
    const auto metadata = fiddle::scanExpressionMapMetadata(mapFile);
    const bool hasRequestedMap = std::any_of(
        metadata.begin(), metadata.end(), [&patch](const auto &entry) {
          return entry.entityID == patch.expressionMapId;
        });
    if (!hasRequestedMap)
      return AgentControlServer::Response::failure(
          "Expression map ID was not found in: " + mapPath);
    sourceDirectories.insert(mapFile.getParentDirectory().getFullPathName());
    mapSources.emplace_back(mapFile, patch.expressionMapId);
    patches.push_back(std::move(patch));
  }

  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return AgentControlServer::Response::failure(
        "The library catalog is not available");
  fiddle::LibraryCatalogSnapshot target;
  target.id = libraryId.toStdString();
  target.name = name.toStdString();
  target.vendor = params["vendor"].toString().trim().toStdString();
  target.variant = params["variant"].toString().trim().toStdString();
  target.exists = true;
  target.patches = patches;
  // The patches' maps join Fiddle's maps (docs/maps.md).
  std::vector<std::unique_ptr<fiddle::UndoableAction>> actions;
  std::vector<agent::MapImport> imports;
  for (const auto &[mapFile, entityId] : mapSources)
    imports.push_back({LibraryMapKind::expression, entityId, mapFile.getFullPathName()});
  MapFiles files;
  juce::String importError;
  if (!appendMapImports(*repository, files, imports, actions, importError))
    return AgentControlServer::Response::failure(importError);
  actions.push_back(std::make_unique<fiddle::LibraryCatalogAction>(*repository, target));
  if (!libraryUndoManager_.perform(std::make_unique<fiddle::CompoundAction>(
          "Save library: " + name, std::move(actions))))
    return AgentControlServer::Response::failure(
        "Fiddle could not create the guided library");
  // Maps it imported reach the layers using them now, like any map import.
  syncLayersToChangedMaps();
  pushLibraryMaps();

  for (const auto &path : sourceDirectories) {
    const juce::File directory(path);
    xmapLibrary_.scanDirectory(directory);
    rememberExpressionMapSourceDirectory(directory);
  }
  pushLibraryCatalogHistory();
  pushLayerCatalog();
  return AgentControlServer::Response::success(
      agentLibrarySetupSnapshot(libraryId));
}


AgentControlServer::Response
MainComponent::agentUndoRedo(const juce::var &params, bool redo) {
  const auto history = text(params, "history").isEmpty() ? juce::String("project")
                                                          : text(params, "history");
  if (history != "project" && history != "library")
    return Response::failure("history must be project or library");
  // Either history can hold setup steps (chairs, layers, libraries), which the
  // setup tools refuse during playback, so Undo and Redo do too.
  if (isTransportStarted_.load(std::memory_order_relaxed))
    return Response::failure("Stop Dorico playback before undoing or redoing: a step may "
                             "add or remove chairs, layers or libraries");
  // A short answer: which step moved and where each history now stands. Read
  // the result with fiddle_get_chairs, fiddle_get_mixer or fiddle_get_library.
  const auto finish = [&](bool changed, const juce::String &step) {
    auto *result = new juce::DynamicObject();
    result->setProperty("history", history);
    result->setProperty("changed", changed);
    result->setProperty(redo ? "redone" : "undone", changed ? step : juce::String());
    result->setProperty("projectHistory", historyJson(undoManager_));
    result->setProperty("libraryHistory", historyJson(libraryUndoManager_));
    return Response::success(juce::var(result));
  };
  if (history == "project") {
    const auto step = redo ? undoManager_.redoDescription() : undoManager_.undoDescription();
    return finish(applyAgentUndoRedo(redo), step);
  }
  // Library Manager allows catalog Undo only outside its editor; an unsaved
  // draft would overwrite the restored catalog when saved.
  if (libraryDraftDirty_)
    return Response::failure("Library Manager has unsaved edits. Ask the user to save or "
                             "discard them first.");
  auto &manager = libraryUndoManager_;
  const auto step = redo ? manager.redoDescription() : manager.undoDescription();
  bool changed = false;
  if (redo ? manager.canRedo() : manager.canUndo()) {
    changed = redo ? manager.redo() : manager.undo();
    if (!changed)
      return Response::failure("Catalog history could not be applied. A patch may now be "
                               "used by a layer. History has been kept.");
    catalogChangedByAgent({});
  }
  return finish(changed, step);
}

void MainComponent::catalogChangedByAgent(const juce::String &libraryId) {
  pushLibraryCatalogHistory();
  pushLayerCatalog();
  syncLayersToChangedMaps();
  pushLibraryMaps();
  pushMixerState(false);
  pushLibraryLayerStatus();
  broadcastMessage("libraryCatalogChanged", libraryId);
}

AgentControlServer::Response
MainComponent::agentSetLayerControl(const juce::String &method,
                                    const juce::var &params) {
  if (!params.isObject())
    return Response::failure("Control parameters must be an object");
  const auto stripId = params["stripId"].toString();
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return Response::failure("Layer not found: " + stripId);

  bool changed = false;
  if (method == "layer.setGain") {
    const auto value = params["gainDb"];
    if (!value.isDouble() && !value.isInt() && !value.isInt64())
      return Response::failure("gainDb must be a number");
    const auto gainDb = static_cast<float>(static_cast<double>(value));
    if (!std::isfinite(gainDb) || gainDb < -120.0f || gainDb > 6.0f)
      return Response::failure("gainDb must be between -120 and 6");
    if (strip->gainDb() != gainDb)
      changed = mixerCommandService_->setGain(stripId, gainDb);
  } else {
    const auto property = method == "layer.setMute" ? "muted" : "soloed";
    const auto value = params[property];
    if (!value.isBool())
      return Response::failure(juce::String(property) + " must be a boolean");
    const bool target = static_cast<bool>(value);
    const bool current = method == "layer.setMute" ? strip->isMuted()
                                                    : strip->isSoloed();
    if (current != target) {
      changed = method == "layer.setMute"
                    ? mixerCommandService_->setMute(stripId, target)
                    : mixerCommandService_->setSolo(stripId, target);
    }
  }

  if (changed)
    agentMixerChanged();
  return Response::success(agentLayerResult(stripId, changed));
}

AgentControlServer::Response
MainComponent::agentInspectLibrarySources(const juce::var &params) {
  const auto kind = libraryMapKindFromName(
      (text(params, "kind").isEmpty() ? juce::String("expression") : text(params, "kind")).toStdString());
  if (!kind)
    return Response::failure("kind must be expression or percussion");
  // No directory: the folders Dorico loads libraries from.
  const auto directoryPath = text(params, "expressionMapDirectory");
  juce::Array<juce::File> files;
  juce::String searched;
  if (directoryPath.isNotEmpty()) {
    const juce::File directory(directoryPath);
    if (!directory.isDirectory())
      return Response::failure("Expression-map directory was not found");
    files = directory.findChildFiles(juce::File::findFiles, true, "*.doricolib");
    searched = directory.getFullPathName();
  } else {
    juce::StringArray folders;
    for (const auto &source : doricoLibraryAdditionDirectories()) {
      if (!source.isDirectory()) continue;
      folders.add(source.getFullPathName());
      for (const auto &file : source.findChildFiles(juce::File::findFiles, true, "*.doricolib"))
        if (!file.getFileName().startsWith("Fiddle")) // Fiddle's bridge maps
          files.add(file);
    }
    if (folders.isEmpty())
      return Response::failure("Dorico's library folders were not found. Give "
                               "expressionMapDirectory instead.");
    searched = folders.joinIntoString("; ");
  }
  const int limit = limitParam(params, "maxMaps", 500, 500);
  const auto mapQuery = text(params, "mapQuery");
  if (mapQuery.isEmpty() && files.size() > 2000)
    return Response::failure(
        "This directory contains " + juce::String(files.size()) +
        " .doricolib files. Supply mapQuery or choose a narrower directory.");
  juce::Array<juce::File> candidateFiles;
  for (const auto &file : files)
    if (mapQuery.isEmpty() || file.getFullPathName().containsIgnoreCase(mapQuery))
      candidateFiles.add(file);
  auto *repository = db_.getLibraryRoutingRepository();
  juce::Array<juce::var> maps;
  int visitedFiles = 0, duplicates = 0;
  bool moreMaps = false; // unique maps left out at the limit
  // One entry per map ID. Dorico 5 and 6 folders, backups and copies often
  // hold the same map; keep the highest version and count the other copies.
  std::map<juce::String, int> mapIndex;
  const auto addMap = [&](const juce::String &name, const juce::String &entityId, int version,
                          const juce::String &creator, const juce::String &pluginNames,
                          const juce::File &file) {
    if (const auto found = mapIndex.find(entityId); found != mapIndex.end()) {
      ++duplicates;
      auto *existing = maps.getReference(found->second).getDynamicObject();
      existing->setProperty("copies", static_cast<int>(existing->getProperty("copies")) + 1);
      if (version > static_cast<int>(existing->getProperty("version"))) {
        existing->setProperty("name", name);
        existing->setProperty("version", version);
        existing->setProperty("creator", creator);
        existing->setProperty("pluginNames", pluginNames);
        existing->setProperty("sourcePath", file.getFullPathName());
      }
      return;
    }
    // Past the limit, keep reading only for newer copies of listed maps.
    if (maps.size() >= limit) {
      moreMaps = true;
      return;
    }
    mapIndex[entityId] = maps.size();
    auto *item = new juce::DynamicObject();
    item->setProperty("kind", juce::String(libraryMapKindName(*kind)));
    item->setProperty("name", name);
    item->setProperty("entityId", entityId);
    item->setProperty("version", version);
    item->setProperty("creator", creator);
    item->setProperty("pluginNames", pluginNames);
    item->setProperty("sourcePath", file.getFullPathName());
    item->setProperty("inFiddle", repository && repository->getMap(*kind, entityId.toStdString()).has_value());
    item->setProperty("copies", 1);
    maps.add(juce::var(item));
  };
  for (const auto &file : candidateFiles) {
    ++visitedFiles;
    if (*kind == LibraryMapKind::expression) {
      for (const auto &metadata : scanExpressionMapMetadata(file)) {
        addMap(juce::String(metadata.name), juce::String(metadata.entityID), metadata.version,
               juce::String(metadata.creator), juce::String(metadata.pluginNames), file);
      }
    } else {
      juce::String error;
      for (const auto &map : readLibraryMapFile(*kind, file, error)) {
        addMap(juce::String(map.name), juce::String(map.entityId), map.version,
               juce::String(map.creator), juce::String(map.pluginNames), file);
      }
    }
  }
  const auto query = text(params, "pluginQuery");
  juce::Array<juce::var> plugins;
  for (const auto &description : pluginScanner_.getKnownPluginList().getTypes()) {
    if (!description.isInstrument ||
        (query.isNotEmpty() && !description.name.containsIgnoreCase(query) &&
         !description.manufacturerName.containsIgnoreCase(query)))
      continue;
    auto *item = new juce::DynamicObject();
    item->setProperty("uid", description.uniqueId);
    item->setProperty("name", description.name);
    item->setProperty("manufacturer", description.manufacturerName);
    item->setProperty("format", description.pluginFormatName);
    plugins.add(juce::var(item));
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("directory", searched);
  result->setProperty("kind", juce::String(libraryMapKindName(*kind)));
  result->setProperty("mapQuery", mapQuery);
  result->setProperty("maps", juce::var(maps));
  // Files found, files whose path matches mapQuery, and files read (all of
  // them, so a newer copy is found even past the limit); maps are unique by
  // ID, with duplicateCount extra copies seen.
  result->setProperty("mapFileCount", files.size());
  result->setProperty("matchedMapFileCount", candidateFiles.size());
  result->setProperty("scannedFileCount", visitedFiles);
  result->setProperty("uniqueMapCount", maps.size());
  result->setProperty("duplicateCount", duplicates);
  result->setProperty("truncated", moreMaps);
  result->setProperty("plugins", juce::var(plugins));
  return Response::success(juce::var(result));
}

AgentControlServer::Response
MainComponent::agentOpenGuidedLibrary(const juce::var &params) {
  const auto libraryId = text(params, "libraryId");
  if (libraryId.isEmpty())
    return Response::failure("libraryId is required");
  const auto libraries = db_.listLibraries();
  if (std::none_of(libraries.begin(), libraries.end(),
                   [&](const auto &library) { return library.id == libraryId; }))
    return Response::failure("Library not found");
  pendingGuidedLibraryId_ = libraryId;
  showLibraryManagerWindow();
  if (libraryManagerWindowLoaded_) {
    broadcastMessage("openGuidedLibrary", libraryId);
    pendingGuidedLibraryId_.clear();
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("opened", true);
  result->setProperty("libraryId", libraryId);
  return Response::success(juce::var(result));
}

// ── Discovery ────────────────────────────────────────────────────────────

AgentControlServer::Response
MainComponent::agentSearchDoricoInstruments(const juce::var &params) {
  const auto &instruments = instrumentBrowser_.getInstruments();
  if (instruments.empty())
    return Response::failure("Dorico's instrument list is unavailable. Is Dorico installed?");
  const int limit = limitParam(params, "limit", 25, 200);
  const auto matches = agent::searchInstruments(
      instruments, text(params, "query"), text(params, "family"),
      flag(params, "includeVariants"), limit + 1);
  juce::Array<juce::var> result;
  for (std::size_t index = 0; index < matches.size() && static_cast<int>(index) < limit; ++index) {
    const auto &instrument = *matches[index];
    auto *item = new juce::DynamicObject();
    item->setProperty("entityId", instrument.entityID);
    item->setProperty("name", instrument.name);
    item->setProperty("family", instrument.family);
    item->setProperty("musicXMLSoundID", instrument.musicXMLSoundID);
    item->setProperty("isVariant", !instrument.isDefault);
    if (instrument.kitInstruments.empty()) {
      item->setProperty("suggestedRole", agent::suggestedRole(instrument));
    } else {
      // A Dorico kit becomes a Fiddle kit: one chair per instrument.
      juce::Array<juce::var> members;
      for (const auto &id : instrument.kitInstruments) members.add(id);
      item->setProperty("kitInstruments", juce::var(members));
    }
    result.add(juce::var(item));
  }
  auto *response = new juce::DynamicObject();
  response->setProperty("instruments", juce::var(result));
  response->setProperty("truncated", static_cast<int>(matches.size()) > limit);
  return Response::success(juce::var(response));
}

AgentControlServer::Response MainComponent::agentListPlugins(const juce::var &params) {
  const auto query = text(params, "query");
  const bool instrumentsOnly = flag(params, "instrumentsOnly");
  const int limit = limitParam(params, "limit", 200, 1000);
  const auto all = juce::JSON::parse(pluginScanner_.getPluginListAsJson());
  juce::Array<juce::var> plugins;
  int matched = 0;
  if (const auto *list = all.getArray())
    for (const auto &plugin : *list) {
      if (instrumentsOnly && !static_cast<bool>(plugin["isInstrument"])) continue;
      if (query.isNotEmpty() &&
          !agent::matchesSearch(plugin["name"].toString() + " " + plugin["manufacturer"].toString(), query))
        continue;
      if (++matched <= limit) plugins.add(plugin);
    }
  auto *result = new juce::DynamicObject();
  result->setProperty("scanning", pluginScanner_.isScanning());
  result->setProperty("knownPluginCount", pluginScanner_.getPluginCount());
  result->setProperty("matchedCount", matched);
  result->setProperty("truncated", matched > limit);
  result->setProperty("plugins", juce::var(plugins));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentScanPlugins(const juce::var &params) {
  if (!pluginJsHandlers_)
    return Response::failure("The plug-in scanner is not available");
  const bool fullRescan = flag(params, "fullRescan");
  const bool started = pluginJsHandlers_->beginScan(fullRescan);
  auto *result = new juce::DynamicObject();
  result->setProperty("started", started);
  result->setProperty("scanning", pluginScanner_.isScanning());
  result->setProperty("message", started
      ? juce::String("Scan started. Call fiddle_list_plugins until scanning is false.")
      : juce::String("A scan is already running. Call fiddle_list_plugins until scanning is false."));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentListMaps(const juce::var &params) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The library catalog is not available");
  std::optional<LibraryMapKind> kind;
  if (text(params, "kind").isNotEmpty()) {
    kind = libraryMapKindFromName(text(params, "kind").toStdString());
    if (!kind) return Response::failure("kind must be expression or percussion");
  }
  const auto query = text(params, "query");
  const bool favoritesOnly = flag(params, "favoritesOnly");
  // Percussion maps that cover a Dorico instrument; expression maps don't
  // list instruments, so this filter leaves only percussion maps.
  const auto instrumentId = text(params, "instrumentEntityId").toStdString();
  const int limit = limitParam(params, "limit", 100, 1000);
  std::map<std::pair<LibraryMapKind, std::string>, int> uses;
  for (const auto &use : repository->listMapUsage()) ++uses[{use.kind, use.entityId}];
  juce::Array<juce::var> maps;
  int matched = 0;
  for (const auto &row : repository->listMaps(kind)) {
    if (favoritesOnly && !row.favorite) continue;
    const auto definition = juce::String::fromUTF8(row.xml.c_str());
    ExpressionMapLabels labels;
    std::shared_ptr<const PercussionMap> percussion;
    if (row.kind == LibraryMapKind::expression) labels = expressionMapLabels(definition);
    else percussion = compilePercussionMap(definition);
    if (!instrumentId.empty()) {
      if (!percussion) continue;
      if (!agent::coversInstrument(percussion->instrumentsInOrder(), instrumentId)) continue;
    }
    if (query.isNotEmpty() &&
        !agent::matchesSearch(juce::String(row.name) + " " + juce::String(row.entityId) + " " +
                             juce::String(labels.creator) + " " + juce::String(labels.pluginNames) +
                             " " + juce::String(row.sourcePath),
                         query))
      continue;
    if (++matched > limit) continue;
    auto *item = new juce::DynamicObject();
    item->setProperty("kind", juce::String(libraryMapKindName(row.kind)));
    item->setProperty("entityId", juce::String(row.entityId));
    item->setProperty("name", juce::String(row.name));
    item->setProperty("favorite", row.favorite);
    item->setProperty("sourcePath", juce::String(row.sourcePath));
    item->setProperty("usedBy", uses[{row.kind, row.entityId}]);
    if (row.kind == LibraryMapKind::expression) {
      item->setProperty("creator", juce::String(labels.creator));
      item->setProperty("pluginNames", juce::String(labels.pluginNames));
    } else {
      juce::Array<juce::var> instruments;
      if (percussion)
        for (const auto &id : percussion->instrumentsInOrder()) instruments.add(juce::String(id));
      item->setProperty("instruments", juce::var(instruments));
    }
    maps.add(juce::var(item));
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("maps", juce::var(maps));
  result->setProperty("matchedCount", matched);
  result->setProperty("truncated", matched > limit);
  return Response::success(juce::var(result));
}

// ── Libraries ────────────────────────────────────────────────────────────

AgentControlServer::Response MainComponent::agentListLibraries(const juce::var &) {
  auto *repository = db_.getLibraryRoutingRepository();
  juce::Array<juce::var> libraries;
  for (const auto &library : db_.listLibraries()) {
    int patches = 0, waiting = 0, layers = 0;
    if (repository)
      for (const auto &patch : repository->listPatches(library.id.toStdString())) {
        ++patches;
        if (!patch.expectedPresetName.empty() && !patch.setupComplete) ++waiting;
        layers += repository->patchUsageCount(patch.id);
      }
    auto *item = new juce::DynamicObject();
    item->setProperty("id", library.id);
    item->setProperty("name", library.name);
    item->setProperty("vendor", library.vendor);
    item->setProperty("variant", library.variant);
    item->setProperty("patchCount", patches);
    item->setProperty("patchesAwaitingPresetCount", waiting);
    item->setProperty("layerCount", layers);
    libraries.add(juce::var(item));
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("libraries", juce::var(libraries));
  result->setProperty("libraryHistory", historyJson(libraryUndoManager_));
  return Response::success(juce::var(result));
}

juce::var MainComponent::agentLibrarySnapshot(const juce::String &libraryId) {
  auto *repository = db_.getLibraryRoutingRepository();
  const auto library = repository ? repository->captureLibrary(libraryId.toStdString())
                                   : std::nullopt;
  if (!library || !library->exists)
    return {};
  std::map<int, juce::String> pluginNames;
  for (const auto &description : pluginScanner_.getKnownPluginList().getTypes())
    pluginNames[description.uniqueId] = description.name;
  const auto mapName = [repository](LibraryMapKind kind, const std::string &id) {
    if (id.empty()) return juce::String();
    const auto map = repository->getMap(kind, id);
    return map ? juce::String(map->name) : juce::String("(missing map)");
  };
  auto *result = new juce::DynamicObject();
  result->setProperty("id", libraryId);
  result->setProperty("name", juce::String(library->name));
  result->setProperty("vendor", juce::String(library->vendor));
  result->setProperty("variant", juce::String(library->variant));
  juce::Array<juce::var> patches;
  for (const auto &patch : library->patches) {
    auto *item = new juce::DynamicObject();
    item->setProperty("id", juce::String(patch.id));
    item->setProperty("name", juce::String(patch.name));
    item->setProperty("instrumentEntityId", juce::String(patch.instrumentEntityId));
    item->setProperty("family", juce::String(patch.family));
    item->setProperty("character", juce::String(patch.character));
    item->setProperty("pluginUid", patch.pluginUid);
    item->setProperty("pluginName", pluginNames.count(patch.pluginUid)
                                        ? pluginNames[patch.pluginUid]
                                        : juce::String("(not installed)"));
    item->setProperty("expressionMapId", juce::String(patch.expressionMapId));
    item->setProperty("expressionMapName", mapName(LibraryMapKind::expression, patch.expressionMapId));
    item->setProperty("percussionMapId", juce::String(patch.percussionMapId));
    item->setProperty("percussionMapName", mapName(LibraryMapKind::percussion, patch.percussionMapId));
    auto *playAs = new juce::DynamicObject();
    for (const auto &[instrument, mapInstrument] : patch.percussionPlayAs)
      playAs->setProperty(juce::Identifier(juce::String(instrument)), juce::String(mapInstrument));
    item->setProperty("percussionPlayAs", juce::var(playAs));
    item->setProperty("expectedPresetName", juce::String(patch.expectedPresetName));
    item->setProperty("awaitingPresetCapture", !patch.expectedPresetName.empty() && !patch.setupComplete);
    item->setProperty("hasPluginState", !patch.pluginState.empty());
    item->setProperty("layerCount", repository->patchUsageCount(patch.id));
    patches.add(juce::var(item));
  }
  result->setProperty("patches", juce::var(patches));
  return juce::var(result);
}

// fiddle_find_patches: patches across every library. An instrument (given, or
// a chair's) matches patches classified as it or as an instrument sharing its
// Dorico sound (its variants, such as Horn in F and Horn in B-flat), and
// patches whose percussion map covers it, whatever their classification.
AgentControlServer::Response MainComponent::agentFindPatches(const juce::var &params) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The library catalog is not available");
  const auto query = text(params, "query");
  auto instrumentId = text(params, "instrumentEntityId");
  if (const auto chairId = text(params, "chairId"); chairId.isNotEmpty()) {
    const auto chair = repository->getChair(chairId.toStdString());
    if (!chair) return Response::failure("Chair not found");
    if (instrumentId.isNotEmpty() && instrumentId != juce::String(chair->instrumentEntityId))
      return Response::failure("Give chairId or instrumentEntityId, not both");
    instrumentId = juce::String(chair->instrumentEntityId);
  }
  const bool byPlugin = params.isObject() && (params["pluginUid"].isInt() || params["pluginUid"].isInt64());
  const int pluginUid = byPlugin ? static_cast<int>(params["pluginUid"]) : 0;
  const int limit = limitParam(params, "limit", 50, 500);
  std::map<juce::String, const BrowsableInstrument *> instruments;
  for (const auto &instrument : instrumentBrowser_.getInstruments())
    instruments[instrument.entityID] = &instrument;
  juce::String wantedSound;
  if (instrumentId.isNotEmpty()) {
    const auto found = instruments.find(instrumentId);
    if (found == instruments.end())
      return Response::failure("Unknown Dorico instrument ID: " + instrumentId);
    wantedSound = found->second->musicXMLSoundID;
  }
  std::map<std::string, juce::String> libraryNames;
  for (const auto &library : db_.listLibraries())
    libraryNames[library.id.toStdString()] = library.name;
  std::map<int, juce::String> pluginNames;
  for (const auto &description : pluginScanner_.getKnownPluginList().getTypes())
    pluginNames[description.uniqueId] = description.name;
  const auto mapName = [repository](LibraryMapKind kind, const std::string &id) {
    if (id.empty()) return juce::String();
    const auto map = repository->getMap(kind, id);
    return map ? juce::String(map->name) : juce::String("(missing map)");
  };
  // Percussion maps' instruments, compiled once per map.
  std::map<std::string, std::vector<std::string>> mapInstruments;
  const auto percussionCovers = [&](const std::string &mapId) {
    if (mapId.empty()) return false;
    auto found = mapInstruments.find(mapId);
    if (found == mapInstruments.end()) {
      std::vector<std::string> listed;
      if (const auto row = repository->getMap(LibraryMapKind::percussion, mapId))
        if (const auto map = compilePercussionMap(juce::String::fromUTF8(row->xml.c_str())))
          listed = map->instrumentsInOrder();
      found = mapInstruments.emplace(mapId, std::move(listed)).first;
    }
    return agent::coversInstrument(found->second, instrumentId.toStdString());
  };
  juce::Array<juce::var> patches;
  int matched = 0;
  for (const auto &patch : repository->listPatches()) {
    const auto instrument = instruments.find(juce::String(patch.instrumentEntityId));
    const auto instrumentName = instrument != instruments.end() ? instrument->second->name : juce::String();
    if (byPlugin && patch.pluginUid != pluginUid) continue;
    juce::String reason;
    if (instrumentId.isNotEmpty()) {
      if (juce::String(patch.instrumentEntityId) == instrumentId)
        reason = "instrument";
      else if (wantedSound.isNotEmpty() && instrument != instruments.end() &&
               instrument->second->musicXMLSoundID == wantedSound)
        reason = "variant";
      else if (percussionCovers(patch.percussionMapId))
        reason = "percussion map";
      else
        continue;
    }
    const auto library = libraryNames[patch.libraryId];
    const auto plugin = pluginNames.count(patch.pluginUid) ? pluginNames[patch.pluginUid]
                                                            : juce::String("(not installed)");
    if (!agent::matchesSearch(juce::String(patch.name) + " " + library + " " + instrumentName + " " +
                                  juce::String(patch.instrumentEntityId) + " " + juce::String(patch.family) +
                                  " " + juce::String(patch.character) + " " + plugin,
                              query))
      continue;
    if (++matched > limit) continue;
    auto *item = new juce::DynamicObject();
    item->setProperty("id", juce::String(patch.id));
    item->setProperty("name", juce::String(patch.name));
    item->setProperty("libraryId", juce::String(patch.libraryId));
    item->setProperty("libraryName", library);
    item->setProperty("instrumentEntityId", juce::String(patch.instrumentEntityId));
    item->setProperty("instrumentName", instrumentName);
    item->setProperty("character", juce::String(patch.character));
    item->setProperty("pluginUid", patch.pluginUid);
    item->setProperty("pluginName", plugin);
    item->setProperty("expressionMapName", mapName(LibraryMapKind::expression, patch.expressionMapId));
    item->setProperty("percussionMapName", mapName(LibraryMapKind::percussion, patch.percussionMapId));
    item->setProperty("awaitingPresetCapture", !patch.expectedPresetName.empty() && !patch.setupComplete);
    item->setProperty("hasPluginState", !patch.pluginState.empty());
    item->setProperty("layerCount", repository->patchUsageCount(patch.id));
    if (reason.isNotEmpty()) item->setProperty("matchedBy", reason);
    patches.add(juce::var(item));
  }
  auto *result = new juce::DynamicObject();
  if (instrumentId.isNotEmpty()) result->setProperty("instrumentEntityId", instrumentId);
  result->setProperty("patches", juce::var(patches));
  result->setProperty("matchedCount", matched);
  result->setProperty("truncated", matched > limit);
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentGetLibrary(const juce::var &params) {
  const auto libraryId = text(params, "libraryId");
  if (libraryId.isEmpty())
    return Response::failure("libraryId is required");
  const auto snapshot = agentLibrarySnapshot(libraryId);
  if (snapshot.isVoid())
    return Response::failure("Library not found");
  return Response::success(snapshot);
}

AgentControlServer::Response MainComponent::agentSaveLibrary(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(true); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The library catalog is not available");
  const auto requestedId = text(params, "libraryId");
  std::optional<LibraryCatalogSnapshot> existing;
  if (requestedId.isNotEmpty()) {
    existing = repository->captureLibrary(requestedId.toStdString());
    if (!existing)
      return Response::failure("The library catalog could not be read");
  }

  MapFiles files;
  agent::LibraryEditChecks checks;
  checks.instrumentExists = [this](const std::string &id) {
    const auto &instruments = instrumentBrowser_.getInstruments();
    return std::any_of(instruments.begin(), instruments.end(),
                       [&](const auto &instrument) { return instrument.entityID.toStdString() == id; });
  };
  checks.instrumentPluginAvailable = [this](int uid) {
    for (const auto &description : pluginScanner_.getKnownPluginList().getTypes())
      if (description.uniqueId == uid && description.isInstrument) return true;
    return false;
  };
  checks.mapAvailable = [&](LibraryMapKind kind, const std::string &id, const juce::String &path) {
    return path.isNotEmpty() ? files.find(kind, path, id) != nullptr
                             : repository->getMap(kind, id).has_value();
  };
  checks.percussionMapInstruments = [&](const std::string &id, const juce::String &path)
      -> std::optional<std::vector<std::string>> {
    // Fiddle's copy wins over the file: an import keeps a map Fiddle has.
    juce::String xml;
    if (const auto row = repository->getMap(LibraryMapKind::percussion, id))
      xml = juce::String::fromUTF8(row->xml.c_str());
    else if (const auto *candidate = path.isNotEmpty() ? files.find(LibraryMapKind::percussion, path, id) : nullptr)
      xml = candidate->definitionXml;
    const auto map = xml.isNotEmpty() ? compilePercussionMap(xml) : nullptr;
    if (!map) return std::nullopt;
    return map->instrumentsInOrder();
  };
  checks.patchUsageCount = [repository](const std::string &id) { return repository->patchUsageCount(id); };

  auto edit = agent::mergeLibraryEdit(existing, params, checks,
                                      [] { return juce::Uuid().toString().toStdString(); });
  if (!edit.ok)
    return Response::failure(edit.error);

  std::vector<std::unique_ptr<UndoableAction>> actions;
  juce::String importError;
  if (!appendMapImports(*repository, files, edit.imports, actions, importError))
    return Response::failure(importError);
  auto save = std::make_unique<LibraryCatalogAction>(*repository, edit.target);
  const bool changed = !actions.empty() || !save->isNoOp();
  if (!save->isNoOp())
    actions.push_back(std::move(save));
  if (changed && !libraryUndoManager_.perform(std::make_unique<CompoundAction>(
                     "Save library: " + juce::String(edit.target.name), std::move(actions))))
    return Response::failure("Fiddle could not save the library. A removed patch may still "
                             "be used by a layer.");
  const auto libraryId = juce::String(edit.target.id);
  if (changed) {
    for (const auto &import : edit.imports)
      if (import.kind == LibraryMapKind::expression) {
        const auto directory = juce::File(import.path).getParentDirectory();
        xmapLibrary_.scanDirectory(directory);
        rememberExpressionMapSourceDirectory(directory);
      }
    catalogChangedByAgent(libraryId);
  }

  auto *result = new juce::DynamicObject();
  result->setProperty("changed", changed);
  result->setProperty("created", edit.created);
  juce::Array<juce::var> added;
  for (const auto &id : edit.addedPatchIds) added.add(juce::String(id));
  result->setProperty("addedPatchIds", juce::var(added));
  result->setProperty("library", agentLibrarySnapshot(libraryId));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentDeleteLibrary(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(true); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The library catalog is not available");
  const auto libraryId = text(params, "libraryId");
  const auto existing = repository->captureLibrary(libraryId.toStdString());
  if (libraryId.isEmpty() || !existing || !existing->exists)
    return Response::failure("Library not found");
  int referencedPatches = 0, layers = 0;
  for (const auto &patch : existing->patches)
    if (const int uses = repository->patchUsageCount(patch.id); uses > 0) {
      ++referencedPatches;
      layers += uses;
    }
  if (referencedPatches > 0)
    return Response::failure("Cannot delete this library: " + juce::String(referencedPatches) +
                             (referencedPatches == 1 ? " patch is" : " patches are") + " used by " +
                             juce::String(layers) + (layers == 1 ? " layer" : " layers") +
                             ". Remove those layers first.");
  LibraryCatalogSnapshot target;
  target.id = libraryId.toStdString();
  if (!libraryUndoManager_.perform(std::make_unique<LibraryCatalogAction>(*repository, std::move(target))))
    return Response::failure("Could not delete the library. Nothing was changed.");
  catalogChangedByAgent(libraryId);
  auto *result = new juce::DynamicObject();
  result->setProperty("deleted", true);
  result->setProperty("libraryId", libraryId);
  result->setProperty("libraryHistory", historyJson(libraryUndoManager_));
  return Response::success(juce::var(result));
}

// ── Chairs, kits and layers ──────────────────────────────────────────────

// filter (all optional): chairIds (array) or chairId, query (matchesSearch
// on name, instrument, family and kit), withoutLayers. A filtered answer lists
// only the returned chairs' kits; the template status covers every chair.
juce::var MainComponent::agentChairsSnapshot(const juce::var &filter) {
  auto *repository = db_.getLibraryRoutingRepository();
  std::map<juce::String, const BrowsableInstrument *> instruments;
  for (const auto &instrument : instrumentBrowser_.getInstruments())
    instruments[instrument.entityID] = &instrument;
  std::optional<std::set<juce::String>> wanted;
  if (const auto *ids = filter.isObject() ? filter["chairIds"].getArray() : nullptr) {
    wanted.emplace();
    for (const auto &id : *ids) wanted->insert(id.toString());
  } else if (text(filter, "chairId").isNotEmpty()) {
    wanted = std::set<juce::String>{text(filter, "chairId")};
  }
  const auto query = text(filter, "query");
  const bool withoutLayers = flag(filter, "withoutLayers");
  const bool filtered = wanted.has_value() || query.isNotEmpty() || withoutLayers;
  const auto bridges = agentBridgeChecks();
  std::set<std::string> returnedKits;
  juce::Array<juce::var> chairs;
  std::map<std::string, std::pair<std::string, juce::Array<juce::var>>> kits;
  std::vector<std::string> kitOrder;
  if (repository)
    for (const auto &chair : repository->listChairs()) {
      if (!chair.kitId.empty()) {
        if (!kits.count(chair.kitId)) kitOrder.push_back(chair.kitId);
        auto &kit = kits[chair.kitId];
        kit.first = chair.kitName;
        kit.second.add(juce::String(chair.id));
      }
      const auto instrument = instruments.find(juce::String(chair.instrumentEntityId));
      const auto instrumentName = instrument != instruments.end() ? instrument->second->name
                                                                  : juce::String("(unknown to Dorico)");
      const auto layerRows = repository->listLayers(chair.id);
      if ((wanted && !wanted->count(juce::String(chair.id))) ||
          (withoutLayers && !layerRows.empty()) ||
          !agent::matchesSearch(juce::String(chair.name) + " " + juce::String(chair.instrumentEntityId) +
                                    " " + instrumentName + " " + juce::String(chair.family) + " " +
                                    juce::String(chair.kitName),
                                query))
        continue;
      if (!chair.kitId.empty()) returnedKits.insert(chair.kitId);
      auto *item = new juce::DynamicObject();
      item->setProperty("id", juce::String(chair.id));
      item->setProperty("name", juce::String(chair.name));
      item->setProperty("instrumentEntityId", juce::String(chair.instrumentEntityId));
      item->setProperty("instrumentName", instrumentName);
      item->setProperty("family", juce::String(chair.family));
      item->setProperty("role", roleName(chair.role));
      item->setProperty("ordinal", chair.ordinal);
      item->setProperty("kitId", juce::String(chair.kitId));
      item->setProperty("kitName", juce::String(chair.kitName));
      // The MIDI destination Dorico sends this chair on.
      item->setProperty("midiPort", chair.flatIndex / 16);
      item->setProperty("midiChannel", chair.flatIndex % 16 + 1);
      if (const auto bridge = bridges.find({chair.flatIndex / 16, chair.flatIndex % 16 + 1});
          bridge != bridges.end())
        item->setProperty("bridge", bridge->second);
      juce::Array<juce::var> layers;
      for (const auto &layer : layerRows) {
        auto *row = new juce::DynamicObject();
        row->setProperty("id", juce::String(layer.id));
        row->setProperty("patchId", juce::String(layer.patchId));
        row->setProperty("patchName", juce::String(layer.patchName));
        row->setProperty("libraryName", juce::String(layer.libraryName));
        row->setProperty("pluginUid", layer.pluginUid);
        row->setProperty("missingPatch", !layer.patchId.empty() && !repository->getPatch(layer.patchId));
        layers.add(juce::var(row));
      }
      item->setProperty("layers", juce::var(layers));
      chairs.add(juce::var(item));
    }
  juce::Array<juce::var> kitList;
  for (const auto &id : kitOrder) {
    if (filtered && !returnedKits.count(id)) continue;
    auto *kit = new juce::DynamicObject();
    kit->setProperty("kitId", juce::String(id));
    kit->setProperty("name", juce::String(kits[id].first));
    kit->setProperty("chairIds", juce::var(kits[id].second));
    kitList.add(juce::var(kit));
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("chairs", juce::var(chairs));
  result->setProperty("kits", juce::var(kitList));
  // known: Fiddle has a record of an install; dirty: Dorico's template no
  // longer matches the chairs and needs fiddle_install_playback_template.
  result->setProperty("templateStatus", templateStatus().toJson());
  result->setProperty("history", historyJson(undoManager_));
  return juce::var(result);
}

AgentControlServer::Response MainComponent::agentCreateChairs(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The chair list is not available");
  const auto *entries = params["chairs"].getArray();
  if (!entries || entries->isEmpty() || entries->size() > 128)
    return Response::failure("chairs must contain between 1 and 128 entries");
  std::map<juce::String, const BrowsableInstrument *> instruments;
  for (const auto &instrument : instrumentBrowser_.getInstruments())
    instruments[instrument.entityID] = &instrument;
  const auto existing = repository->listChairs();
  std::vector<ChairRow> chairs;
  for (const auto &entry : *entries) {
    if (!entry.isObject()) return Response::failure("Every chair must be an object");
    const auto entityId = text(entry, "instrumentEntityId");
    const auto found = instruments.find(entityId);
    if (found == instruments.end())
      return Response::failure("Unknown Dorico instrument ID: " + entityId +
                               ". Use fiddle_search_dorico_instruments.");
    const auto &instrument = *found->second;
    if (!instrument.kitInstruments.empty())
      return Response::failure(instrument.name + " is a Dorico kit. Create it with fiddle_create_kit.");
    ChairRow chair;
    chair.id = juce::Uuid().toString().toStdString();
    chair.instrumentEntityId = entityId.toStdString();
    const auto role = text(entry, "role").isEmpty() ? agent::suggestedRole(instrument) : text(entry, "role");
    if (role != "solo" && role != "section")
      return Response::failure("role must be solo or section");
    chair.role = role == "section" ? DoricoRole::section : DoricoRole::solo;
    chair.family = (text(entry, "family").isEmpty() ? instrument.family : text(entry, "family")).toStdString();
    if (const auto kitId = text(entry, "kitId"); kitId.isNotEmpty()) {
      const auto member = std::find_if(existing.begin(), existing.end(),
                                       [&](const auto &c) { return c.kitId == kitId.toStdString(); });
      if (member == existing.end()) return Response::failure("Kit not found: " + kitId);
      chair.kitId = member->kitId;
      chair.kitName = member->kitName;
      chair.family = member->family; // a kit stays together
      chair.role = DoricoRole::solo;
    }
    auto name = text(entry, "name");
    if (name.isEmpty()) {
      // Like the Chairs window: "Flute 1", "Solo Violin 2"…
      const auto sameRole = [&](const ChairRow &c) {
        return c.instrumentEntityId == chair.instrumentEntityId && c.role == chair.role;
      };
      const auto count = std::count_if(existing.begin(), existing.end(), sameRole) +
                         std::count_if(chairs.begin(), chairs.end(), sameRole);
      const auto conventional = agent::suggestedRole(instrument);
      const juce::String prefix = roleName(chair.role) == conventional ? ""
                                  : chair.role == DoricoRole::solo ? "Solo " : "Section ";
      name = prefix + instrument.name + " " + juce::String(static_cast<int>(count) + 1);
    }
    chair.name = name.toStdString();
    chairs.push_back(std::move(chair));
  }

  std::vector<std::string> ids;
  for (const auto &chair : chairs) ids.push_back(chair.id);
  bool ok = false;
  const auto changed = [this](bool success) { chairEditChanged(success); };
  if (chairs.size() == 1) {
    ok = undoManager_.perform(std::make_unique<AddChairAction>(*repository, mixer_, chairs.front(), changed));
  } else {
    std::vector<std::unique_ptr<UndoableAction>> members;
    for (const auto &chair : chairs)
      members.push_back(std::make_unique<AddChairAction>(*repository, mixer_, chair, [](bool) {}));
    ok = undoManager_.perform(std::make_unique<GroupedLayerAction>(
        "Add " + juce::String(static_cast<int>(chairs.size())) + " chairs", std::move(members), changed));
  }
  if (!ok)
    return Response::failure("Could not add the chairs; nothing was changed");
  juce::Array<juce::var> created;
  for (const auto &id : ids) created.add(juce::String(id));
  auto *result = new juce::DynamicObject();
  result->setProperty("createdChairIds", juce::var(created));
  // Just the new chairs; fiddle_get_chairs has the rest.
  result->setProperty("chairs", agentChairsSnapshot(object({{"chairIds", juce::var(created)}})));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentUpdateChair(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto chairId = text(params, "chairId").toStdString();
  if (!repository || !repository->getChair(chairId))
    return Response::failure("Chair not found");
  EditChairsAction::Edit edit{chairId, std::nullopt, std::nullopt, std::nullopt};
  if (const auto name = text(params, "name"); name.isNotEmpty()) edit.name = name.toStdString();
  if (const auto role = text(params, "role"); role.isNotEmpty()) {
    if (role != "solo" && role != "section") return Response::failure("role must be solo or section");
    if (!repository->getChair(chairId)->kitId.empty() && role == "section")
      return Response::failure("A kit's chairs are solo players");
    edit.role = role == "section" ? DoricoRole::section : DoricoRole::solo;
  }
  if (!edit.name && !edit.role)
    return Response::failure("Give a new name or role");
  auto action = std::make_unique<EditChairsAction>(
      *repository, mixer_, std::vector<EditChairsAction::Edit>{edit},
      [this](bool success) { chairEditChanged(success); });
  const bool changed = !action->isNoOp();
  if (changed && !undoManager_.perform(std::move(action)))
    return Response::failure("Could not change the chair; nothing was changed");
  auto *result = new juce::DynamicObject();
  result->setProperty("changed", changed);
  result->setProperty("chairs", agentChairsSnapshot(object({{"chairId", juce::String(chairId)}})));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentDeleteChair(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto chairId = text(params, "chairId").toStdString();
  if (!repository || !repository->getChair(chairId))
    return Response::failure("Chair not found");
  const auto layerCount = static_cast<int>(repository->listLayers(chairId).size());
  if (!undoManager_.perform(std::make_unique<RemoveChairAction>(
          *repository, mixer_, chairId, [this](bool success) { chairEditChanged(success); })))
    return Response::failure("Could not delete the chair; nothing was changed");
  auto *result = new juce::DynamicObject();
  result->setProperty("deleted", true);
  result->setProperty("removedLayerCount", layerCount);
  result->setProperty("chairs", agentChairsSnapshot(object({{"chairIds", juce::var(juce::Array<juce::var>())}})));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentCreateKit(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The chair list is not available");
  const auto kitName = text(params, "name");
  if (kitName.isEmpty())
    return Response::failure("A kit needs a name");
  std::map<juce::String, const BrowsableInstrument *> instruments;
  for (const auto &instrument : instrumentBrowser_.getInstruments())
    instruments[instrument.entityID] = &instrument;

  // Either a Dorico kit's instruments, or a list of instruments.
  std::vector<std::pair<juce::String, juce::String>> members; // entity ID, given name
  if (const auto kit = text(params, "fromDoricoKit"); kit.isNotEmpty()) {
    const auto found = instruments.find(kit);
    if (found == instruments.end() || found->second->kitInstruments.empty())
      return Response::failure(kit + " is not a Dorico kit");
    for (const auto &id : found->second->kitInstruments) members.emplace_back(id, juce::String());
  } else if (const auto *entries = params["instruments"].getArray()) {
    for (const auto &entry : *entries)
      members.emplace_back(text(entry, "instrumentEntityId"), text(entry, "name"));
  }
  if (members.empty() || members.size() > 64)
    return Response::failure("A kit needs between 1 and 64 instruments (instruments or fromDoricoKit)");
  std::vector<juce::String> doricoNames;
  for (const auto &[id, given] : members) {
    const auto found = instruments.find(id);
    if (found == instruments.end())
      return Response::failure("Unknown Dorico instrument ID: " + id);
    if (!found->second->kitInstruments.empty())
      return Response::failure(id + " is itself a kit; use fromDoricoKit");
    doricoNames.push_back(found->second->name);
  }
  const auto defaults = agent::kitChairNames(doricoNames);
  const auto family = text(params, "family").isNotEmpty()
                          ? text(params, "family")
                          : (instruments[members.front().first]->family.isNotEmpty()
                                 ? instruments[members.front().first]->family
                                 : juce::String("percussion"));
  const auto kitId = juce::Uuid().toString().toStdString();
  std::vector<ChairRow> chairs;
  for (std::size_t index = 0; index < members.size(); ++index) {
    ChairRow chair;
    chair.id = juce::Uuid().toString().toStdString();
    chair.instrumentEntityId = members[index].first.toStdString();
    chair.name = (members[index].second.isNotEmpty() ? members[index].second : defaults[index]).toStdString();
    chair.family = family.toStdString();
    chair.role = DoricoRole::solo;
    chair.kitId = kitId;
    chair.kitName = kitName.toStdString();
    chairs.push_back(std::move(chair));
  }
  juce::Array<juce::var> chairIds;
  for (const auto &chair : chairs) chairIds.add(juce::String(chair.id));
  if (!undoManager_.perform(std::make_unique<AddKitAction>(
          *repository, mixer_, std::move(chairs), [this](bool success) { chairEditChanged(success); })))
    return Response::failure("Could not add the kit; nothing was changed");
  auto *result = new juce::DynamicObject();
  result->setProperty("kitId", juce::String(kitId));
  result->setProperty("chairIds", juce::var(chairIds));
  result->setProperty("chairs", agentChairsSnapshot(object({{"chairIds", juce::var(chairIds)}})));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentRenameKit(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto kitId = text(params, "kitId").toStdString();
  const auto name = text(params, "name").toStdString();
  if (name.empty())
    return Response::failure("A kit needs a name");
  std::vector<EditChairsAction::Edit> edits;
  juce::Array<juce::var> kitChairIds;
  if (repository && !kitId.empty())
    for (const auto &chair : repository->listChairs())
      if (chair.kitId == kitId) {
        edits.push_back({chair.id, std::nullopt, std::nullopt, name});
        kitChairIds.add(juce::String(chair.id));
      }
  if (edits.empty())
    return Response::failure("Kit not found");
  auto action = std::make_unique<EditChairsAction>(*repository, mixer_, edits,
                                                   [this](bool success) { chairEditChanged(success); });
  const bool changed = !action->isNoOp();
  if (changed && !undoManager_.perform(std::move(action)))
    return Response::failure("Could not rename the kit; nothing was changed");
  auto *result = new juce::DynamicObject();
  result->setProperty("changed", changed);
  result->setProperty("chairs", agentChairsSnapshot(object({{"chairIds", juce::var(kitChairIds)}})));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentDeleteKit(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto kitId = text(params, "kitId").toStdString();
  int chairCount = 0, layerCount = 0;
  if (repository && !kitId.empty())
    for (const auto &chair : repository->listChairs())
      if (chair.kitId == kitId) {
        ++chairCount;
        layerCount += static_cast<int>(repository->listLayers(chair.id).size());
      }
  if (chairCount == 0)
    return Response::failure("Kit not found");
  if (!undoManager_.perform(std::make_unique<RemoveKitAction>(
          *repository, mixer_, kitId, [this](bool success) { chairEditChanged(success); })))
    return Response::failure("Could not delete the kit; nothing was changed");
  auto *result = new juce::DynamicObject();
  result->setProperty("deleted", true);
  result->setProperty("removedChairCount", chairCount);
  result->setProperty("removedLayerCount", layerCount);
  result->setProperty("chairs", agentChairsSnapshot(object({{"chairIds", juce::var(juce::Array<juce::var>())}})));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentAddLayer(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto patchId = text(params, "patchId").toStdString();
  const auto patch = repository ? repository->getPatch(patchId) : std::nullopt;
  const auto edit = addChairLayers(text(params, "chairId").toStdString(), patchId,
                                   flag(params, "wholeKit"));
  if (!edit.ok)
    return Response::failure(edit.error);
  auto *result = new juce::DynamicObject();
  juce::Array<juce::var> layers;
  for (const auto &id : edit.layerIds)
    if (const auto *strip = mixer_.getStrip(juce::String(id)))
      layers.add(agentLayerSnapshot(*strip));
  result->setProperty("layers", juce::var(layers));
  if (patch && !patch->expectedPresetName.empty() && !patch->setupComplete)
    result->setProperty("warning", "This patch's player preset (" + juce::String(patch->expectedPresetName) +
                                       ") hasn't been captured yet, so the layer starts from " +
                                       (patch->pluginState.empty()
                                            ? juce::String("the player's default state")
                                            : juce::String("the patch's previously captured state, "
                                                           "not that preset")) +
                                       ". Once the preset is captured, update the layer with "
                                       "fiddle_update_layers_from_library.");
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentRemoveLayer(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  const auto edit = removeChairLayers(text(params, "layerId").toStdString(), flag(params, "wholeKit"));
  if (!edit.ok)
    return Response::failure(edit.error);
  juce::Array<juce::var> removed;
  for (const auto &id : edit.layerIds) removed.add(juce::String(id));
  auto *result = new juce::DynamicObject();
  result->setProperty("removedLayerIds", juce::var(removed));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentInstallTemplate(const juce::var &) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  const auto install = installChairPlaybackTemplateNow();
  broadcastMessage("chairTemplateResult", juce::var(install.message)); // the Chairs window
  if (!install.ok)
    return Response::failure(install.message);
  auto *result = new juce::DynamicObject();
  result->setProperty("installed", true);
  result->setProperty("message", install.message);
  result->setProperty("templateStatus", templateStatus().toJson());
  return Response::success(juce::var(result));
}

// ── Finishing and checking setup ───────────────────────────────────────

AgentControlServer::Response MainComponent::agentRefreshLayers(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return Response::failure("The chair list is not available");
  const auto layerId = text(params, "layerId").toStdString();
  const auto patchId = text(params, "patchId").toStdString();
  if (layerId.empty() == patchId.empty())
    return Response::failure("Give layerId or patchId");
  std::vector<LayerRow> layers;
  if (!layerId.empty()) {
    const auto layer = repository->getLayer(layerId);
    if (!layer)
      return Response::failure("Layer not found");
    if (layer->patchId.empty())
      return Response::failure("This layer doesn't come from a library patch");
    // A kit's linked layers update together, as in the layer's details.
    layers = kitLinkedLayers(*layer);
  } else {
    for (const auto &chair : repository->listChairs())
      for (const auto &layer : repository->listLayers(chair.id))
        if (layer.patchId == patchId) layers.push_back(layer);
    if (layers.empty())
      return Response::failure("No layer uses this patch");
  }
  const auto patch = repository->getPatch(layers.front().patchId);
  if (!patch)
    return Response::failure("The layer's library patch no longer exists");
  for (const auto &layer : layers)
    if (!mixer_.getStrip(juce::String(layer.id)))
      return Response::failure("A layer using this patch isn't in the mixer");
  if (!refreshLayersFromPatch(*patch, layers))
    return Response::failure("Could not update the layers from the library");
  pushMixerState();
  scheduleStateRebuild();
  juce::Array<juce::var> ids;
  for (const auto &layer : layers) ids.add(juce::String(layer.id));
  auto *result = new juce::DynamicObject();
  result->setProperty("updatedLayerIds", juce::var(ids));
  result->setProperty("patchName", juce::String(patch->name));
  result->setProperty("history", historyJson(undoManager_));
  return Response::success(juce::var(result));
}

std::map<std::pair<int, int>, juce::var> MainComponent::agentBridgeChecks() const {
  std::map<std::pair<int, int>, juce::var> checks;
  if (!bridgeMapCommands_) return checks;
  const auto status = bridgeMapCommands_->status();
  const auto *assignments = status["assignments"].getArray();
  if (!assignments) return checks;
  bool reported = false;
  for (const auto &assignment : *assignments) {
    const auto beacon = assignment["beacon"].toString();
    reported = reported || beacon == "matched" || beacon == "mismatched";
  }
  for (const auto &assignment : *assignments) {
    const auto hash = assignment["hash"].toString();
    const auto beacon = assignment["beacon"].toString();
    juce::String label;
    if (const auto *sections = status["sections"].getArray())
      for (const auto &section : *sections)
        if (section["hash"].toString() == hash) label = section["label"].toString();
    if (label.isEmpty())
      if (const auto *documents = status["documents"].getArray())
        for (const auto &document : *documents)
          if (document["hash"].toString() == hash) label = document["name"].toString();
    // Dorico sends every chair in its template its map's version when
    // playback starts, so once any chair has reported, a silent one isn't in
    // this project's template.
    const auto check = agent::bridgeCheck(beacon, static_cast<bool>(assignment["verified"]), reported,
                                          static_cast<bool>(assignment["otherRevision"])
                                              ? static_cast<int>(assignment["playingRevision"]) : 0,
                                          static_cast<int>(assignment["assignedRevision"]),
                                          static_cast<bool>(assignment["ambiguousRevision"]));
    auto *item = new juce::DynamicObject();
    item->setProperty("map", label.isNotEmpty() ? label : juce::String("Bridge map"));
    item->setProperty("state", check.state);
    item->setProperty("message", check.message);
    if (const auto withheld = static_cast<juce::int64>(assignment["withheldNotes"]); withheld > 0) {
      item->setProperty("notesNotPlayed", withheld);
      item->setProperty("notPlayedByReason", assignment["withheldByReason"]);
    }
    checks[{static_cast<int>(assignment["port"]), static_cast<int>(assignment["channel"])}] = juce::var(item);
  }
  return checks;
}

namespace {
/// Bridge-map sections for the agent: what is installed and whether this
/// Fiddle has a newer built-in version.
juce::var bridgeSectionsJson(const juce::var &status) {
  std::map<juce::String, juce::var> revisions;
  if (const auto *documents = status["documents"].getArray())
    for (const auto &document : *documents)
      revisions[document["hash"].toString()] = document["revision"];
  juce::Array<juce::var> sections;
  if (const auto *list = status["sections"].getArray())
    for (const auto &section : *list) {
      const auto hash = section["hash"].toString();
      auto *item = new juce::DynamicObject();
      item->setProperty("section", section["key"]);
      item->setProperty("label", section["label"]);
      item->setProperty("installed", hash.isNotEmpty());
      if (hash.isNotEmpty()) item->setProperty("revision", revisions[hash]);
      item->setProperty("builtInRevision", section["builtInRevision"]);
      item->setProperty("updateAvailable", static_cast<bool>(section["updateAvailable"]));
      sections.add(juce::var(item));
    }
  return juce::var(sections);
}
} // namespace

AgentControlServer::Response MainComponent::agentGetBridgeMaps(const juce::var &) {
  if (!bridgeMapCommands_)
    return Response::failure("Bridge maps are not available");
  const auto status = bridgeMapCommands_->status();
  // Chairs whose notes aren't all playing, by name, including withheld notes
  // on a chair whose map checks out.
  auto *repository = db_.getLibraryRoutingRepository();
  const auto checks = agentBridgeChecks();
  juce::Array<juce::var> attention;
  std::map<juce::String, int> counts;
  if (repository)
    for (const auto &chair : repository->listChairs()) {
      const auto check = checks.find({chair.flatIndex / 16, chair.flatIndex % 16 + 1});
      if (check == checks.end()) continue;
      const auto state = check->second["state"].toString();
      ++counts[state];
      if (agent::bridgeNeedsAttention(state, static_cast<juce::int64>(check->second["notesNotPlayed"]))) {
        auto *item = new juce::DynamicObject();
        item->setProperty("chairId", juce::String(chair.id));
        item->setProperty("chairName", juce::String(chair.name));
        item->setProperty("bridge", check->second);
        attention.add(juce::var(item));
      }
    }
  auto *byState = new juce::DynamicObject();
  for (const auto &[state, count] : counts) byState->setProperty(state, count);
  auto *result = new juce::DynamicObject();
  result->setProperty("sections", bridgeSectionsJson(status));
  result->setProperty("chairsByState", juce::var(byState));
  result->setProperty("chairsNeedingAttention", juce::var(attention));
  if (status["error"].toString().isNotEmpty()) result->setProperty("error", status["error"]);
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentUpdateBridgeMaps(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  if (!bridgeMapCommands_)
    return Response::failure("Bridge maps are not available");
  const auto section = text(params, "section");
  if (section.isNotEmpty() &&
      std::none_of(bridgeSections().begin(), bridgeSections().end(),
                   [&](const auto &info) { return section == info.key; }))
    return Response::failure("Unknown bridge-map section: " + section);
  const auto before = juce::JSON::toString(bridgeSectionsJson(bridgeMapCommands_->status()));
  if (!bridgeMapCommands_->useBuiltInSections(section.isEmpty() ? std::vector<juce::String>{}
                                                                : std::vector<juce::String>{section}))
    return Response::failure(bridgeMapCommands_->status()["error"].toString());
  const auto sections = bridgeSectionsJson(bridgeMapCommands_->status());
  auto *result = new juce::DynamicObject();
  result->setProperty("changed", juce::JSON::toString(sections) != before);
  result->setProperty("sections", sections);
  // Dorico has the old maps until the template is installed again.
  result->setProperty("templateStatus", templateStatus().toJson());
  result->setProperty("history", historyJson(undoManager_));
  return Response::success(juce::var(result));
}

// ── Player windows for preset capture ───────────────────────────────────
// Some players (Synchron Player, Kontakt) choose presets only in their own
// editor, so the user picks the preset and the agent captures the result.
// The window runs in agentPatchPlayers_, outside the mixer, the audio graph
// and project versions, like Library Manager's player windows.

void MainComponent::noteAgentPatchPlayerChanges() {
  for (const auto &patchId : agentPatchPlayers_.consumeChangedPatchIds()) {
    const auto session = agentPatchPlayerSessions_.find(patchId);
    // Loading and restoring the saved state are the baseline, not a change.
    if (session != agentPatchPlayerSessions_.end() && session->second.status == "loaded")
      session->second.changed = true;
  }
  // The user closing the window ends its session: an answer still pending
  // for it is then stale, and opening again starts a new session.
  for (auto session = agentPatchPlayerSessions_.begin(); session != agentPatchPlayerSessions_.end();) {
    if (session->second.status == "loaded") {
      const bool visible = agentPatchPlayers_.isEditorVisible(session->first);
      if (visible) {
        session->second.editorShown = true;
      } else if (session->second.editorShown) {
        agentPatchPlayers_.discard(session->first);
        session = agentPatchPlayerSessions_.erase(session);
        continue;
      }
    }
    ++session;
  }
}

juce::var MainComponent::agentPatchPlayerJson(const std::string &patchId) {
  auto *result = new juce::DynamicObject();
  result->setProperty("patchId", juce::String(patchId));
  const auto session = agentPatchPlayerSessions_.find(patchId);
  result->setProperty("open", session != agentPatchPlayerSessions_.end());
  if (session != agentPatchPlayerSessions_.end()) {
    result->setProperty("session", session->second.sessionId);
    result->setProperty("status", session->second.status);
    result->setProperty("windowTitle", session->second.title);
    result->setProperty("changed", session->second.changed);
    if (session->second.error.isNotEmpty()) result->setProperty("error", session->second.error);
  }
  return juce::var(result);
}

AgentControlServer::Response MainComponent::agentOpenPatchPlayer(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(false); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto patchId = text(params, "patchId").toStdString();
  const auto patch = repository && !patchId.empty() ? repository->getPatch(patchId) : std::nullopt;
  if (!patch)
    return Response::failure("Patch not found");
  std::optional<juce::PluginDescription> description;
  for (const auto &candidate : pluginScanner_.getKnownPluginList().getTypes())
    if (candidate.uniqueId == patch->pluginUid && candidate.isInstrument) {
      description = candidate;
      break;
    }
  if (!description)
    return Response::failure("The patch's player plug-in is not installed");
  juce::String libraryName;
  for (const auto &library : db_.listLibraries())
    if (library.id.toStdString() == patch->libraryId) libraryName = library.name;
  const auto title = libraryName + " — " + juce::String(patch->name);

  noteAgentPatchPlayerChanges();
  auto existing = agentPatchPlayerSessions_.find(patchId);
  const bool reuse = existing != agentPatchPlayerSessions_.end() &&
                     existing->second.pluginUid == patch->pluginUid &&
                     existing->second.status != "failed" &&
                     existing->second.baseline == agent::patchBaseline(*patch);
  if (!reuse) {
    agentPatchPlayers_.discard(patchId);
    AgentPatchPlayer session;
    session.pluginUid = patch->pluginUid;
    session.generation = ++agentPatchPlayerGeneration_;
    session.sessionId = juce::Uuid().toString();
    session.title = title;
    session.baseline = agent::patchBaseline(*patch);
    agentPatchPlayerSessions_[patchId] = session;
  }
  const int generation = agentPatchPlayerSessions_[patchId].generation;
  juce::MemoryBlock state;
  if (!patch->pluginState.empty())
    state.append(patch->pluginState.data(), patch->pluginState.size());
  juce::Component::SafePointer<MainComponent> safeThis(this);
  const bool started = agentPatchPlayers_.open(
      patchId, title, *description, state,
      [safeThis, patchId, generation](bool success, const juce::String &error) {
        if (safeThis == nullptr) return;
        const auto session = safeThis->agentPatchPlayerSessions_.find(patchId);
        if (session == safeThis->agentPatchPlayerSessions_.end() ||
            session->second.generation != generation)
          return;
        if (success) {
          session->second.status = "loaded";
          // The host has just shown the editor; noting that now (not at the
          // next poll) lets a close before that poll still end the session.
          if (safeThis->agentPatchPlayers_.isEditorVisible(patchId))
            session->second.editorShown = true;
        } else {
          session->second.status = "failed";
          session->second.error = error.isNotEmpty() ? error : juce::String("Could not open the player");
        }
      });
  if (!started) {
    agentPatchPlayerSessions_.erase(patchId);
    return Response::failure("Could not open the player");
  }

  auto result = agentPatchPlayerJson(patchId);
  auto *object = result.getDynamicObject();
  object->setProperty("patchName", juce::String(patch->name));
  object->setProperty("libraryName", libraryName);
  object->setProperty("pluginName", description->name);
  object->setProperty("expectedPresetName", juce::String(patch->expectedPresetName));
  object->setProperty("message",
      "The player window \"" + title + "\" opens in Fiddle. Ask the user to choose " +
      (patch->expectedPresetName.empty() ? juce::String("the preset this patch should use")
                                         : "the preset \"" + juce::String(patch->expectedPresetName) + "\"") +
      " in it, then call fiddle_capture_patch_player.");
  return Response::success(result);
}

AgentControlServer::Response MainComponent::agentPatchPlayerStatus(const juce::var &params) {
  const auto patchId = text(params, "patchId").toStdString();
  if (patchId.empty())
    return Response::failure("patchId is required");
  noteAgentPatchPlayerChanges();
  return Response::success(agentPatchPlayerJson(patchId));
}

AgentControlServer::Response MainComponent::agentCapturePatchPlayer(const juce::var &params) {
  if (const auto blocker = agentSetupBlocker(true); blocker.isNotEmpty())
    return Response::failure(blocker);
  auto *repository = db_.getLibraryRoutingRepository();
  const auto patchId = text(params, "patchId").toStdString();
  noteAgentPatchPlayerChanges();
  const auto patch = repository && !patchId.empty() ? repository->getPatch(patchId) : std::nullopt;
  if (!patch)
    return Response::failure("Patch not found");
  const auto session = agentPatchPlayerSessions_.find(patchId);
  agent::PlayerSessionState state;
  if (session != agentPatchPlayerSessions_.end()) {
    state.open = true;
    state.session = session->second.sessionId.toStdString();
    state.status = session->second.status;
    state.changed = session->second.changed;
    state.baseline = session->second.baseline;
  }
  if (const auto captured = agentPatchPlayerCaptured_.find(patchId); captured != agentPatchPlayerCaptured_.end())
    state.lastCaptured = captured->second;
  const auto requestedSession = text(params, "session").toStdString();
  switch (agent::decidePlayerCapture(state, requestedSession, flag(params, "allowUnchanged"),
                                     agent::patchBaseline(*patch))) {
  case agent::PlayerCapture::capture:
    break;
  case agent::PlayerCapture::alreadyCaptured: {
    auto *result = new juce::DynamicObject();
    result->setProperty("captured", true);
    result->setProperty("alreadyCaptured", true);
    result->setProperty("patchId", juce::String(patchId));
    return Response::success(juce::var(result));
  }
  case agent::PlayerCapture::stale:
    return Response::failure("This answer is for a player window that has since closed or been "
                             "reopened, so nothing was captured.");
  case agent::PlayerCapture::notOpen:
    return Response::failure("No player window is open for this patch. Call fiddle_open_patch_player first.");
  case agent::PlayerCapture::loading:
    return Response::failure("The player is still loading. Try again in a moment.");
  case agent::PlayerCapture::failed:
    return Response::failure("The player failed to open: " + session->second.error);
  case agent::PlayerCapture::unchanged:
    // The same rule as Library Manager's Capture & continue: a capture
    // records a choice the user made, never the default or stale state.
    return Response::failure("The player hasn't changed since its window opened, so nothing was "
                             "captured. Ask the user to choose the preset in the window. If it "
                             "was already right, call again with allow_unchanged.");
  case agent::PlayerCapture::patchChanged:
    return Response::failure("The patch was changed (in Library Manager or by another tool) while "
                             "its window was open, so nothing was captured. Open the player again.");
  }
  if (patch->pluginUid != session->second.pluginUid)
    return Response::failure("The patch's player changed while the window was open. Open it again.");
  std::vector<std::uint8_t> captured;
  if (!agentPatchPlayers_.captureState(patchId, patch->pluginUid, captured))
    return Response::failure("Could not read the player's state");
  auto library = repository->captureLibrary(patch->libraryId);
  if (!library || !library->exists)
    return Response::failure("The patch's library could not be read");
  bool found = false;
  for (auto &row : library->patches)
    if (row.id == patchId) {
      row.pluginState = std::move(captured);
      row.setupComplete = true;
      found = true;
    }
  if (!found)
    return Response::failure("Patch not found in its library");
  // Identical state on a finished patch is a successful capture with
  // nothing to save, not an error (Undo refuses no-op steps).
  auto action = std::make_unique<LibraryCatalogAction>(*repository, *library);
  const bool changedCatalog = !action->isNoOp();
  if (changedCatalog && !libraryUndoManager_.perform(std::move(action)))
    return Response::failure("Fiddle could not save the captured preset");
  agentPatchPlayerCaptured_[patchId] = session->second.sessionId.toStdString();
  agentPatchPlayers_.discard(patchId); // closes the window
  agentPatchPlayerSessions_.erase(patchId);
  if (changedCatalog)
    catalogChangedByAgent(juce::String(patch->libraryId));

  const int layers = repository->patchUsageCount(patchId);
  auto *result = new juce::DynamicObject();
  result->setProperty("captured", true);
  result->setProperty("changed", changedCatalog);
  result->setProperty("patchId", juce::String(patchId));
  result->setProperty("patchName", juce::String(patch->name));
  result->setProperty("layerCount", layers);
  if (layers > 0)
    result->setProperty("note", "Layers already using this patch keep their earlier player state "
                                "until updated with fiddle_update_layers_from_library.");
  result->setProperty("libraryHistory", historyJson(libraryUndoManager_));
  return Response::success(juce::var(result));
}

AgentControlServer::Response MainComponent::agentClosePatchPlayer(const juce::var &params) {
  const auto patchId = text(params, "patchId").toStdString();
  if (patchId.empty())
    return Response::failure("patchId is required");
  const auto session = agentPatchPlayerSessions_.find(patchId);
  const auto requestedSession = text(params, "session");
  // An answer for an earlier window leaves a reopened one alone.
  if (session != agentPatchPlayerSessions_.end() && requestedSession.isNotEmpty() &&
      requestedSession != session->second.sessionId) {
    auto *stale = new juce::DynamicObject();
    stale->setProperty("closed", false);
    stale->setProperty("stale", true);
    return Response::success(juce::var(stale));
  }
  const bool wasOpen = session != agentPatchPlayerSessions_.end();
  if (wasOpen) agentPatchPlayerSessions_.erase(session);
  agentPatchPlayers_.discard(patchId);
  auto *result = new juce::DynamicObject();
  result->setProperty("closed", wasOpen);
  return Response::success(juce::var(result));
}

} // namespace fiddle
