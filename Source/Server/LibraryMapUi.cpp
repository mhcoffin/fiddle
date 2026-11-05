// Per-library expression and percussion maps (docs/library-map-import.md):
// lookups for playback, catalog edits, import/export, layer assignment, the
// one-time migration and source update checks.
#include "LibraryCatalogAction.h"
#include "LibraryMaps.h"
#include "MainComponent.h"
#include "PercussionMap.h"
#include "UndoActions.h"

namespace fiddle {
namespace {

juce::var argument(const juce::var &payload, int index) {
  return payload.isArray() && index < payload.size() ? payload[index] : juce::var();
}

std::optional<LibraryMapKind> kindArgument(const juce::var &payload, int index) {
  return libraryMapKindFromName(argument(payload, index).toString().toStdString());
}

juce::String kindNoun(LibraryMapKind kind, bool plural = false) {
  return juce::String(kind == LibraryMapKind::percussion ? "percussion map" : "expression map") +
         (plural ? "s" : "");
}

LibraryMapRow rowFromCandidate(const std::string &libraryId, const LibraryMapCandidate &candidate) {
  LibraryMapRow row;
  row.libraryId = libraryId;
  row.kind = candidate.kind;
  row.entityId = candidate.entityId;
  row.name = candidate.name;
  row.xml = candidate.definitionXml.toStdString();
  row.sourcePath = candidate.file.getFullPathName().toStdString();
  row.sourceVersion = candidate.version;
  row.sourceHash = candidate.hash.toStdString();
  return row;
}

// How a file's map compares with the library's copy.
juce::String importStatus(const std::optional<LibraryMapRow> &existing, const LibraryMapCandidate &candidate) {
  if (!existing)
    return "new";
  if (libraryMapHash(juce::String::fromUTF8(existing->xml.c_str())) == candidate.hash)
    return "same";
  const int storedVersion =
      libraryMapDefinitionVersion(existing->kind, juce::String::fromUTF8(existing->xml.c_str()));
  return candidate.version > storedVersion ? "newer" : "changed";
}

juce::var stringArray(const std::vector<std::string> &values) {
  juce::Array<juce::var> result;
  for (const auto &value : values)
    result.add(juce::String(value));
  return result;
}

} // namespace

std::string MainComponent::layerLibraryId(const juce::String &stripId) {
  auto *repository = db_.getLibraryRoutingRepository();
  const auto layer = repository ? repository->getLayer(stripId.toStdString()) : std::nullopt;
  return layer ? layer->libraryId : std::string{};
}

PercussionMapAssignment MainComponent::loadPercussionMap(const std::string &libraryId,
                                                         const std::string &entityId) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (entityId.empty() || !repository)
    return {};
  const auto row = repository->getLibraryMap(libraryId, LibraryMapKind::percussion, entityId);
  if (!row)
    return {};
  const auto xml = juce::String::fromUTF8(row->xml.c_str());
  return {compilePercussionMap(xml), xml};
}

ExpressionMapAssignment MainComponent::loadLibraryExpressionMap(const std::string &libraryId,
                                                               const std::string &entityId) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (entityId.empty() || !repository)
    return {};
  const auto row = repository->getLibraryMap(libraryId, LibraryMapKind::expression, entityId);
  if (!row)
    return {};
  const auto definition = juce::String::fromUTF8(row->xml.c_str());
  auto data = compileExpressionMap(definition);
  if (!data)
    return {};
  // The source text is a complete .doricolib so project versions restore it.
  return {std::move(data), juce::String(row->sourcePath), expressionMapLibraryXml(definition)};
}

void MainComponent::refreshLiveLibraryMaps() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  bool changed = false;
  for (const auto &layer : repository->listLayers()) {
    auto *strip = mixer_.getStrip(juce::String(layer.id));
    if (!strip || layer.libraryId.empty())
      continue;
    const auto percussion = strip->snapshotPercussionMap();
    if (percussion.map) {
      auto latest = loadPercussionMap(layer.libraryId, percussion.map->entityId);
      if (latest.map && latest.sourceXml != percussion.sourceXml) {
        strip->setPercussionMapAssignment(std::move(latest));
        changed = true;
      }
    }
    const auto expression = strip->snapshotExpressionMap();
    if (expression.data) {
      auto latest = loadLibraryExpressionMap(layer.libraryId, expression.data->entityID);
      if (latest.data && latest.sourceXml != expression.sourceXml) {
        strip->setExpressionMapAssignment(std::move(latest));
        changed = true;
      }
    }
  }
  if (changed) {
    saveAllStripsToDB(false);
    scheduleStateRebuild();
  }
}

void MainComponent::pushLibraryMaps() {
  juce::Array<juce::var> result;
  if (auto *repository = db_.getLibraryRoutingRepository()) {
    for (const auto &row : repository->listLibraryMaps()) {
      const auto definition = juce::String::fromUTF8(row.xml.c_str());
      auto *obj = new juce::DynamicObject();
      obj->setProperty("libraryId", juce::String(row.libraryId));
      obj->setProperty("kind", juce::String(libraryMapKindName(row.kind)));
      obj->setProperty("entityId", juce::String(row.entityId));
      obj->setProperty("name", juce::String(row.name));
      obj->setProperty("revision", row.revision);
      obj->setProperty("version", libraryMapDefinitionVersion(row.kind, definition));
      obj->setProperty("sourcePath", juce::String(row.sourcePath));
      obj->setProperty("sourceVersion", row.sourceVersion);
      obj->setProperty("edited", row.edited);
      obj->setProperty("references",
                       stringArray(repository->libraryMapReferences(row.libraryId, row.kind, row.entityId)));
      if (row.kind == LibraryMapKind::percussion) {
        const auto map = compilePercussionMap(definition);
        juce::Array<juce::var> instruments;
        if (map)
          for (const auto &id : map->instruments())
            instruments.add(juce::String(id));
        obj->setProperty("instruments", instruments);
        obj->setProperty("entryCount", map ? int(map->entries.size()) : 0);
        obj->setProperty("map", map ? map->toJson() : juce::var());
      }
      result.add(juce::var(obj));
    }
  }
  broadcastMessage("setLibraryMaps", juce::var(result));
}

void MainComponent::libraryMapResult(const juce::String &libraryId, LibraryMapKind kind,
                                     const juce::String &action, bool success,
                                     const juce::String &message, const juce::String &entityId) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("libraryId", libraryId);
  obj->setProperty("kind", juce::String(libraryMapKindName(kind)));
  obj->setProperty("action", action);
  obj->setProperty("success", success);
  obj->setProperty("message", message);
  obj->setProperty("entityId", entityId);
  broadcastMessage("libraryMapResult", juce::var(obj));
}

bool MainComponent::performLibraryMapEdit(const std::string &libraryId, LibraryMapKind kind,
                                          const std::string &entityId,
                                          std::optional<LibraryMapRow> target,
                                          const juce::String &description) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return false;
  auto action = std::make_unique<LibraryMapCatalogAction>(*repository, libraryId, kind, entityId,
                                                          std::move(target), description);
  const bool success = action->isNoOp() || libraryUndoManager_.perform(std::move(action));
  if (success) {
    refreshLiveLibraryMaps();
    pushLibraryCatalogHistory();
    pushMixerState(false);
  }
  pushLibraryMaps();
  return success;
}

bool MainComponent::importLibraryMap(const std::string &libraryId, const LibraryMapCandidate &candidate) {
  return performLibraryMapEdit(libraryId, candidate.kind, candidate.entityId,
                               rowFromCandidate(libraryId, candidate),
                               "Import " + kindNoun(candidate.kind) + ": " + juce::String(candidate.name));
}

void MainComponent::offerLibraryMapImport(const juce::String &libraryId, LibraryMapKind kind,
                                          const juce::Array<juce::File> &sources,
                                          const juce::String &origin) {
  auto *repository = db_.getLibraryRoutingRepository();
  juce::Array<juce::var> candidates, errors;
  std::set<std::string> seen;
  for (const auto &file : doricolibFilesIn(sources)) {
    // Fiddle's own bridge libraries are Dorico-side plumbing, not a sample
    // library's maps.
    if (origin == "dorico" && file.getFileName().startsWith("Fiddle"))
      continue;
    juce::String error;
    const auto maps = readLibraryMapFile(kind, file, error);
    if (error.isNotEmpty())
      errors.add(file.getFileName() + ": " + error);
    else if (maps.empty() && origin != "dorico")
      errors.add(file.getFileName() + ": no " + kindNoun(kind, true));
    for (const auto &map : maps) {
      if (!seen.insert(map.entityId).second)
        continue;
      const auto existing = repository ? repository->getLibraryMap(libraryId.toStdString(), kind, map.entityId)
                                       : std::nullopt;
      auto *candidate = new juce::DynamicObject();
      candidate->setProperty("entityId", juce::String(map.entityId));
      candidate->setProperty("name", juce::String(map.name));
      candidate->setProperty("version", map.version);
      candidate->setProperty("file", map.file.getFullPathName());
      candidate->setProperty("status", importStatus(existing, map));
      candidate->setProperty("existingVersion",
                             existing ? libraryMapDefinitionVersion(kind, juce::String::fromUTF8(existing->xml.c_str()))
                                      : 0);
      candidate->setProperty("creator", juce::String(map.creator));
      candidate->setProperty("pluginNames", juce::String(map.pluginNames));
      candidates.add(juce::var(candidate));
    }
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("libraryId", libraryId);
  result->setProperty("kind", juce::String(libraryMapKindName(kind)));
  result->setProperty("origin", origin);
  result->setProperty("maps", candidates);
  result->setProperty("errors", errors);
  broadcastMessage("libraryMapImportCandidates", juce::var(result));
}

void MainComponent::assignPercussionMap(const juce::String &stripId, PercussionMapAssignment assignment) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return;
  auto before = strip->snapshotPercussionMap();
  if ((before.map ? before.map->entityId : "") == (assignment.map ? assignment.map->entityId : "") &&
      before.sourceXml == assignment.sourceXml)
    return;
  if (undoManager_.perform(std::make_unique<SetPercussionMapAction>(mixer_, stripId, std::move(before),
                                                                    std::move(assignment)))) {
    saveAllStripsToDB(false);
    pushMixerState();
    pushLibraryLayerStatus();
    scheduleStateRebuild();
  }
}

std::string MainComponent::importMapFileForStrip(const juce::String &stripId, LibraryMapKind kind,
                                                 const juce::File &file) {
  juce::String error;
  const auto maps = readLibraryMapFile(kind, file, error);
  if (maps.empty()) {
    pushLogMessage("<b>[Maps]</b> " + file.getFileName() + ": " +
                       (error.isNotEmpty() ? error : "no " + kindNoun(kind, true)),
                   true);
    return {};
  }
  // A percussion file may hold maps for several instruments: prefer the one
  // covering this layer's instrument.
  const auto *chosen = &maps.front();
  if (kind == LibraryMapKind::percussion)
    if (auto *strip = mixer_.getStrip(stripId))
      for (const auto &map : maps)
        if (const auto compiled = compilePercussionMap(map.definitionXml);
            compiled && compiled->instruments().count(strip->instrumentEntityId.toStdString())) {
          chosen = &map;
          break;
        }
  const auto libraryId = layerLibraryId(stripId);
  auto *repository = db_.getLibraryRoutingRepository();
  if (libraryId.empty() || !repository) {
    pushLogMessage("<b>[Maps]</b> This layer belongs to no library, so the map can't be added to one.", true);
    return {};
  }
  const auto existing = repository->getLibraryMap(libraryId, kind, chosen->entityId);
  if (importStatus(existing, *chosen) != "same" && !importLibraryMap(libraryId, *chosen))
    return {};
  return chosen->entityId;
}

void MainComponent::migrateLibraryMaps() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  std::vector<juce::File> catalogFiles;
  std::set<std::string> seenFiles;
  for (const auto &entry : xmapLibrary_.getEntries())
    if (seenFiles.insert(entry.sourceFile.getFullPathName().toStdString()).second)
      catalogFiles.push_back(entry.sourceFile);
  const bool findPercussionSources = db_.loadSetting("library_maps_percussion_sources_found") != "1";
  const auto report = fiddle::migrateLibraryMaps(
      *repository, catalogFiles, doricoMapSources(),
      [this](const std::string &entityId) { return db_.findSavedExpressionMapSource(entityId); },
      findPercussionSources);
  if (findPercussionSources)
    db_.saveSetting("library_maps_percussion_sources_found", "1");
  juce::StringArray missing;
  for (const auto &label : report.missing)
    missing.add(juce::String(label));
  if (report.expressionCopied || report.percussionSourced || !missing.isEmpty())
    pushLogMessage("<b>[Maps]</b> Library maps: copied " + juce::String(report.expressionCopied) +
                       " expression maps into their libraries, found sources for " +
                       juce::String(report.percussionSourced) + " percussion maps" +
                       (missing.isEmpty() ? juce::String() : ". Missing: " + missing.joinIntoString(", ")),
                   !missing.isEmpty());
}

void MainComponent::setupLibraryMapHandlers() {
  jsRouter_.registerHandler("requestLibraryMaps", [this](const juce::var &) {
    safeCallAsync([this] { pushLibraryMaps(); });
  });

  // [libraryId, kind]: choose .doricolib files or folders to import from.
  jsRouter_.registerHandler("chooseLibraryMapFiles", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString();
    const auto kind = kindArgument(payload, 1);
    if (libraryId.isEmpty() || !kind)
      return;
    auto chooser = std::make_shared<juce::FileChooser>(
        "Import " + kindNoun(*kind, true) + " (.doricolib file or folder)", juce::File{}, "*.doricolib");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::canSelectDirectories |
                             juce::FileBrowserComponent::canSelectMultipleItems,
                         [safeThis, chooser, libraryId, kind = *kind](const juce::FileChooser &fc) {
                           if (safeThis && !fc.getResults().isEmpty())
                             safeThis->offerLibraryMapImport(libraryId, kind, fc.getResults(), "file");
                         });
  });

  // [libraryId, kind]: every map of that kind in Dorico's library folders.
  jsRouter_.registerHandler("findDoricoLibraryMaps", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString();
    const auto kind = kindArgument(payload, 1);
    if (libraryId.isEmpty() || !kind)
      return;
    safeCallAsync([this, libraryId, kind = *kind] {
      offerLibraryMapImport(libraryId, kind, doricoMapSources(), "dorico");
    });
  });

  // [libraryId, kind, [{file, entityId}]]: import the chosen maps.
  jsRouter_.registerHandler("importLibraryMaps", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString().toStdString();
    const auto kind = kindArgument(payload, 1);
    const auto chosen = argument(payload, 2);
    if (libraryId.empty() || !kind || !chosen.isArray())
      return;
    safeCallAsync([this, libraryId, kind = *kind, chosen] {
      int imported = 0;
      juce::StringArray failures;
      std::map<juce::String, std::vector<LibraryMapCandidate>> files;
      for (const auto &item : *chosen.getArray()) {
        const auto path = item["file"].toString();
        const auto entityId = item["entityId"].toString().toStdString();
        if (!files.count(path)) {
          juce::String error;
          files[path] = readLibraryMapFile(kind, juce::File(path), error);
        }
        const auto &maps = files[path];
        const auto match = std::find_if(maps.begin(), maps.end(),
                                        [&](const auto &map) { return map.entityId == entityId; });
        if (match != maps.end() && importLibraryMap(libraryId, *match))
          ++imported;
        else
          failures.add(juce::String(entityId));
      }
      libraryMapResult(juce::String(libraryId), kind, "import", failures.isEmpty(),
                       "Imported " + juce::String(imported) + " " + kindNoun(kind, imported != 1) +
                           (failures.isEmpty() ? juce::String() : ". Failed: " + failures.joinIntoString("; ")));
    });
  });

  // [libraryId, percussion map JSON]: create or edit a percussion map.
  jsRouter_.registerHandler("saveLibraryMap", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString().toStdString();
    const auto json = argument(payload, 1);
    safeCallAsync([this, libraryId, json] {
      const auto kind = LibraryMapKind::percussion;
      PercussionMap map;
      juce::String error;
      if (libraryId.empty() || !PercussionMap::fromJson(json, map, error)) {
        libraryMapResult(juce::String(libraryId), kind, "save", false,
                         error.isNotEmpty() ? error : "No library", json["entityId"].toString());
        return;
      }
      auto *repository = db_.getLibraryRoutingRepository();
      const auto existing = repository ? repository->getLibraryMap(libraryId, kind, map.entityId) : std::nullopt;
      LibraryMapRow row = existing.value_or(LibraryMapRow{});
      row.libraryId = libraryId;
      row.kind = kind;
      row.entityId = map.entityId;
      row.name = map.name;
      row.xml = map.toDefinitionXml().toStdString();
      // Edits to an imported map are marked so an update warns before
      // replacing them.
      if (existing && existing->xml != row.xml && !existing->sourcePath.empty())
        row.edited = true;
      const auto id = juce::String(map.entityId);
      const bool success = performLibraryMapEdit(
          libraryId, kind, map.entityId, row,
          (existing ? "Edit percussion map: " : "Create percussion map: ") + juce::String(map.name));
      libraryMapResult(juce::String(libraryId), kind, "save", success,
                       success ? "Percussion map saved" : "Could not save the percussion map", id);
    });
  });

  // [libraryId, kind, entityId]
  jsRouter_.registerHandler("deleteLibraryMap", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString().toStdString();
    const auto kind = kindArgument(payload, 1);
    const auto entityId = argument(payload, 2).toString().toStdString();
    if (!kind)
      return;
    safeCallAsync([this, libraryId, kind = *kind, entityId] {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository)
        return;
      const auto references = repository->libraryMapReferences(libraryId, kind, entityId);
      if (!references.empty()) {
        juce::StringArray labels;
        for (const auto &label : references)
          labels.add(juce::String(label));
        libraryMapResult(juce::String(libraryId), kind, "delete", false,
                         "In use by " + labels.joinIntoString(", "), juce::String(entityId));
        return;
      }
      const auto row = repository->getLibraryMap(libraryId, kind, entityId);
      const bool success = row && performLibraryMapEdit(libraryId, kind, entityId, std::nullopt,
                                                        "Delete " + kindNoun(kind) + ": " + juce::String(row->name));
      libraryMapResult(juce::String(libraryId), kind, "delete", success,
                       success ? "Deleted " + juce::String(row->name) : "Could not delete the " + kindNoun(kind),
                       juce::String(entityId));
    });
  });

  // [libraryId, kind, entityId]: save the map as a .doricolib for Dorico.
  jsRouter_.registerHandler("exportLibraryMap", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString().toStdString();
    const auto kind = kindArgument(payload, 1);
    const auto entityId = argument(payload, 2).toString().toStdString();
    auto *repository = db_.getLibraryRoutingRepository();
    const auto row = kind && repository ? repository->getLibraryMap(libraryId, *kind, entityId) : std::nullopt;
    if (!row)
      return;
    const auto definition = juce::String::fromUTF8(row->xml.c_str());
    juce::String text;
    if (row->kind == LibraryMapKind::percussion) {
      if (const auto map = compilePercussionMap(definition))
        text = map->toLibraryXml();
    } else {
      text = expressionMapLibraryXml(definition);
    }
    if (text.isEmpty())
      return;
    const auto suffix = row->kind == LibraryMapKind::percussion ? " (PM)" : "";
    auto chooser = std::make_shared<juce::FileChooser>(
        "Export " + kindNoun(row->kind),
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
            .getChildFile(juce::File::createLegalFileName(juce::String(row->name) + suffix + ".doricolib")),
        "*.doricolib");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    chooser->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [safeThis, chooser, text, row = *row](const juce::FileChooser &fc) {
                           if (!safeThis || fc.getResult() == juce::File{})
                             return;
                           const auto file = fc.getResult().withFileExtension("doricolib");
                           const bool success = file.replaceWithText(text);
                           safeThis->libraryMapResult(juce::String(row.libraryId), row.kind, "export", success,
                                                      success ? "Exported " + file.getFullPathName()
                                                              : "Could not write " + file.getFullPathName(),
                                                      juce::String(row.entityId));
                         });
  });

  // Layers: assign a percussion map from the layer's library.
  jsRouter_.registerHandler("loadPercussionMap", [this](const juce::var &payload) {
    const auto stripId = argument(payload, 0).toString();
    const auto entityId = argument(payload, 1).toString().toStdString();
    safeCallAsync([this, stripId, entityId] {
      if (auto assignment = loadPercussionMap(layerLibraryId(stripId), entityId); assignment.map)
        assignPercussionMap(stripId, std::move(assignment));
    });
  });
  jsRouter_.registerHandler("clearPercussionMap", [this](const juce::var &payload) {
    const auto stripId = argument(payload, 0).toString();
    safeCallAsync([this, stripId] { assignPercussionMap(stripId, {}); });
  });
  // The escape hatch: a file's map joins the layer's library, then plays.
  jsRouter_.registerHandler("loadPercussionMapFromFile", [this](const juce::var &payload) {
    const auto stripId = argument(payload, 0).toString();
    auto chooser = std::make_shared<juce::FileChooser>("Load percussion map", juce::File{}, "*.doricolib");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [safeThis, chooser, stripId](const juce::FileChooser &fc) {
          if (!safeThis || fc.getResult() == juce::File{})
            return;
          const auto entityId = safeThis->importMapFileForStrip(stripId, LibraryMapKind::percussion, fc.getResult());
          if (!entityId.empty())
            safeThis->assignPercussionMap(
                stripId, safeThis->loadPercussionMap(safeThis->layerLibraryId(stripId), entityId));
        });
  });

  setupLibraryMapSourceHandlers();
}

// Source update checks (docs/library-map-import.md, "Checking sources for
// updates"): compare each imported map with the file it came from.
void MainComponent::checkLibraryMapSources(const juce::String &libraryId) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  std::map<std::pair<std::string, LibraryMapKind>, std::vector<LibraryMapCandidate>> files;
  const auto mapsIn = [&files](const juce::File &file, LibraryMapKind kind) -> const std::vector<LibraryMapCandidate> & {
    const auto key = std::make_pair(file.getFullPathName().toStdString(), kind);
    if (!files.count(key)) {
      juce::String error;
      files[key] = readLibraryMapFile(kind, file, error);
    }
    return files[key];
  };
  const auto findIn = [&](const juce::File &file, LibraryMapKind kind,
                          const std::string &entityId) -> std::optional<LibraryMapCandidate> {
    for (const auto &map : mapsIn(file, kind))
      if (map.entityId == entityId)
        return map;
    return std::nullopt;
  };
  std::optional<juce::Array<juce::File>> doricoFiles;
  juce::Array<juce::var> statuses;
  for (const auto &row : repository->listLibraryMaps(libraryId.toStdString())) {
    if (row.sourcePath.empty())
      continue;
    auto *status = new juce::DynamicObject();
    status->setProperty("kind", juce::String(libraryMapKindName(row.kind)));
    status->setProperty("entityId", juce::String(row.entityId));
    auto found = findIn(juce::File(row.sourcePath), row.kind, row.entityId);
    if (!found) {
      // The file moved or no longer holds the map: look in Dorico's folders.
      if (!doricoFiles)
        doricoFiles = doricolibFilesIn(doricoMapSources());
      for (const auto &file : *doricoFiles)
        if ((found = findIn(file, row.kind, row.entityId))) {
          status->setProperty("movedTo", file.getFullPathName());
          break;
        }
    }
    if (!found) {
      status->setProperty("status", "missing");
    } else {
      status->setProperty("version", found->version);
      status->setProperty("file", found->file.getFullPathName());
      status->setProperty("status", found->hash.toStdString() == row.sourceHash ? "current"
                                    : found->version > row.sourceVersion       ? "newer"
                                                                               : "differs");
    }
    statuses.add(juce::var(status));
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("libraryId", libraryId);
  result->setProperty("statuses", statuses);
  broadcastMessage("libraryMapSourceStatus", juce::var(result));
}

void MainComponent::setupLibraryMapSourceHandlers() {
  // [libraryId]
  jsRouter_.registerHandler("checkLibraryMapSources", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString();
    if (libraryId.isNotEmpty())
      safeCallAsync([this, libraryId] { checkLibraryMapSources(libraryId); });
  });
  // [libraryId, kind, entityId, file?]: replace the stored map with the
  // file's (the recorded source unless another file is given).
  jsRouter_.registerHandler("updateLibraryMap", [this](const juce::var &payload) {
    const auto libraryId = argument(payload, 0).toString().toStdString();
    const auto kind = kindArgument(payload, 1);
    const auto entityId = argument(payload, 2).toString().toStdString();
    const auto path = argument(payload, 3).toString();
    if (!kind)
      return;
    safeCallAsync([this, libraryId, kind = *kind, entityId, path] {
      auto *repository = db_.getLibraryRoutingRepository();
      const auto row = repository ? repository->getLibraryMap(libraryId, kind, entityId) : std::nullopt;
      if (!row)
        return;
      const juce::File file(path.isNotEmpty() ? path : juce::String(row->sourcePath));
      juce::String error;
      std::optional<LibraryMapCandidate> found;
      for (const auto &map : readLibraryMapFile(kind, file, error))
        if (map.entityId == entityId)
          found = map;
      const bool success = found && performLibraryMapEdit(libraryId, kind, entityId,
                                                          rowFromCandidate(libraryId, *found),
                                                          "Update " + kindNoun(kind) + ": " + juce::String(row->name));
      libraryMapResult(juce::String(libraryId), kind, "update", success,
                       success ? "Updated " + juce::String(row->name) + " from " + file.getFileName()
                               : "Could not update " + juce::String(row->name) +
                                     (error.isNotEmpty() ? ": " + error : juce::String()),
                       juce::String(entityId));
      if (success)
        checkLibraryMapSources(juce::String(libraryId));
    });
  });
}

ExpressionMapSources MainComponent::libraryExpressionMapSources() {
  ExpressionMapSources sources;
  sources.forStrip = [this](const juce::String &stripId, const std::string &entityId) {
    const auto libraryId = layerLibraryId(stripId);
    // A strip with no library keeps the old behaviour: Dorico's folders.
    if (libraryId.empty())
      return ExpressionMapAssignment{xmapLibrary_.load(entityId), {}, {}};
    return loadLibraryExpressionMap(libraryId, entityId);
  };
  sources.forLibrary = [this](const juce::String &libraryId, const std::string &entityId) {
    return loadLibraryExpressionMap(libraryId.toStdString(), entityId);
  };
  sources.importForStrip = [this](const juce::String &stripId, const juce::File &file) {
    return importMapFileForStrip(stripId, LibraryMapKind::expression, file);
  };
  return sources;
}

} // namespace fiddle
