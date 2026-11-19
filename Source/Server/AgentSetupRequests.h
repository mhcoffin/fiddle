#pragma once

// Request logic for the agent setup tools (docs/mcp-control.md) that doesn't
// need the running application: searching Dorico's instruments, naming a
// kit's chairs, and merging a library edit into the stored library. The
// MainComponent side (AgentControl.cpp) supplies lookups and performs the
// resulting undoable actions.

#include "DoricoInstrumentBrowser.h"
#include "LibraryRoutingRepository.h"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace fiddle::agent {

/// Dorico instruments matching a search, best first. Every word of query
/// must appear in the name or entity ID (case-insensitive); an empty query
/// matches everything. family filters by the instrument's top-level family.
/// Variants (instruments with a parent, such as "Clarinet in A") are
/// included only when includeVariants is set.
std::vector<const BrowsableInstrument *>
searchInstruments(const std::vector<BrowsableInstrument> &instruments,
                  const juce::String &query, const juce::String &family,
                  bool includeVariants, int limit);

/// The chair role Fiddle suggests for an instrument: strings and choir
/// default to sections, everything else to solo players (chairModel.js).
juce::String suggestedRole(const BrowsableInstrument &instrument);

/// Chair names for a kit's instruments: each instrument's own name, numbered
/// when several share a name ("Wood Block 1", "Wood Block 2").
std::vector<juce::String> kitChairNames(const std::vector<juce::String> &names);

/// A map a library edit needs imported from a .doricolib file first.
struct MapImport {
  LibraryMapKind kind = LibraryMapKind::expression;
  std::string entityId;
  juce::String path;
  bool operator==(const MapImport &other) const {
    return kind == other.kind && entityId == other.entityId && path == other.path;
  }
};

/// Lookups a library edit is checked against.
struct LibraryEditChecks {
  /// A canonical Dorico instrument ID.
  std::function<bool(const std::string &)> instrumentExists;
  /// An installed instrument plug-in.
  std::function<bool(int)> instrumentPluginAvailable;
  /// The map is in Fiddle's collection or, with a path, in that file.
  std::function<bool(LibraryMapKind, const std::string &, const juce::String &)> mapAvailable;
  /// A percussion map's instruments (from Fiddle or, with a path, the file),
  /// or nullopt when the map is unknown.
  std::function<std::optional<std::vector<std::string>>(const std::string &, const juce::String &)>
      percussionMapInstruments;
  /// Layers using a patch.
  std::function<int(const std::string &)> patchUsageCount;
};

struct LibraryEdit {
  bool ok = false;
  juce::String error;
  LibraryCatalogSnapshot target;
  std::vector<MapImport> imports;
  bool created = false;
  std::vector<std::string> addedPatchIds;
};

/// Merge a library.save request into the stored library (nullopt or
/// !exists for a new one). Fields a patch entry omits keep their stored
/// values; an empty string clears an optional field. New patches go after
/// the existing ones. A patch that changes player or expected preset loses
/// its captured state and needs the user to finish it again.
/// newPatchId supplies IDs for new patches and, when creating, the library.
LibraryEdit mergeLibraryEdit(const std::optional<LibraryCatalogSnapshot> &existing,
                             const juce::var &params, const LibraryEditChecks &checks,
                             const std::function<std::string()> &newId);

} // namespace fiddle::agent
