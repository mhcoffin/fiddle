// Fiddle's expression and percussion maps (docs/maps.md): lookups for
// playback, catalog edits, import/export, favorites, layer assignment, the
// startup migration and source update checks.
#include "FileChooserLaunch.h"
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

LibraryMapRow rowFromCandidate(const LibraryMapCandidate &candidate) {
  LibraryMapRow row;
  row.kind = candidate.kind;
  row.entityId = candidate.entityId;
  row.name = candidate.name;
  row.xml = candidate.definitionXml.toStdString();
  row.sourcePath = candidate.file.getFullPathName().toStdString();
  row.sourceVersion = candidate.version;
  row.sourceHash = candidate.hash.toStdString();
  return row;
}

// How a file's map compares with Fiddle's copy.
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

PercussionMapAssignment MainComponent::loadPercussionMap(const std::string &entityId) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (entityId.empty() || !repository)
    return {};
  const auto row = repository->getMap(LibraryMapKind::percussion, entityId);
  if (!row)
    return {};
  const auto xml = juce::String::fromUTF8(row->xml.c_str());
  return {compilePercussionMap(xml), xml};
}

ExpressionMapAssignment MainComponent::loadStoredExpressionMap(const std::string &entityId) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (entityId.empty() || !repository)
    return {};
  const auto row = repository->getMap(LibraryMapKind::expression, entityId);
  if (!row)
    return {};
  const auto definition = juce::String::fromUTF8(row->xml.c_str());
  auto data = compileExpressionMap(definition);
  if (!data)
    return {};
  // The source text is a complete .doricolib so project versions restore it.
  return {std::move(data), juce::String(row->sourcePath), expressionMapLibraryXml(definition)};
}

void MainComponent::syncLayersToChangedMaps() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  auto now = mapDefinitions(repository->listMaps());
  const auto changed = changedMaps(catalogMapSnapshot_, now);
  catalogMapSnapshot_ = std::move(now);
  if (!changed.empty())
    refreshLiveLibraryMaps(&changed);
}

void MainComponent::refreshLiveLibraryMaps(const std::set<MapKey> *only) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  const auto wanted = [only](LibraryMapKind kind, const std::string &entityId) {
    return !only || only->count({kind, entityId}) > 0;
  };
  bool changed = false;
  for (const auto &layer : repository->listLayers()) {
    auto *strip = mixer_.getStrip(juce::String(layer.id));
    if (!strip)
      continue;
    const auto percussion = strip->snapshotPercussionMap();
    if (percussion.map && wanted(LibraryMapKind::percussion, percussion.map->entityId)) {
      auto latest = keepingPlayAs(loadPercussionMap(percussion.map->entityId), percussion.playAs);
      if (latest.map && (latest.sourceXml != percussion.sourceXml || latest.playAs != percussion.playAs)) {
        strip->setPercussionMapAssignment(std::move(latest));
        changed = true;
      }
    }
    const auto expression = strip->snapshotExpressionMap();
    if (expression.data && wanted(LibraryMapKind::expression, expression.data->entityID)) {
      auto latest = loadStoredExpressionMap(expression.data->entityID);
      if (latest.data && latest.sourceXml != expression.sourceXml) {
        strip->setExpressionMapAssignment(std::move(latest));
        changed = true;
      }
    }
  }
  if (changed) {
    saveAllStripsToDB(false);
    scheduleStateRebuild();
    pushMixerState(false); // the layers' map names, whichever path changed them
  }
}

namespace {
juce::var mapUseJson(const MapUse &use) {
  auto *item = new juce::DynamicObject();
  item->setProperty("type", use.layer ? "layer" : "patch");
  item->setProperty("patchId", juce::String(use.patchId));
  item->setProperty("libraryId", juce::String(use.libraryId));
  item->setProperty("instrument", juce::String(use.instrument));
  item->setProperty("label", juce::String(use.label));
  return juce::var(item);
}
} // namespace

// Usage changes with every layer and patch edit, so it also travels on its
// own, light message (setLibraryMapUsage) that the UI merges into the maps.
void MainComponent::pushMapUsage() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  juce::Array<juce::var> result;
  for (const auto &use : repository->listMapUsage()) {
    auto item = mapUseJson(use);
    item.getDynamicObject()->setProperty("kind", juce::String(libraryMapKindName(use.kind)));
    item.getDynamicObject()->setProperty("entityId", juce::String(use.entityId));
    result.add(item);
  }
  broadcastMessage("setLibraryMapUsage", juce::var(result));
}

void MainComponent::pushLibraryMaps() {
  juce::Array<juce::var> result;
  if (auto *repository = db_.getLibraryRoutingRepository()) {
    // Who uses each map: the picker's suggestions and the Used by column.
    std::map<std::pair<LibraryMapKind, std::string>, std::vector<MapUse>> usage;
    for (auto &use : repository->listMapUsage())
      usage[{use.kind, use.entityId}].push_back(std::move(use));
    for (const auto &row : repository->listMaps()) {
      const auto definition = juce::String::fromUTF8(row.xml.c_str());
      auto *obj = new juce::DynamicObject();
      obj->setProperty("kind", juce::String(libraryMapKindName(row.kind)));
      obj->setProperty("entityId", juce::String(row.entityId));
      obj->setProperty("name", juce::String(row.name));
      obj->setProperty("revision", row.revision);
      obj->setProperty("version", libraryMapDefinitionVersion(row.kind, definition));
      obj->setProperty("sourcePath", juce::String(row.sourcePath));
      obj->setProperty("sourceVersion", row.sourceVersion);
      obj->setProperty("edited", row.edited);
      obj->setProperty("favorite", row.favorite);
      juce::Array<juce::var> references, uses;
      std::vector<std::string> layerLabels;
      for (const auto &use : usage[{row.kind, row.entityId}]) {
        uses.add(mapUseJson(use));
        if (use.layer)
          layerLabels.push_back(use.label);
        else
          references.add(juce::String(use.label));
      }
      for (const auto &label : layerLabels)
        references.add(juce::String(label));
      obj->setProperty("references", references);
      obj->setProperty("uses", uses);
      if (row.kind == LibraryMapKind::percussion) {
        const auto map = compilePercussionMap(definition);
        juce::Array<juce::var> instruments;
        if (map)
          for (const auto &id : map->instruments())
            instruments.add(juce::String(id));
        obj->setProperty("instruments", instruments);
        obj->setProperty("entryCount", map ? int(map->entries.size()) : 0);
        obj->setProperty("map", map ? map->toJson() : juce::var());
      } else {
        const auto labels = expressionMapLabels(definition);
        obj->setProperty("creator", juce::String(labels.creator));
        obj->setProperty("pluginNames", juce::String(labels.pluginNames));
      }
      result.add(juce::var(obj));
    }
  }
  broadcastMessage("setLibraryMaps", juce::var(result));
}

void MainComponent::libraryMapResult(LibraryMapKind kind, const juce::String &action, bool success,
                                     const juce::String &message, const juce::String &entityId,
                                     const juce::String &requestId, const juce::StringArray &imported) {
  auto *obj = new juce::DynamicObject();
  obj->setProperty("kind", juce::String(libraryMapKindName(kind)));
  obj->setProperty("action", action);
  obj->setProperty("success", success);
  obj->setProperty("message", message);
  obj->setProperty("entityId", entityId);
  obj->setProperty("requestId", requestId);
  juce::Array<juce::var> importedIds;
  for (const auto &id : imported)
    importedIds.add(id);
  obj->setProperty("imported", importedIds);
  broadcastMessage("libraryMapResult", juce::var(obj));
}

bool MainComponent::performLibraryMapEdit(LibraryMapKind kind, const std::string &entityId,
                                          std::optional<LibraryMapRow> target,
                                          const juce::String &description) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return false;
  auto action = std::make_unique<LibraryMapCatalogAction>(*repository, kind, entityId, std::move(target),
                                                          description);
  const bool success = action->isNoOp() || libraryUndoManager_.perform(std::move(action));
  if (success) {
    syncLayersToChangedMaps();
    pushLibraryCatalogHistory();
    pushMixerState(false);
  }
  pushLibraryMaps();
  return success;
}

bool MainComponent::importLibraryMap(const LibraryMapCandidate &candidate) {
  return performLibraryMapEdit(candidate.kind, candidate.entityId, rowFromCandidate(candidate),
                               "Import " + kindNoun(candidate.kind) + ": " + juce::String(candidate.name));
}

void MainComponent::offerLibraryMapImport(LibraryMapKind kind, const juce::Array<juce::File> &sources,
                                          const juce::String &origin, const juce::String &requestId) {
  auto *repository = db_.getLibraryRoutingRepository();
  std::map<std::string, int> useCounts;
  if (repository)
    for (const auto &use : repository->listMapUsage())
      if (use.kind == kind)
        ++useCounts[use.entityId];
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
      const auto existing = repository ? repository->getMap(kind, map.entityId) : std::nullopt;
      auto *candidate = new juce::DynamicObject();
      candidate->setProperty("entityId", juce::String(map.entityId));
      candidate->setProperty("name", juce::String(map.name));
      candidate->setProperty("version", map.version);
      candidate->setProperty("file", map.file.getFullPathName());
      candidate->setProperty("status", importStatus(existing, map));
      candidate->setProperty("existingVersion",
                             existing ? libraryMapDefinitionVersion(kind, juce::String::fromUTF8(existing->xml.c_str()))
                                      : 0);
      // Replacing Fiddle's copy changes every patch and layer that uses it.
      candidate->setProperty("references", existing ? useCounts[map.entityId] : 0);
      candidate->setProperty("creator", juce::String(map.creator));
      candidate->setProperty("pluginNames", juce::String(map.pluginNames));
      candidates.add(juce::var(candidate));
    }
  }
  auto *result = new juce::DynamicObject();
  result->setProperty("kind", juce::String(libraryMapKindName(kind)));
  result->setProperty("requestId", requestId);
  result->setProperty("origin", origin);
  result->setProperty("maps", candidates);
  result->setProperty("errors", errors);
  broadcastMessage("libraryMapImportCandidates", juce::var(result));
}

void MainComponent::assignPercussionMap(const juce::String &stripId, PercussionMapAssignment assignment) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return;
  // A percussion kit's linked layers share their percussion map: each chair
  // resolves it by its own instrument (docs/percussion-kits.md).
  std::vector<juce::String> stripIds{stripId};
  auto *repository = db_.getLibraryRoutingRepository();
  if (const auto layer = repository ? repository->getLayer(stripId.toStdString()) : std::nullopt)
    for (const auto &linked : kitLinkedLayers(*layer))
      if (linked.id != layer->id && mixer_.getStrip(juce::String(linked.id)))
        stripIds.push_back(juce::String(linked.id));
  const auto same = [](const PercussionMapAssignment &a, const PercussionMapAssignment &b) {
    return (a.map ? a.map->entityId : "") == (b.map ? b.map->entityId : "") && a.sourceXml == b.sourceXml &&
           a.playAs == b.playAs;
  };
  std::vector<std::unique_ptr<UndoableAction>> actions;
  for (const auto &id : stripIds) {
    auto before = mixer_.getStrip(id)->snapshotPercussionMap();
    // Each chair keeps its own Play as while the new map still lists it.
    auto after = keepingPlayAs(assignment, before.playAs);
    if (!same(before, after))
      actions.push_back(std::make_unique<SetPercussionMapAction>(mixer_, id, std::move(before), std::move(after)));
  }
  if (actions.empty())
    return;
  const auto description = actions.front()->getDescription() +
                            (actions.size() > 1 ? " for " + juce::String((int)actions.size()) + " kit layers" : "");
  std::unique_ptr<UndoableAction> action = actions.size() == 1
      ? std::move(actions.front())
      : std::make_unique<CompoundAction>(description, std::move(actions));
  if (undoManager_.perform(std::move(action))) {
    saveAllStripsToDB(false);
    pushMixerState();
    pushLibraryLayerStatus();
    scheduleStateRebuild();
  }
}

std::string MainComponent::importExpressionMapFileForStrip(const juce::String &stripId,
                                                           const juce::File &file) {
  juce::String error;
  const auto maps = readLibraryMapFile(LibraryMapKind::expression, file, error);
  auto *repository = db_.getLibraryRoutingRepository();
  if (maps.empty() || !repository || !mixer_.getStrip(stripId)) {
    pushLogMessage("<b>[Maps]</b> " + file.getFileName() + ": " +
                       (error.isNotEmpty() ? error : juce::String("no expression maps")),
                   true);
    return {};
  }
  const auto &chosen = maps.front();
  const auto existing = repository->getMap(LibraryMapKind::expression, chosen.entityId);
  const auto status = importStatus(existing, chosen);
  if (status == "same")
    return chosen.entityId;
  if (existing) {
    // Fiddle's copy is shared: don't change other patches and layers from
    // one layer's file. The Maps manager's import shows them and asks.
    const auto references = repository->mapReferences(LibraryMapKind::expression, chosen.entityId);
    if (!references.empty()) {
      pushLogMessage("<b>[Maps]</b> " + juce::String(chosen.name) + " in " + file.getFileName() +
                         " differs from Fiddle's copy, which " + juce::String((int)references.size()) +
                         " patches and layers use. Import it from Expression Maps in the Library "
                         "Manager to replace that copy.",
                     true);
      return {};
    }
  }
  return importLibraryMap(chosen) ? chosen.entityId : std::string{};
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
    pushLogMessage("<b>[Maps]</b> Maps: copied " + juce::String(report.expressionCopied) +
                       " missing expression maps into Fiddle, found sources for " +
                       juce::String(report.percussionSourced) + " percussion maps" +
                       (missing.isEmpty() ? juce::String() : ". Missing: " + missing.joinIntoString(", ")),
                   !missing.isEmpty());
}

void MainComponent::setupLibraryMapHandlers() {
  jsRouter_.registerHandler("requestLibraryMaps", [this](const juce::var &) {
    safeCallAsync([this] { pushLibraryMaps(); });
  });

  // [kind, requestId]: choose .doricolib files or folders to import from.
  jsRouter_.registerHandler("chooseLibraryMapFiles", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto requestId = argument(payload, 1).toString();
    if (!kind)
      return;
    auto chooser = std::make_shared<juce::FileChooser>(
        "Import " + kindNoun(*kind, true) + " (.doricolib file or folder)", juce::File{}, "*.doricolib");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    fiddle::launchFileChooser(*chooser, juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::canSelectDirectories |
                             juce::FileBrowserComponent::canSelectMultipleItems,
                         [safeThis, chooser, kind = *kind, requestId](const juce::FileChooser &fc) {
                           if (!safeThis)
                             return;
                           if (!fc.getResults().isEmpty()) {
                             safeThis->offerLibraryMapImport(kind, fc.getResults(), "file", requestId);
                             return;
                           }
                           // Cancelled: the panel that asked closes its dialog.
                           auto *result = new juce::DynamicObject();
                           result->setProperty("kind", juce::String(libraryMapKindName(kind)));
                           result->setProperty("requestId", requestId);
                           result->setProperty("cancelled", true);
                           safeThis->broadcastMessage("libraryMapImportCandidates", juce::var(result));
                         });
  });

  // [kind, requestId]: every map of that kind in Dorico's library folders.
  jsRouter_.registerHandler("findDoricoLibraryMaps", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto requestId = argument(payload, 1).toString();
    if (!kind)
      return;
    safeCallAsync([this, kind = *kind, requestId] {
      offerLibraryMapImport(kind, doricoMapSources(), "dorico", requestId);
    });
  });

  // [kind, [{file, entityId}], requestId]: import the chosen maps.
  jsRouter_.registerHandler("importLibraryMaps", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto chosen = argument(payload, 1);
    const auto requestId = argument(payload, 2).toString();
    if (!kind || !chosen.isArray())
      return;
    safeCallAsync([this, kind = *kind, chosen, requestId] {
      juce::StringArray imported, failures;
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
        if (match != maps.end() && importLibraryMap(*match))
          imported.add(juce::String(entityId));
        else
          failures.add(juce::String(entityId));
      }
      libraryMapResult(kind, "import", failures.isEmpty(),
                       "Imported " + juce::String(imported.size()) + " " + kindNoun(kind, imported.size() != 1) +
                           (failures.isEmpty() ? juce::String() : ". Failed: " + failures.joinIntoString("; ")),
                       {}, requestId, imported);
    });
  });

  // [percussion map JSON]: create or edit a percussion map.
  jsRouter_.registerHandler("saveLibraryMap", [this](const juce::var &payload) {
    const auto json = argument(payload, 0);
    safeCallAsync([this, json] {
      const auto kind = LibraryMapKind::percussion;
      PercussionMap map;
      juce::String error;
      if (!PercussionMap::fromJson(json, map, error)) {
        libraryMapResult(kind, "save", false, error, json["entityId"].toString());
        return;
      }
      auto *repository = db_.getLibraryRoutingRepository();
      const auto existing = repository ? repository->getMap(kind, map.entityId) : std::nullopt;
      LibraryMapRow row = existing.value_or(LibraryMapRow{});
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
          kind, map.entityId, row,
          (existing ? "Edit percussion map: " : "Create percussion map: ") + juce::String(map.name));
      libraryMapResult(kind, "save", success,
                       success ? "Percussion map saved" : "Could not save the percussion map", id);
    });
  });

  // [kind, entityId]
  jsRouter_.registerHandler("deleteLibraryMap", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto entityId = argument(payload, 1).toString().toStdString();
    if (!kind)
      return;
    safeCallAsync([this, kind = *kind, entityId] {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository)
        return;
      const auto references = repository->mapReferences(kind, entityId);
      if (!references.empty()) {
        juce::StringArray labels;
        for (const auto &label : references)
          labels.add(juce::String(label));
        libraryMapResult(kind, "delete", false, "In use by " + labels.joinIntoString(", "),
                         juce::String(entityId));
        return;
      }
      const auto row = repository->getMap(kind, entityId);
      const bool success = row && performLibraryMapEdit(kind, entityId, std::nullopt,
                                                        "Delete " + kindNoun(kind) + ": " + juce::String(row->name));
      libraryMapResult(kind, "delete", success,
                       success ? "Deleted " + juce::String(row->name) : "Could not delete the " + kindNoun(kind),
                       juce::String(entityId));
    });
  });

  // [kind, entityId, favorite]: a preference, saved at once and not undoable.
  jsRouter_.registerHandler("setLibraryMapFavorite", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto entityId = argument(payload, 1).toString().toStdString();
    const bool favorite = static_cast<bool>(argument(payload, 2));
    if (!kind || entityId.empty())
      return;
    safeCallAsync([this, kind = *kind, entityId, favorite] {
      if (auto *repository = db_.getLibraryRoutingRepository())
        repository->setMapFavorite(kind, entityId, favorite);
      pushLibraryMaps();
    });
  });

  // [kind, entityId]: save the map as a .doricolib for Dorico.
  jsRouter_.registerHandler("exportLibraryMap", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto entityId = argument(payload, 1).toString().toStdString();
    auto *repository = db_.getLibraryRoutingRepository();
    const auto row = kind && repository ? repository->getMap(*kind, entityId) : std::nullopt;
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
    fiddle::launchFileChooser(*chooser, juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [safeThis, chooser, text, row = *row](const juce::FileChooser &fc) {
                           if (!safeThis || fc.getResult() == juce::File{})
                             return;
                           const auto file = fc.getResult().withFileExtension("doricolib");
                           const bool success = file.replaceWithText(text);
                           safeThis->libraryMapResult(row.kind, "export", success,
                                                      success ? "Exported " + file.getFullPathName()
                                                              : "Could not write " + file.getFullPathName(),
                                                      juce::String(row.entityId));
                         });
  });

  // Layers: assign one of Fiddle's percussion maps.
  jsRouter_.registerHandler("loadPercussionMap", [this](const juce::var &payload) {
    const auto stripId = argument(payload, 0).toString();
    const auto entityId = argument(payload, 1).toString().toStdString();
    safeCallAsync([this, stripId, entityId] {
      if (auto assignment = loadPercussionMap(entityId); assignment.map)
        assignPercussionMap(stripId, std::move(assignment));
    });
  });
  // Play as: which of the map's instruments this layer plays (empty for
  // Dorico's choice). Per layer: a kit's linked layers keep their own.
  jsRouter_.registerHandler("setPercussionPlayAs", [this](const juce::var &payload) {
    const auto stripId = argument(payload, 0).toString();
    const auto instrument = argument(payload, 1).toString().toStdString();
    safeCallAsync([this, stripId, instrument] {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip)
        return;
      auto before = strip->snapshotPercussionMap();
      auto after = keepingPlayAs(before, instrument);
      if (!before.map || after.playAs == before.playAs)
        return;
      if (undoManager_.perform(
              std::make_unique<SetPercussionMapAction>(mixer_, stripId, std::move(before), std::move(after)))) {
        saveAllStripsToDB(false);
        pushMixerState();
        pushLibraryLayerStatus();
        scheduleStateRebuild();
      }
    });
  });
  jsRouter_.registerHandler("clearPercussionMap", [this](const juce::var &payload) {
    const auto stripId = argument(payload, 0).toString();
    safeCallAsync([this, stripId] { assignPercussionMap(stripId, {}); });
  });

  setupLibraryMapSourceHandlers();
}

// Source update checks (docs/maps.md, "Storage"): compare each imported map
// with the file it came from.
void MainComponent::checkLibraryMapSources() {
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
  for (const auto &row : repository->listMaps()) {
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
  result->setProperty("statuses", statuses);
  broadcastMessage("libraryMapSourceStatus", juce::var(result));
}

void MainComponent::setupLibraryMapSourceHandlers() {
  jsRouter_.registerHandler("checkLibraryMapSources", [this](const juce::var &) {
    safeCallAsync([this] { checkLibraryMapSources(); });
  });
  // [kind, entityId, file?]: replace the stored map with the file's (the
  // recorded source unless another file is given).
  jsRouter_.registerHandler("updateLibraryMap", [this](const juce::var &payload) {
    const auto kind = kindArgument(payload, 0);
    const auto entityId = argument(payload, 1).toString().toStdString();
    const auto path = argument(payload, 2).toString();
    if (!kind)
      return;
    safeCallAsync([this, kind = *kind, entityId, path] {
      auto *repository = db_.getLibraryRoutingRepository();
      const auto row = repository ? repository->getMap(kind, entityId) : std::nullopt;
      if (!row)
        return;
      const juce::File file(path.isNotEmpty() ? path : juce::String(row->sourcePath));
      juce::String error;
      std::optional<LibraryMapCandidate> found;
      for (const auto &map : readLibraryMapFile(kind, file, error))
        if (map.entityId == entityId)
          found = map;
      const bool success = found && performLibraryMapEdit(kind, entityId, rowFromCandidate(*found),
                                                          "Update " + kindNoun(kind) + ": " + juce::String(row->name));
      libraryMapResult(kind, "update", success,
                       success ? "Updated " + juce::String(row->name) + " from " + file.getFileName()
                               : "Could not update " + juce::String(row->name) +
                                     (error.isNotEmpty() ? ": " + error : juce::String()),
                       juce::String(entityId));
      if (success)
        checkLibraryMapSources();
    });
  });
}

ExpressionMapSources MainComponent::libraryExpressionMapSources() {
  ExpressionMapSources sources;
  sources.stored = [this](const std::string &entityId) { return loadStoredExpressionMap(entityId); };
  sources.importForStrip = [this](const juce::String &stripId, const juce::File &file) {
    return importExpressionMapFileForStrip(stripId, file);
  };
  return sources;
}

} // namespace fiddle
