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

/// Whether text matches a search: every word of query must start a word of
/// text ("hi" matches "Hi-hat" but not "China"), or, for words of four or
/// more letters, appear anywhere once punctuation and spaces are ignored
/// ("hihat" matches "Hi-Hat", "cello" matches "Violoncello"). Case-insensitive;
/// an empty query matches everything.
bool matchesSearch(const juce::String &text, const juce::String &query);

/// Dorico instruments matching a search (matchesSearch on the name and entity
/// ID), best first. An empty query matches everything. family filters by the
/// instrument's top-level family. Variants (instruments with a parent, such as "Clarinet in A") are
/// included only when includeVariants is set.
std::vector<const BrowsableInstrument *>
searchInstruments(const std::vector<BrowsableInstrument> &instruments,
                  const juce::String &query, const juce::String &family,
                  bool includeVariants, int limit);

/// Whether a percussion map's instruments cover a Dorico instrument, treating
/// aliases ("instrument.band.unpitched.tomtom.high.alias.tom-high") as their
/// base instrument, as playback does.
bool coversInstrument(const std::vector<std::string> &listed, const std::string &instrument);

/// What a capture request for a patch's player window should do
/// (fiddle_capture_patch_player, docs/mcp-control.md).
enum class PlayerCapture {
  capture,         // save the window's state into the patch
  alreadyCaptured, // a repeated answer for a capture that already happened
  stale,           // an answer for a window that has since closed or reopened
  notOpen,
  loading,
  failed,
  unchanged,       // the user hasn't changed the player
  patchChanged     // the patch was edited while the window was open
};

struct PlayerSessionState {
  bool open = false;
  std::string session;      // the open window's session ID
  juce::String status;      // loading, loaded or failed
  bool changed = false;
  std::string baseline;     // patchBaseline() when the window opened
  std::string lastCaptured; // session ID of the patch's last capture
};

/// What a capture depends on in a patch: its saved content (revision), its
/// player, the preset the user was asked for, and whether it is finished.
/// The revision alone misses the last two, which repository saves don't count
/// as content changes.
std::string patchBaseline(const LibraryPatchRow &patch);

/// requestedSession is empty when the caller doesn't name a session (the
/// plain tool), which means the open window. currentBaseline is patchBaseline() of
/// the patch now.
PlayerCapture decidePlayerCapture(const PlayerSessionState &state,
                                  const std::string &requestedSession, bool allowUnchanged,
                                  const std::string &currentBaseline);

/// Whether a chair's bridge map lets its notes play, as the Chairs window
/// says it (ui/src/lib/bridgeStatus.js). state is one of: playing,
/// differentVersion (Dorico has an older or edited copy; notes are muted),
/// notInProjectTemplate (Dorico sent other chairs their maps but nothing to
/// this one), checkOnPlayback (checked when playback starts), unchecked (the
/// map sends no version ID and isn't verified; notes are muted).
struct BridgeCheck {
  juce::String state;
  juce::String message;
};

/// beacon is the live check's result (unsupported, absent, matched or
/// mismatched), empty when there is none. othersReported: another chair's
/// map has reported a version since playback started.
BridgeCheck bridgeCheck(const juce::String &beacon, bool verified, bool othersReported);

/// Whether fiddle_get_bridge_maps lists a chair as needing attention: its
/// notes are muted or not sent, or some notes weren't played, even when the
/// map itself checks out (out of range, pitch not in the percussion map).
bool bridgeNeedsAttention(const juce::String &state, juce::int64 notesNotPlayed);

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
