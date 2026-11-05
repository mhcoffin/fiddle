#include "LibraryMaps.h"

#include "ExpressionMapParser.h"
#include "PercussionMap.h"
#include <juce_cryptography/juce_cryptography.h>
#include <map>
#include <optional>

namespace fiddle {
namespace {

juce::String childText(const juce::XmlElement &element, const char *name) {
  if (auto *child = element.getChildByName(name))
    return child->getAllSubText().trim();
  return {};
}

juce::String normalizedElement(const juce::XmlElement &element) {
  return element.toString(juce::XmlElement::TextFormat().withoutHeader());
}

std::vector<LibraryMapCandidate> readExpressionMaps(const juce::String &text, const juce::File &file,
                                                    juce::String &error) {
  std::vector<LibraryMapCandidate> result;
  if (!text.contains("ExpressionMapDefinition"))
    return result;
  const auto xml = juce::parseXML(text);
  if (!xml) {
    error = "not a readable .doricolib file";
    return result;
  }
  const auto *definitions = xml->getChildByName("expressionMapDefinitions");
  const auto *entities = definitions ? definitions->getChildByName("entities") : nullptr;
  if (!entities)
    return result;
  for (const auto *definition : entities->getChildIterator()) {
    if (!definition->hasTagName("ExpressionMapDefinition"))
      continue;
    LibraryMapCandidate candidate;
    candidate.kind = LibraryMapKind::expression;
    candidate.entityId = childText(*definition, "entityID").toStdString();
    if (candidate.entityId.empty())
      continue;
    candidate.name = childText(*definition, "name").toStdString();
    candidate.version = childText(*definition, "version").getIntValue();
    candidate.creator = childText(*definition, "creator").toStdString();
    candidate.pluginNames = childText(*definition, "pluginNames").toStdString();
    candidate.file = file;
    candidate.definitionXml = normalizedElement(*definition);
    candidate.hash = libraryMapHash(candidate.definitionXml);
    result.push_back(std::move(candidate));
  }
  return result;
}

std::vector<LibraryMapCandidate> readPercussionMaps(const juce::String &text, const juce::File &file,
                                                    juce::String &error) {
  std::vector<LibraryMapCandidate> result;
  if (!text.contains("DrumKitNoteMapDefinition"))
    return result;
  const auto maps = PercussionMap::parseLibrary(text, error);
  for (const auto &map : maps) {
    LibraryMapCandidate candidate;
    candidate.kind = LibraryMapKind::percussion;
    candidate.entityId = map.entityId;
    candidate.name = map.name;
    candidate.version = map.version;
    candidate.file = file;
    candidate.definitionXml = map.toDefinitionXml();
    candidate.hash = libraryMapHash(candidate.definitionXml);
    result.push_back(std::move(candidate));
  }
  return result;
}

} // namespace

std::vector<LibraryMapCandidate> readLibraryMaps(LibraryMapKind kind, const juce::String &text,
                                                 const juce::File &file, juce::String &error) {
  return kind == LibraryMapKind::percussion ? readPercussionMaps(text, file, error)
                                            : readExpressionMaps(text, file, error);
}

std::vector<LibraryMapCandidate> readLibraryMapFile(LibraryMapKind kind, const juce::File &file,
                                                    juce::String &error) {
  if (!file.existsAsFile()) {
    error = "file not found";
    return {};
  }
  return readLibraryMaps(kind, file.loadFileAsString(), file, error);
}

juce::String libraryMapHash(const juce::String &definitionXml) {
  return juce::SHA256(definitionXml.toUTF8()).toHexString();
}

juce::String expressionMapLibraryXml(const juce::String &definitionXml) {
  juce::XmlElement root("kScoreLibrary");
  root.createNewChildElement("fileVersion")->addTextElement("1.1438");
  auto *entities = root.createNewChildElement("expressionMapDefinitions")->createNewChildElement("entities");
  entities->setAttribute("array", "true");
  if (auto definition = juce::parseXML(definitionXml))
    entities->addChildElement(definition.release());
  return root.toString();
}

std::shared_ptr<ExpressionMapData> compileExpressionMap(const juce::String &definitionXml) {
  if (definitionXml.isEmpty())
    return nullptr;
  auto result = std::make_shared<ExpressionMapData>();
  if (!parseExpressionMapXml(expressionMapLibraryXml(definitionXml), *result))
    return nullptr;
  return result;
}

int libraryMapDefinitionVersion(LibraryMapKind kind, const juce::String &definitionXml) {
  if (kind == LibraryMapKind::percussion) {
    const auto map = compilePercussionMap(definitionXml);
    return map ? map->version : 0;
  }
  const auto xml = juce::parseXML(definitionXml);
  return xml ? childText(*xml, "version").getIntValue() : 0;
}

juce::Array<juce::File> doricoLibraryAdditionDirectories() {
  const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
  juce::Array<juce::File> result;
  for (int version = 5; version <= 6; ++version) {
    const auto dir = home.getChildFile("Library/Application Support/Steinberg/Dorico " +
                                       juce::String(version) + "/DefaultLibraryAdditions");
    if (dir.isDirectory())
      result.add(dir);
  }
  return result;
}

juce::Array<juce::File> doricoMapSources() {
  auto result = doricoLibraryAdditionDirectories();
  for (int version = 6; version >= 5; --version) {
    const auto resources = juce::File("/Applications/Dorico " + juce::String(version) + ".app/Contents/Resources");
    for (const auto *name : {"expressionMapsDefinitions.xml", "drumKitNoteMaps.xml"})
      if (const auto file = resources.getChildFile(name); file.existsAsFile())
        result.add(file);
  }
  return result;
}

juce::Array<juce::File> doricolibFilesIn(const juce::Array<juce::File> &sources) {
  juce::Array<juce::File> files;
  for (const auto &source : sources) {
    if (source.isDirectory())
      files.addArray(source.findChildFiles(juce::File::findFiles, true, "*.doricolib"));
    else if (source.existsAsFile())
      files.add(source);
  }
  return files;
}

namespace {
LibraryMapRow migratedRow(const std::string &libraryId, const LibraryMapCandidate &candidate, bool keepSource) {
  LibraryMapRow row;
  row.libraryId = libraryId;
  row.kind = candidate.kind;
  row.entityId = candidate.entityId;
  row.name = candidate.name;
  row.xml = candidate.definitionXml.toStdString();
  if (keepSource) {
    row.sourcePath = candidate.file.getFullPathName().toStdString();
    row.sourceVersion = candidate.version;
    row.sourceHash = candidate.hash.toStdString();
  }
  return row;
}
} // namespace

LibraryMapMigrationReport
migrateLibraryMaps(LibraryRoutingRepository &repository, const std::vector<juce::File> &catalogFiles,
                   const juce::Array<juce::File> &doricoSources,
                   const std::function<juce::String(const std::string &)> &savedExpressionMapSource,
                   bool findPercussionSources) {
  LibraryMapMigrationReport report;
  std::map<std::string, std::vector<LibraryMapCandidate>> cache;
  const auto mapsIn = [&cache](const juce::File &file, LibraryMapKind kind) -> const std::vector<LibraryMapCandidate> & {
    const auto key = std::string(libraryMapKindName(kind)) + ":" + file.getFullPathName().toStdString();
    if (!cache.count(key)) {
      juce::String error;
      cache[key] = readLibraryMapFile(kind, file, error);
    }
    return cache[key];
  };
  const auto missing = repository.missingLibraryMapReferences();
  bool anyExpression = false;
  for (const auto &reference : missing)
    anyExpression = anyExpression || reference.kind == LibraryMapKind::expression;

  // Index the catalog's expression maps once, keeping each map's highest
  // version (as ExpressionMapLibrary does). Dorico's built-in map files come
  // last and only fill gaps.
  std::map<std::string, LibraryMapCandidate> catalog;
  if (anyExpression) {
    for (const auto &file : catalogFiles)
      for (const auto &candidate : mapsIn(file, LibraryMapKind::expression)) {
        const auto existing = catalog.find(candidate.entityId);
        if (existing == catalog.end() || candidate.version > existing->second.version)
          catalog[candidate.entityId] = candidate;
      }
    for (const auto &source : doricoSources)
      if (source.existsAsFile())
        for (const auto &candidate : mapsIn(source, LibraryMapKind::expression))
          catalog.emplace(candidate.entityId, candidate);
  }

  for (const auto &reference : missing) {
    if (reference.kind != LibraryMapKind::expression)
      continue;
    std::optional<LibraryMapRow> row;
    if (const auto found = catalog.find(reference.entityId); found != catalog.end()) {
      row = migratedRow(reference.libraryId, found->second, true);
    } else if (savedExpressionMapSource) {
      juce::String error;
      for (const auto &candidate :
           readLibraryMaps(LibraryMapKind::expression, savedExpressionMapSource(reference.entityId), {}, error))
        if (candidate.entityId == reference.entityId)
          row = migratedRow(reference.libraryId, candidate, false);
    }
    if (!row) {
      report.missing.push_back(reference.libraryId + " / " + reference.entityId);
      continue;
    }
    if (repository.upsertLibraryMap(*row))
      ++report.expressionCopied;
  }

  if (findPercussionSources) {
    std::map<std::string, LibraryMapCandidate> dorico;
    for (const auto &file : doricolibFilesIn(doricoSources))
      if (!file.getFileName().startsWith("Fiddle"))
        for (const auto &candidate : mapsIn(file, LibraryMapKind::percussion))
          dorico.emplace(candidate.entityId, candidate);
    for (auto row : repository.listLibraryMaps({}, LibraryMapKind::percussion)) {
      const auto match = dorico.find(row.entityId);
      if (!row.sourcePath.empty() || match == dorico.end())
        continue;
      row.sourcePath = match->second.file.getFullPathName().toStdString();
      row.sourceVersion = match->second.version;
      row.sourceHash = match->second.hash.toStdString();
      if (repository.upsertLibraryMap(row))
        ++report.percussionSourced;
    }
  }
  return report;
}

} // namespace fiddle
