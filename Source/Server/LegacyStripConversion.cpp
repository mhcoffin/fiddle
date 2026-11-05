#include "LegacyStripConversion.h"

#include "LibraryMaps.h"
#include "PercussionMap.h"

#include <algorithm>
#include <set>

namespace fiddle {
namespace {

std::string libraryLabel(const std::string &label) {
  const auto trimmed = juce::String(label).trim().toStdString();
  return trimmed.empty() ? "Unfiled" : trimmed;
}

std::string titleCase(const std::string &family) {
  auto words = juce::StringArray::fromTokens(juce::String(family).replaceCharacter('-', ' '), " ", "");
  words.removeEmptyStrings();
  for (auto &word : words)
    word = word.substring(0, 1).toUpperCase() + word.substring(1);
  return words.joinIntoString(" ").toStdString();
}

bool isEmptyStrip(const versioning::StripBlob &blob) {
  return blob.pluginUid == 0 && blob.expressionMapEntityId.empty() &&
         blob.percussionMapSourceXml.empty() && blob.luaPluginFileNames.empty() &&
         blob.audioInsertState.empty();
}

std::optional<int> savedInput(const versioning::StripBlob &blob) {
  if (blob.inputPort < 0 || blob.inputChannel < 0 || blob.inputChannel > 15)
    return std::nullopt;
  const int flat = blob.inputPort * 16 + blob.inputChannel;
  // Chair inputs start at 1; index 0 was only ever used by old templates.
  return flat > 0 ? std::optional<int>(flat) : std::nullopt;
}

std::optional<LibraryMapRow> expressionMapRow(const versioning::StripBlob &blob) {
  if (blob.expressionMapEntityId.empty() || blob.expressionMapSourceXml.empty())
    return std::nullopt;
  juce::String error;
  for (const auto &candidate : readLibraryMaps(LibraryMapKind::expression,
                                               juce::String::fromUTF8(blob.expressionMapSourceXml.c_str()),
                                               {}, error)) {
    if (candidate.entityId != blob.expressionMapEntityId)
      continue;
    LibraryMapRow row;
    row.kind = LibraryMapKind::expression;
    row.entityId = candidate.entityId;
    row.name = candidate.name;
    row.xml = candidate.definitionXml.toStdString();
    return row;
  }
  return std::nullopt;
}

std::optional<LibraryMapRow> percussionMapRow(const versioning::StripBlob &blob) {
  const auto map = compilePercussionMap(juce::String::fromUTF8(blob.percussionMapSourceXml.c_str()));
  if (!map)
    return std::nullopt;
  LibraryMapRow row;
  row.kind = LibraryMapKind::percussion;
  row.entityId = map->entityId;
  row.name = map->name;
  row.xml = map->toDefinitionXml().toStdString();
  return row;
}

std::string patchIdFor(const std::string &libraryId, const versioning::StripBlob &blob,
                       const std::string &percussionMapId) {
  std::string key = libraryId;
  key += '\0' + std::to_string(blob.pluginUid) + '\0' + blob.expressionMapEntityId + '\0' + percussionMapId + '\0';
  key.append(reinterpret_cast<const char *>(blob.pluginState.data()), blob.pluginState.size());
  return "restored-" + versioning::xxh3_128(key);
}

} // namespace

std::string restoredLibraryId(const std::string &label) {
  return "restored-" + versioning::xxh3_128("library\n" + libraryLabel(label));
}

std::string restoredLibraryName(const std::string &label) {
  return libraryLabel(label) + " (restored)";
}

std::string standInInstrument(const std::string &family) {
  static const std::map<std::string, std::string> instruments{
      {"strings", "instrument.strings.violin"},
      {"wind", "instrument.wind.flute"},
      {"brass", "instrument.brass.horn.f"},
      {"keyboard", "instrument.keyboard.piano"},
      {"pitched-percussion", "instrument.pitchedpercussion.marimba"},
      {"percussion", "instrument.orchestral.unpitched.snare.drum"},
      {"drum", "instrument.orchestral.unpitched.snare.drum"},
      {"metal", "instrument.orchestral.unpitched.suspended.cymbal"},
  };
  const auto found = instruments.find(family);
  return found == instruments.end() ? "instrument.keyboard.piano" : found->second;
}

LegacyStripConversion convertLegacyStrips(
    const std::vector<std::pair<versioning::Hash, versioning::StripBlob>> &strips,
    const std::vector<ChairRow> &liveChairs,
    const std::function<std::string()> &newId) {
  LegacyStripConversion result;

  // One chair per saved input, in the order strips first use it. Strips with
  // no usable input each get a chair of their own on a fresh input.
  struct Group { std::optional<int> input; std::vector<size_t> strips; };
  std::vector<Group> groups;
  std::map<int, size_t> groupByInput;
  for (size_t i = 0; i < strips.size(); ++i) {
    const auto input = savedInput(strips[i].second);
    if (!input) {
      groups.push_back({std::nullopt, {i}});
      continue;
    }
    const auto [found, inserted] = groupByInput.emplace(*input, groups.size());
    if (inserted)
      groups.push_back({input, {}});
    groups[found->second].strips.push_back(i);
  }

  std::map<int, const ChairRow *> liveByInput;
  for (const auto &chair : liveChairs)
    liveByInput.emplace(chair.flatIndex, &chair);
  int nextInput = 1;
  for (const auto &[input, group] : groupByInput)
    nextInput = std::max(nextInput, input + 1);

  // Chairs copied from the live project keep their ordinals; stand-ins take
  // the next free ordinal for their instrument and role.
  std::map<std::pair<std::string, DoricoRole>, int> lastOrdinal;
  for (const auto &group : groups)
    if (group.input)
      if (const auto live = liveByInput.find(*group.input); live != liveByInput.end()) {
        auto &last = lastOrdinal[{live->second->instrumentEntityId, live->second->role}];
        last = std::max(last, live->second->ordinal);
      }

  std::map<std::string, size_t> libraryIndex;
  std::map<std::string, std::set<std::string>> libraryPatches, libraryMaps;
  auto libraryFor = [&](const std::string &label) -> LibraryCatalogSnapshot & {
    const auto id = restoredLibraryId(label);
    const auto [found, inserted] = libraryIndex.emplace(id, result.libraries.size());
    if (inserted) {
      LibraryCatalogSnapshot library;
      library.id = id;
      library.name = restoredLibraryName(label);
      library.exists = true;
      library.includesMaps = true;
      result.libraries.push_back(std::move(library));
    }
    return result.libraries[found->second];
  };
  auto addMap = [&](LibraryCatalogSnapshot &library, std::optional<LibraryMapRow> row) {
    if (!row || !libraryMaps[library.id].insert(std::string(libraryMapKindName(row->kind)) + ":" + row->entityId).second)
      return;
    row->libraryId = library.id;
    library.maps.push_back(std::move(*row));
  };

  for (size_t g = 0; g < groups.size(); ++g) {
    const auto &group = groups[g];
    const auto &first = strips[group.strips.front()].second;
    ChairRow chair;
    chair.id = newId();
    chair.displayOrder = static_cast<int>(g);
    chair.flatIndex = group.input ? *group.input : nextInput++;
    const auto live = group.input ? liveByInput.find(*group.input) : liveByInput.end();
    if (live != liveByInput.end()) {
      chair.instrumentEntityId = live->second->instrumentEntityId;
      chair.name = live->second->name;
      chair.family = live->second->family;
      chair.role = live->second->role;
      chair.ordinal = live->second->ordinal;
    } else {
      chair.instrumentEntityId = standInInstrument(first.family);
      chair.family = first.family;
      chair.role = first.isSolo ? DoricoRole::solo : DoricoRole::section;
      chair.ordinal = ++lastOrdinal[{chair.instrumentEntityId, chair.role}];
      const auto family = first.family.empty() ? std::string("Strip") : titleCase(first.family);
      chair.name = group.input ? family + " " + std::to_string(chair.flatIndex / 16 + 1) + "/" +
                                     std::to_string(chair.flatIndex % 16 + 1)
                               : family + " " + std::to_string(chair.ordinal);
    }

    int position = 0;
    for (const auto index : group.strips) {
      const auto &[hash, blob] = strips[index];
      if (isEmptyStrip(blob))
        continue;
      auto &library = libraryFor(blob.library);
      const auto expressionMap = expressionMapRow(blob);
      const auto percussionMap = percussionMapRow(blob);
      const auto percussionMapId = percussionMap ? percussionMap->entityId : std::string();
      addMap(library, expressionMap);
      addMap(library, percussionMap);

      const auto patchId = patchIdFor(library.id, blob, percussionMapId);
      std::string patchName = expressionMap ? expressionMap->name
                            : percussionMap ? percussionMap->name
                            : !blob.expressionMapEntityId.empty() ? blob.expressionMapEntityId
                            : chair.name;
      if (libraryPatches[library.id].insert(patchId).second) {
        LibraryPatchRow patch;
        patch.id = patchId;
        patch.libraryId = library.id;
        patch.position = static_cast<int>(library.patches.size());
        patch.name = patchName;
        patch.instrumentEntityId = chair.instrumentEntityId;
        patch.family = blob.family;
        patch.pluginUid = blob.pluginUid;
        patch.pluginState = blob.pluginState;
        patch.expressionMapId = blob.expressionMapEntityId;
        patch.percussionMapId = percussionMapId;
        library.patches.push_back(std::move(patch));
      } else {
        for (const auto &patch : library.patches)
          if (patch.id == patchId)
            patchName = patch.name;
      }

      LayerRow layer;
      layer.id = newId();
      layer.chairId = chair.id;
      layer.patchId = patchId;
      layer.patchName = patchName;
      layer.libraryId = library.id;
      layer.libraryName = library.name;
      layer.position = position++;
      layer.active = blob.active;
      layer.muted = blob.muted;
      layer.soloed = blob.soloed;
      layer.gainDb = blob.gainDb;
      layer.pluginUid = blob.pluginUid;
      layer.pluginState = blob.pluginState;
      layer.expressionMapId = blob.expressionMapEntityId;
      layer.percussionMapId = percussionMapId;
      layer.sourcePatchRevision = 1;
      result.layerHashes.emplace(layer.id, hash);
      result.layers.push_back(std::move(layer));
    }
    result.chairs.push_back(std::move(chair));
  }
  return result;
}

LibraryCatalogSnapshot mergeRestoredLibrary(const std::optional<LibraryCatalogSnapshot> &stored,
                                            const LibraryCatalogSnapshot &generated) {
  if (!stored || !stored->exists)
    return generated;
  auto merged = *stored;
  merged.includesMaps = true;
  std::set<std::string> patchIds, mapKeys;
  for (const auto &patch : merged.patches)
    patchIds.insert(patch.id);
  for (const auto &map : merged.maps)
    mapKeys.insert(std::string(libraryMapKindName(map.kind)) + ":" + map.entityId);
  for (auto patch : generated.patches)
    if (patchIds.insert(patch.id).second) {
      patch.position = static_cast<int>(merged.patches.size());
      merged.patches.push_back(std::move(patch));
    }
  for (const auto &map : generated.maps)
    if (mapKeys.insert(std::string(libraryMapKindName(map.kind)) + ":" + map.entityId).second)
      merged.maps.push_back(map);
  return merged;
}

} // namespace fiddle
