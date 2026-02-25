#pragma once

#include "ExpressionMapData.h"
#include "ExpressionMapParser.h"
#include <juce_core/juce_core.h>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace fiddle {

/// An entry in the expression map library index.
struct ExpressionMapEntry {
  std::string name;
  std::string entityID;
  int version = 0;
  std::string creator;
  juce::File sourceFile;
  int definitionIndex = 0; // index within a multi-definition file
};

/// Discovers, indexes, and loads expression maps from Dorico directories.
/// Deduplicates by entityID, keeping the highest version.
class ExpressionMapLibrary {
public:
  ExpressionMapLibrary() = default;

  /// Scan default Dorico directories for expression maps.
  void scanDefaultDirectories();

  /// Scan a specific directory (recursive) for .doricolib files.
  void scanDirectory(const juce::File &dir);

  /// Add a single .doricolib file to the index.
  void addFile(const juce::File &file);

  /// Get all indexed entries, sorted by name.
  std::vector<ExpressionMapEntry> getEntries() const;

  /// Load a full ExpressionMapData by entityID (parses on demand).
  std::shared_ptr<ExpressionMapData> load(const std::string &entityID);

  /// Serialize the index to JSON for the UI.
  juce::String toJson() const;

  /// Number of known expression maps.
  int size() const;

private:
  mutable std::mutex mutex_;
  // entityID → entry (deduplicated, highest version wins)
  std::map<std::string, ExpressionMapEntry> index_;
};

} // namespace fiddle
