#pragma once

#include "MixerModel.h"
#include "SqliteStatement.h"
#include "SqliteVersionStorage.h"
#include <juce_core/juce_core.h>
#include <mutex>
#include <set>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace fiddle {

/// Row data returned when loading strips from the database.
struct StripRow {
  juce::String id, library, family;
  bool isSolo = true;
  bool active = true;
  int position = 0;
  int inputPort = -1, inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityID;
  juce::MemoryBlock pluginState; // binary BLOB
  std::vector<std::string> luaPluginFileNames; // ordered Lua plugin filenames
};

/// Metadata for a saved configuration version.
struct SavedConfigInfo {
  juce::String name;
  juce::String version; // ISO 8601 timestamp
};

/// Persisted window geometry and state.
struct WindowSettings {
  juce::String windowId; // "main" or "debug"
  int x = 0, y = 0;
  int width = 800, height = 600;
  bool visible = true;
  juce::String mode = "mixer"; // main window only
  bool found = false;          // true if loaded from DB
};

/// Cached plugin metadata from scanning.
struct PluginCacheRow {
  juce::String path;
  int64_t modTime = 0;
  juce::String name, manufacturer, category, format;
  int uid = 0;
  int numInputs = 0, numOutputs = 0;
  bool valid = true;
};

/// A single channel assignment (flat_index → instrument).
struct ChannelAssignmentRow {
  int flatIndex = 0;
  juce::String entityID;
  bool isSolo = true;
  int instanceNum = 1;
};

/// Row data for a sample library.
struct LibraryRow {
  juce::String id;
  juce::String name;
  juce::String vendor;
  juce::String variant;
};

/// Row data for an instrument within a library.
struct LibraryInstrumentRow {
  juce::String libraryId;
  int sortOrder = 0;
  juce::String entityId;
  juce::String name;
  juce::String family;
  juce::String category;
  bool isSolo = true;
  juce::String vstPlugin;
  juce::String exprMap;
  int pluginUid = 0;                // scanned plugin uniqueId
  juce::MemoryBlock pluginState;    // binary VST state blob
  std::vector<int> instanceNums{1}; // Dorico instance numbers this row fills
  juce::String note;                 // user-facing short text note

  /// Check if this row covers a specific instance number.
  bool hasInstance(int n) const {
    return std::find(instanceNums.begin(), instanceNums.end(), n) !=
           instanceNums.end();
  }

  /// Maximum instance number in the set.
  int maxInstance() const {
    if (instanceNums.empty()) return 1;
    return *std::max_element(instanceNums.begin(), instanceNums.end());
  }

  /// Serialize instanceNums to comma-separated string for DB storage.
  juce::String instanceNumsToString() const {
    juce::StringArray parts;
    for (int n : instanceNums) parts.add(juce::String(n));
    return parts.joinIntoString(",");
  }

  /// Parse comma-separated string into instanceNums.
  static std::vector<int> parseInstanceNums(const juce::String &s) {
    std::vector<int> result;
    for (const auto &part : juce::StringArray::fromTokens(s, ",", "")) {
      int n = part.trim().getIntValue();
      if (n > 0) result.push_back(n);
    }
    if (result.empty()) result.push_back(1);
    return result;
  }
};

/// Thread-safe SQLite wrapper for persisting Fiddle server state.
/// Uses WAL mode and prepared statements for performance.
class FiddleDatabase {
public:
  FiddleDatabase();
  ~FiddleDatabase();

  // Non-copyable
  FiddleDatabase(const FiddleDatabase &) = delete;
  FiddleDatabase &operator=(const FiddleDatabase &) = delete;

  /// Open (or create) the database. Call once at startup.
  bool open(const juce::File &dbFile);

  /// Close the database.
  void close();

  bool isOpen() const { return db_ != nullptr; }

  versioning::SqliteVersionStorage *getVersionStorage() const {
    return versionStorage_.get();
  }

  // ── Strip operations (message thread) ──────────────────────────────

  /// Save a strip's metadata (no BLOB). Fast, called on every mutation.
  void saveStrip(const MixerStrip &strip, int position);

  /// Save plugin binary state (BLOB) for a strip. Called on background thread.
  void savePluginBlob(const juce::String &stripId,
                      const juce::MemoryBlock &state);

  /// Remove a strip from the database.
  void removeStrip(const juce::String &stripId);

  /// Remove all strips (e.g. before loading a saved config).
  void clearStrips();

  /// Load all strips, ordered by position.
  std::vector<StripRow> loadAllStrips();

  // ── Settings ───────────────────────────────────────────────────────

  void saveSetting(const std::string &key, const std::string &value);
  std::string loadSetting(const std::string &key,
                          const std::string &defaultValue = "");

  // ── Saved configurations ───────────────────────────────────────────

  /// Snapshot the current strips table into saved_configs under the given name.
  /// Returns the generated version string (ISO 8601 timestamp).
  juce::String saveConfig(const juce::String &name);

  /// Save config data with an explicit version (e.g. from Dorico restore).
  void saveConfigVersion(const juce::String &name, const juce::String &version,
                         const juce::String &jsonData);

  /// Restore strips from a saved config (latest version). Clears current
  /// strips.
  bool loadConfig(const juce::String &name);

  /// Restore strips from a specific version of a saved config.
  bool loadConfigVersion(const juce::String &name, const juce::String &version);

  /// Check if a specific version of a config exists.
  bool hasConfigVersion(const juce::String &name, const juce::String &version);

  /// List all saved configuration names (latest version of each).
  std::vector<SavedConfigInfo> listConfigs();

  /// List all versions of a named config (newest first).
  std::vector<SavedConfigInfo> listConfigVersions(const juce::String &name);

  /// Delete all versions of a saved config.
  void deleteConfig(const juce::String &name);

  // ── Plugin capabilities ────────────────────────────────────────────

  /// Record that a plugin UID fires AudioProcessorListener callbacks.
  void savePluginCapability(int pluginUid);

  /// Load all plugin UIDs known to fire listener callbacks.
  std::set<int> loadListenerCapableUids();

  // ── Window settings ────────────────────────────────────────────────

  /// Save window position, size, visibility, and mode.
  void saveWindowSettings(const WindowSettings &ws);

  /// Load window settings by id ("main" or "debug"). Returns defaults if
  /// not found.
  WindowSettings loadWindowSettings(const juce::String &windowId);

  // ── Plugin cache ───────────────────────────────────────────────────

  /// Insert or update a plugin cache entry.
  void savePluginCacheEntry(const PluginCacheRow &row);

  /// Load all cached plugin entries.
  std::vector<PluginCacheRow> loadPluginCache();

  /// Remove a plugin entry by path.
  void removePluginCacheEntry(const juce::String &path);

  /// Clear the entire plugin cache (for full rescan).
  void clearPluginCache();

  // ── Ensemble slots ────────────────────────────────────────────────

  /// Replace all ensemble slots with the given JSON array.
  void saveEnsemble(const juce::String &slotsJson);

  /// Load ensemble slots as a JSON array string. Returns empty string if
  /// no rows exist.
  juce::String loadEnsemble();

  // ── Channel assignments ─────────────────────────────────────────

  /// Save all channel assignments (replaces existing).
  void saveChannelAssignments(const std::vector<ChannelAssignmentRow> &rows);

  /// Load all channel assignments.
  std::vector<ChannelAssignmentRow> loadChannelAssignments();

  /// Move an assignment row to the graveyard.
  void moveToGraveyard(const ChannelAssignmentRow &row);

  /// Try to reclaim a graveyard entry for a given entityID + isSolo.
  /// Returns the flat_index if found, or -1.
  int reclaimFromGraveyard(const juce::String &entityID, bool isSolo);

  /// Load all graveyard entries.
  std::vector<ChannelAssignmentRow> loadGraveyard();

  /// Clear the entire graveyard.
  void clearGraveyard();

  // ── Libraries ──────────────────────────────────────────────────────

  /// Save (upsert) a library and its instruments in a single transaction.
  void saveLibrary(const LibraryRow &lib,
                   const std::vector<LibraryInstrumentRow> &instruments);

  /// List all libraries (header info only).
  std::vector<LibraryRow> listLibraries();

  /// Load all instruments for a given library.
  std::vector<LibraryInstrumentRow>
  loadLibraryInstruments(const juce::String &libraryId);

  /// Delete a library and its instruments.
  void deleteLibrary(const juce::String &libraryId);

  /// Load all instruments across all libraries (for Dorico template).
  std::vector<LibraryInstrumentRow> loadAllLibraryInstruments();

private:
  void createSchema();
  void prepareStatements();
  void finalizeStatements();

  /// Execute a simple SQL statement (no parameters/results).
  bool exec(const char *sql);

  sqlite3 *db_ = nullptr;
  mutable std::mutex mutex_;
  std::unique_ptr<versioning::SqliteVersionStorage> versionStorage_;

  // Prepared statements
  SqliteStatement stmtSaveStrip_;
  SqliteStatement stmtSaveBlob_;
  SqliteStatement stmtRemoveStrip_;
  SqliteStatement stmtClearStrips_;
  SqliteStatement stmtLoadStrips_;
  SqliteStatement stmtSaveSetting_;
  SqliteStatement stmtLoadSetting_;
  SqliteStatement stmtSaveConfig_;
  SqliteStatement stmtSaveConfigVersion_;
  SqliteStatement stmtLoadConfig_;
  SqliteStatement stmtLoadConfigVersion_;
  SqliteStatement stmtHasConfigVersion_;
  SqliteStatement stmtListConfigs_;
  SqliteStatement stmtListConfigVersions_;
  SqliteStatement stmtDeleteConfig_;
  SqliteStatement stmtSavePluginCapability_;
  SqliteStatement stmtLoadListenerUids_;
  SqliteStatement stmtSaveWindowSettings_;
  SqliteStatement stmtLoadWindowSettings_;
  SqliteStatement stmtSavePluginCache_;
  SqliteStatement stmtLoadPluginCache_;
  SqliteStatement stmtRemovePluginCache_;
  SqliteStatement stmtClearPluginCache_;
  SqliteStatement stmtClearEnsemble_;
  SqliteStatement stmtInsertEnsembleSlot_;
  SqliteStatement stmtLoadEnsemble_;
  SqliteStatement stmtClearAssignments_;
  SqliteStatement stmtInsertAssignment_;
  SqliteStatement stmtLoadAssignments_;
  SqliteStatement stmtClearGraveyard_;
  SqliteStatement stmtInsertGraveyard_;
  SqliteStatement stmtFindGraveyard_;
  SqliteStatement stmtRemoveGraveyard_;
  SqliteStatement stmtLoadGraveyard_;

  // Library statements
  SqliteStatement stmtSaveLibrary_;
  SqliteStatement stmtDeleteLibrary_;
  SqliteStatement stmtDeleteLibraryInstruments_;
  SqliteStatement stmtInsertLibraryInstrument_;
  SqliteStatement stmtListLibraries_;
  SqliteStatement stmtLoadLibraryInstruments_;
  SqliteStatement stmtLoadAllLibraryInstruments_;
};

} // namespace fiddle
