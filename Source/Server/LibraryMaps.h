#pragma once

#include "ExpressionMapData.h"
#include "LibraryRoutingRepository.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fiddle {

// Reading, fingerprinting and packaging of library maps
// (docs/library-map-import.md). A stored map is its own definition element:
// ExpressionMapDefinition or DrumKitNoteMapDefinition, normalized so the same
// map always serializes to the same text.

/// One map found in a .doricolib file.
struct LibraryMapCandidate {
  LibraryMapKind kind = LibraryMapKind::expression;
  std::string entityId, name;
  /// The map's own version number in the file.
  int version = 0;
  juce::File file;
  /// The normalized definition element, as stored in library_maps.xml.
  juce::String definitionXml;
  /// libraryMapHash(definitionXml).
  juce::String hash;
  /// Expression maps only: the author's creator and player labels.
  std::string creator, pluginNames;
};

/// Every map of one kind in a .doricolib file's text. Errors (unreadable
/// XML, a percussion map that fails validation) are reported through error;
/// a file with no maps of that kind returns empty with no error.
std::vector<LibraryMapCandidate> readLibraryMaps(LibraryMapKind kind, const juce::String &text,
                                                 const juce::File &file, juce::String &error);
std::vector<LibraryMapCandidate> readLibraryMapFile(LibraryMapKind kind, const juce::File &file,
                                                    juce::String &error);

/// SHA-256 of a normalized definition, as lowercase hex.
juce::String libraryMapHash(const juce::String &definitionXml);

/// A complete .doricolib holding just this expression map definition, for
/// parsing and for export to Dorico.
juce::String expressionMapLibraryXml(const juce::String &definitionXml);

/// Parse a stored expression map definition, or null.
std::shared_ptr<ExpressionMapData> compileExpressionMap(const juce::String &definitionXml);

/// The map's own version number in a stored definition, or 0.
int libraryMapDefinitionVersion(LibraryMapKind kind, const juce::String &definitionXml);

/// Dorico 5 and 6 DefaultLibraryAdditions folders that exist.
juce::Array<juce::File> doricoLibraryAdditionDirectories();

/// Everywhere Dorico keeps maps: the DefaultLibraryAdditions folders plus
/// Dorico's own built-in map files (expressionMapsDefinitions.xml,
/// drumKitNoteMaps.xml), newest Dorico first.
juce::Array<juce::File> doricoMapSources();

/// Every .doricolib under the given files and folders (recursive).
juce::Array<juce::File> doricolibFilesIn(const juce::Array<juce::File> &sources);

/// What the one-time migration did (docs/library-map-import.md, Migration).
struct LibraryMapMigrationReport {
  int expressionCopied = 0;
  int percussionSourced = 0;
  /// "libraryId / entityId" of expression maps found nowhere.
  std::vector<std::string> missing;
};

/// Copy each expression map a library uses into that library: from the
/// catalog files (Dorico's folders and remembered folders), then Dorico's
/// built-in map files, then a saved project version's copy. With
/// findPercussionSources, record the Dorico file each sourceless percussion
/// map came from. Safe to rerun: maps already copied are left alone.
LibraryMapMigrationReport
migrateLibraryMaps(LibraryRoutingRepository &repository, const std::vector<juce::File> &catalogFiles,
                   const juce::Array<juce::File> &doricoSources,
                   const std::function<juce::String(const std::string &)> &savedExpressionMapSource,
                   bool findPercussionSources);

} // namespace fiddle
