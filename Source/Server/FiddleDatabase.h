#pragma once

#include "MixerModel.h"
#include <juce_core/juce_core.h>
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace fiddle {

/// Row data returned when loading strips from the database.
struct StripRow {
  juce::String id, library, family;
  bool isSolo = true;
  int position = 0;
  int inputPort = -1, inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityID;
  juce::MemoryBlock pluginState; // binary BLOB
};

/// Metadata for a saved configuration.
struct SavedConfigInfo {
  juce::String name;
  juce::String createdAt;
  juce::String updatedAt;
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
  void saveConfig(const juce::String &name);

  /// Restore strips from a saved config (clears current strips first).
  bool loadConfig(const juce::String &name);

  /// List all saved configuration names.
  std::vector<SavedConfigInfo> listConfigs();

  /// Delete a saved config by name.
  void deleteConfig(const juce::String &name);

private:
  void createSchema();
  void prepareStatements();
  void finalizeStatements();

  /// Execute a simple SQL statement (no parameters/results).
  bool exec(const char *sql);

  sqlite3 *db_ = nullptr;
  mutable std::mutex mutex_;

  // Prepared statements
  sqlite3_stmt *stmtSaveStrip_ = nullptr;
  sqlite3_stmt *stmtSaveBlob_ = nullptr;
  sqlite3_stmt *stmtRemoveStrip_ = nullptr;
  sqlite3_stmt *stmtClearStrips_ = nullptr;
  sqlite3_stmt *stmtLoadStrips_ = nullptr;
  sqlite3_stmt *stmtSaveSetting_ = nullptr;
  sqlite3_stmt *stmtLoadSetting_ = nullptr;
  sqlite3_stmt *stmtSaveConfig_ = nullptr;
  sqlite3_stmt *stmtLoadConfig_ = nullptr;
  sqlite3_stmt *stmtListConfigs_ = nullptr;
  sqlite3_stmt *stmtDeleteConfig_ = nullptr;
};

} // namespace fiddle
