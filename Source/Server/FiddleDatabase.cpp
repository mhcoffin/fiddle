#include "FiddleDatabase.h"
#include <iostream>

namespace fiddle {

FiddleDatabase::FiddleDatabase() = default;

FiddleDatabase::~FiddleDatabase() { close(); }

bool FiddleDatabase::open(const juce::File &dbFile) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_) {
    versionStorage_.reset();
    libraryRoutingRepository_.reset();
    finalizeStatements();
    sqlite3_close(db_);
    db_ = nullptr;
  }

  dbFile.getParentDirectory().createDirectory();

  int rc = sqlite3_open(dbFile.getFullPathName().toRawUTF8(), &db_);
  if (rc != SQLITE_OK) {
    std::cerr << "[FiddleDB] Failed to open: " << sqlite3_errmsg(db_)
              << std::endl;
    db_ = nullptr;
    return false;
  }

  // Enable WAL mode for performance
  exec("PRAGMA foreign_keys=ON");
  exec("PRAGMA journal_mode=WAL");
  exec("PRAGMA synchronous=NORMAL");

  createSchema();
  prepareStatements();
  libraryRoutingRepository_ =
      std::make_unique<LibraryRoutingRepository>(db_, mutex_);
  versionStorage_ =
      std::make_unique<versioning::SqliteVersionStorage>(db_, mutex_);

  std::cerr << "[FiddleDB] Opened: " << dbFile.getFullPathName() << std::endl;
  return true;
}

void FiddleDatabase::close() {
  std::lock_guard<std::mutex> lock(mutex_);
  versionStorage_.reset();
  libraryRoutingRepository_.reset();
  finalizeStatements();
  if (db_) {
    sqlite3_close(db_);
    db_ = nullptr;
  }
}

void FiddleDatabase::createSchema() {
  exec(R"(
    CREATE TABLE IF NOT EXISTS strips (
      id             TEXT PRIMARY KEY,
      position       INTEGER NOT NULL DEFAULT 0,
      library        TEXT NOT NULL DEFAULT '',
      family         TEXT NOT NULL DEFAULT '',
      is_solo        INTEGER NOT NULL DEFAULT 1,
      input_port     INTEGER NOT NULL DEFAULT -1,
      input_channel  INTEGER NOT NULL DEFAULT -1,
      plugin_uid     INTEGER NOT NULL DEFAULT 0,
      gain_db        REAL NOT NULL DEFAULT 0.0,
      expression_map TEXT NOT NULL DEFAULT '',
      plugin_state   BLOB,
      active         INTEGER NOT NULL DEFAULT 1,
      muted          INTEGER NOT NULL DEFAULT 0,
      soloed         INTEGER NOT NULL DEFAULT 0,
      lua_plugins    TEXT NOT NULL DEFAULT ''
    )
  )");

  // Migrate old saved_configs schema if needed
  // Old schema had (name TEXT PRIMARY KEY, data, created_at, updated_at)
  // New schema has (name, version) composite PK
  {
    sqlite3_stmt *checkStmt = nullptr;
    if (sqlite3_prepare_v2(db_, "PRAGMA table_info(saved_configs)", -1,
                           &checkStmt, nullptr) == SQLITE_OK) {
      bool hasCreatedAt = false;
      while (sqlite3_step(checkStmt) == SQLITE_ROW) {
        auto *colName = (const char *)sqlite3_column_text(checkStmt, 1);
        if (colName && std::string(colName) == "created_at")
          hasCreatedAt = true;
      }
      sqlite3_finalize(checkStmt);
      if (hasCreatedAt) {
        std::cerr << "[FiddleDB] Migrating saved_configs to versioned schema"
                  << std::endl;
        exec("DROP TABLE saved_configs");
      }
    }
  }

  exec(R"(
    CREATE TABLE IF NOT EXISTS saved_configs (
      name       TEXT NOT NULL,
      version    TEXT NOT NULL,
      data       TEXT NOT NULL,
      PRIMARY KEY (name, version)
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS settings (
      key   TEXT PRIMARY KEY,
      value TEXT NOT NULL
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS plugin_capabilities (
      plugin_uid     INTEGER PRIMARY KEY,
      uses_listener  INTEGER NOT NULL DEFAULT 0
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS window_settings (
      window_id  TEXT PRIMARY KEY,
      x          INTEGER,
      y          INTEGER,
      width      INTEGER NOT NULL DEFAULT 800,
      height     INTEGER NOT NULL DEFAULT 600,
      visible    INTEGER NOT NULL DEFAULT 1,
      mode       TEXT NOT NULL DEFAULT 'mixer'
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS plugin_cache (
      path         TEXT PRIMARY KEY,
      mod_time     INTEGER NOT NULL,
      name         TEXT NOT NULL DEFAULT '',
      manufacturer TEXT NOT NULL DEFAULT '',
      category     TEXT NOT NULL DEFAULT '',
      format       TEXT NOT NULL DEFAULT 'VST3',
      uid          INTEGER NOT NULL DEFAULT 0,
      num_inputs   INTEGER NOT NULL DEFAULT 0,
      num_outputs  INTEGER NOT NULL DEFAULT 0,
      is_instrument INTEGER NOT NULL DEFAULT 0,
      valid        INTEGER NOT NULL DEFAULT 1
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS master_audio (
      singleton_id INTEGER PRIMARY KEY CHECK (singleton_id = 1),
      gain_db      REAL NOT NULL DEFAULT 0.0
    )
  )");
  exec("INSERT OR IGNORE INTO master_audio (singleton_id, gain_db) "
       "VALUES (1, 0.0)");

  exec(R"(
    CREATE TABLE IF NOT EXISTS master_inserts (
      slot_id       TEXT PRIMARY KEY,
      position      INTEGER NOT NULL,
      format_name   TEXT NOT NULL,
      plugin_uid    INTEGER NOT NULL,
      file_id       TEXT NOT NULL,
      manufacturer  TEXT NOT NULL DEFAULT '',
      plugin_name   TEXT NOT NULL DEFAULT '',
      category      TEXT NOT NULL DEFAULT '',
      plugin_version TEXT NOT NULL DEFAULT '',
      num_inputs    INTEGER NOT NULL DEFAULT 0,
      num_outputs   INTEGER NOT NULL DEFAULT 0,
      bypassed      INTEGER NOT NULL DEFAULT 0,
      plugin_state  BLOB
    )
  )");

  // Harmless cache-schema upgrade for databases created before audio effects.
  const auto pluginCacheUpgrade = sqlite3_exec(
      db_,
      "ALTER TABLE plugin_cache ADD COLUMN is_instrument INTEGER NOT NULL "
      "DEFAULT 0",
      nullptr, nullptr, nullptr);
  if (pluginCacheUpgrade == SQLITE_OK) {
    exec("UPDATE plugin_cache SET is_instrument = 1 "
         "WHERE category LIKE '%Instrument%' OR category LIKE '%Synth%'");
  }

  exec(R"(
    CREATE TABLE IF NOT EXISTS ensemble_slots (
      sort_order          INTEGER PRIMARY KEY,
      entity_id           TEXT NOT NULL,
      name                TEXT NOT NULL,
      family              TEXT NOT NULL DEFAULT '',
      music_xml_sound_id  TEXT NOT NULL DEFAULT '',
      solo_count          INTEGER NOT NULL DEFAULT 1,
      section_count       INTEGER NOT NULL DEFAULT 1
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS channel_assignments (
      flat_index    INTEGER PRIMARY KEY,
      entity_id     TEXT NOT NULL,
      is_solo       INTEGER NOT NULL,
      instance_num  INTEGER NOT NULL DEFAULT 1
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS channel_graveyard (
      flat_index    INTEGER PRIMARY KEY,
      entity_id     TEXT NOT NULL,
      is_solo       INTEGER NOT NULL,
      instance_num  INTEGER NOT NULL DEFAULT 1
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS strip_blobs (
      hash          TEXT PRIMARY KEY,
      library_id    TEXT NOT NULL DEFAULT '00000000-0000-0000-0000-000000000000',
      library       TEXT NOT NULL,
      family        TEXT NOT NULL,
      is_solo       INTEGER NOT NULL,
      input_port    INTEGER NOT NULL,
      input_channel INTEGER NOT NULL,
      plugin_uid    INTEGER NOT NULL,
      gain_db       REAL NOT NULL,
      expression_map TEXT NOT NULL,
      plugin_state  BLOB,
      active        INTEGER NOT NULL DEFAULT 1,
      muted         INTEGER NOT NULL DEFAULT 0,
      soloed        INTEGER NOT NULL DEFAULT 0,
      lua_plugins   TEXT NOT NULL DEFAULT ''
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS fiddle_states (
      hash TEXT PRIMARY KEY,
      master_gain REAL NOT NULL,
      strip_hashes TEXT NOT NULL,
      audio_schema INTEGER NOT NULL DEFAULT 1,
      master_state BLOB,
      routing_state BLOB
    )
  )");
  sqlite3_exec(db_,
               "ALTER TABLE fiddle_states ADD COLUMN audio_schema INTEGER "
               "NOT NULL DEFAULT 1",
               nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "ALTER TABLE fiddle_states ADD COLUMN master_state BLOB",
               nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "ALTER TABLE fiddle_states ADD COLUMN routing_state BLOB",
               nullptr, nullptr, nullptr);

  exec(R"(
    CREATE TABLE IF NOT EXISTS versions (
      id               TEXT PRIMARY KEY,
      state_hash       TEXT NOT NULL,
      branch_id        TEXT NOT NULL,
      parent_id        TEXT NOT NULL DEFAULT '',
      merge_parent_id  TEXT NOT NULL DEFAULT '',
      created_at       DATETIME DEFAULT CURRENT_TIMESTAMP
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS branches (
      id      TEXT PRIMARY KEY,
      name    TEXT UNIQUE NOT NULL,
      head_id TEXT NOT NULL
    )
  )");

  exec(R"(
    CREATE TABLE IF NOT EXISTS libraries (
      id      TEXT PRIMARY KEY,
      name    TEXT NOT NULL,
      vendor  TEXT NOT NULL DEFAULT '',
      variant TEXT NOT NULL DEFAULT ''
    )
  )");

  // Migrate: add vendor/variant columns if missing
  // (sqlite3_exec will silently fail if column already exists)
  sqlite3_exec(db_, "ALTER TABLE libraries ADD COLUMN vendor TEXT NOT NULL DEFAULT ''",
               nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "ALTER TABLE libraries ADD COLUMN variant TEXT NOT NULL DEFAULT ''",
               nullptr, nullptr, nullptr);

  exec(R"(
    CREATE TABLE IF NOT EXISTS library_instruments (
      library_id  TEXT NOT NULL,
      sort_order  INTEGER NOT NULL,
      entity_id   TEXT NOT NULL DEFAULT '',
      name        TEXT NOT NULL,
      category    TEXT NOT NULL DEFAULT '',
      is_solo     INTEGER NOT NULL DEFAULT 1,
      vst_plugin  TEXT NOT NULL DEFAULT '',
      expr_map    TEXT NOT NULL DEFAULT '',
      PRIMARY KEY (library_id, sort_order),
      FOREIGN KEY (library_id) REFERENCES libraries(id)
    )
  )");

  // v3: add plugin_uid and plugin_state to library_instruments
  sqlite3_exec(db_, "ALTER TABLE library_instruments ADD COLUMN plugin_uid INTEGER NOT NULL DEFAULT 0",
               nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "ALTER TABLE library_instruments ADD COLUMN plugin_state BLOB",
               nullptr, nullptr, nullptr);

  // v4: add family column to library_instruments
  sqlite3_exec(db_, "ALTER TABLE library_instruments ADD COLUMN family TEXT NOT NULL DEFAULT ''",
               nullptr, nullptr, nullptr);

  // v5: add instance_num column to library_instruments (legacy, migrated in v8)
  sqlite3_exec(db_, "ALTER TABLE library_instruments ADD COLUMN instance_num INTEGER NOT NULL DEFAULT 1",
               nullptr, nullptr, nullptr);

  // v6: add note column to library_instruments
  sqlite3_exec(db_, "ALTER TABLE library_instruments ADD COLUMN note TEXT NOT NULL DEFAULT ''",
               nullptr, nullptr, nullptr);

  // v8: replace instance_num INTEGER with instance_nums TEXT (comma-separated)
  sqlite3_exec(db_, "ALTER TABLE library_instruments ADD COLUMN instance_nums TEXT NOT NULL DEFAULT '1'",
               nullptr, nullptr, nullptr);
  // Migrate existing data: copy integer values to text column
  sqlite3_exec(db_, "UPDATE library_instruments SET instance_nums = CAST(instance_num AS TEXT) WHERE instance_num != 1",
               nullptr, nullptr, nullptr);

  // v7: add active column to strips (library activation state)
  sqlite3_exec(db_, "ALTER TABLE strips ADD COLUMN active INTEGER NOT NULL DEFAULT 1",
               nullptr, nullptr, nullptr);

  // v9: add lua_plugins column to strips (comma-separated filenames)
  sqlite3_exec(db_, "ALTER TABLE strips ADD COLUMN lua_plugins TEXT NOT NULL DEFAULT ''",
               nullptr, nullptr, nullptr);

  // v10: persist per-strip monitoring state. Existing sessions retain the
  // historical behaviour: neither muted nor soloed.
  sqlite3_exec(db_, "ALTER TABLE strips ADD COLUMN muted INTEGER NOT NULL DEFAULT 0",
               nullptr, nullptr, nullptr);
  sqlite3_exec(db_, "ALTER TABLE strips ADD COLUMN soloed INTEGER NOT NULL DEFAULT 0",
               nullptr, nullptr, nullptr);

  // Seed the default library on first boot
  exec("INSERT OR IGNORE INTO libraries (id, name) VALUES "
       "('00000000-0000-0000-0000-000000000000', '')");

  LibraryRoutingRepository::ensureSchema(db_);
}

void FiddleDatabase::prepareStatements() {
  auto prep = [this](const char *sql, SqliteStatement &stmt) {
    int rc = sqlite3_prepare_v2(db_, sql, -1, stmt.outPtr(), nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[FiddleDB] Prepare failed: " << sqlite3_errmsg(db_)
                << "\n  SQL: " << sql << std::endl;
    }
  };

  prep(R"(
    INSERT OR REPLACE INTO strips
      (id, position, library, family, is_solo, input_port, input_channel,
       plugin_uid, gain_db, expression_map, active, lua_plugins, muted, soloed)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )",
       stmtSaveStrip_);

  prep(R"(
    UPDATE strips SET plugin_state = ? WHERE id = ?
  )",
       stmtSaveBlob_);

  prep("DELETE FROM strips WHERE id = ?", stmtRemoveStrip_);

  prep("DELETE FROM strips", stmtClearStrips_);

  prep("SELECT id, position, library, family, is_solo, input_port, "
       "input_channel, plugin_uid, gain_db, expression_map, plugin_state, active, "
       "lua_plugins, muted, soloed "
       "FROM strips ORDER BY position",
       stmtLoadStrips_);

  prep("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
       stmtSaveSetting_);

  prep("SELECT value FROM settings WHERE key = ?", stmtLoadSetting_);

  prep(R"(
    INSERT INTO saved_configs (name, version, data)
    VALUES (?, datetime('now'), ?)
  )",
       stmtSaveConfig_);

  prep(R"(
    INSERT INTO saved_configs (name, version, data)
    VALUES (?, ?, ?)
  )",
       stmtSaveConfigVersion_);

  prep(R"(
    SELECT data FROM saved_configs
    WHERE name = ? ORDER BY version DESC LIMIT 1
  )",
       stmtLoadConfig_);

  prep(R"(
    SELECT data FROM saved_configs
    WHERE name = ? AND version = ?
  )",
       stmtLoadConfigVersion_);

  prep(R"(
    SELECT COUNT(*) FROM saved_configs
    WHERE name = ? AND version = ?
  )",
       stmtHasConfigVersion_);

  prep(R"(
    SELECT name, MAX(version) as version FROM saved_configs
    GROUP BY name ORDER BY version DESC
  )",
       stmtListConfigs_);

  prep(R"(
    SELECT name, version FROM saved_configs
    WHERE name = ? ORDER BY version DESC
  )",
       stmtListConfigVersions_);

  prep("DELETE FROM saved_configs WHERE name = ?", stmtDeleteConfig_);

  prep(R"(
    INSERT OR REPLACE INTO plugin_capabilities (plugin_uid, uses_listener)
    VALUES (?, 1)
  )",
       stmtSavePluginCapability_);

  prep("SELECT plugin_uid FROM plugin_capabilities WHERE uses_listener = 1",
       stmtLoadListenerUids_);

  prep(R"(
    INSERT OR REPLACE INTO window_settings
      (window_id, x, y, width, height, visible, mode)
    VALUES (?, ?, ?, ?, ?, ?, ?)
  )",
       stmtSaveWindowSettings_);

  prep("SELECT x, y, width, height, visible, mode FROM window_settings "
       "WHERE window_id = ?",
       stmtLoadWindowSettings_);

  prep(R"(
    INSERT OR REPLACE INTO plugin_cache
      (path, mod_time, name, manufacturer, category, format,
       uid, num_inputs, num_outputs, is_instrument, valid)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )",
       stmtSavePluginCache_);

  prep("SELECT path, mod_time, name, manufacturer, category, format, "
       "uid, num_inputs, num_outputs, is_instrument, valid FROM plugin_cache",
       stmtLoadPluginCache_);

  prep("DELETE FROM plugin_cache WHERE path = ?", stmtRemovePluginCache_);

  prep("DELETE FROM plugin_cache", stmtClearPluginCache_);

  // Ensemble
  prep("DELETE FROM ensemble_slots", stmtClearEnsemble_);
  prep("INSERT INTO ensemble_slots "
       "(sort_order, entity_id, name, family, music_xml_sound_id, "
       "solo_count, section_count) VALUES (?,?,?,?,?,?,?)",
       stmtInsertEnsembleSlot_);
  prep("SELECT entity_id, name, family, music_xml_sound_id, solo_count, "
       "section_count FROM ensemble_slots ORDER BY sort_order",
       stmtLoadEnsemble_);

  // Channel assignments
  prep("DELETE FROM channel_assignments", stmtClearAssignments_);
  prep("INSERT OR REPLACE INTO channel_assignments "
       "(flat_index, entity_id, is_solo, instance_num) VALUES (?,?,?,?)",
       stmtInsertAssignment_);
  prep("SELECT flat_index, entity_id, is_solo, instance_num "
       "FROM channel_assignments ORDER BY flat_index",
       stmtLoadAssignments_);

  // Channel graveyard
  prep("DELETE FROM channel_graveyard", stmtClearGraveyard_);
  prep("INSERT OR REPLACE INTO channel_graveyard "
       "(flat_index, entity_id, is_solo, instance_num) VALUES (?,?,?,?)",
       stmtInsertGraveyard_);
  prep("SELECT flat_index FROM channel_graveyard "
       "WHERE entity_id = ? AND is_solo = ? ORDER BY instance_num LIMIT 1",
       stmtFindGraveyard_);
  prep("DELETE FROM channel_graveyard WHERE flat_index = ?",
       stmtRemoveGraveyard_);
  prep("SELECT flat_index, entity_id, is_solo, instance_num "
       "FROM channel_graveyard ORDER BY flat_index",
       stmtLoadGraveyard_);

  // Library statements
  prep(R"(
    INSERT OR REPLACE INTO libraries (id, name, vendor, variant)
    VALUES (?, ?, ?, ?)
  )",
       stmtSaveLibrary_);
  prep("DELETE FROM libraries WHERE id = ?", stmtDeleteLibrary_);
  prep("DELETE FROM library_instruments WHERE library_id = ?",
       stmtDeleteLibraryInstruments_);
  prep(R"(
    INSERT INTO library_instruments
      (library_id, sort_order, entity_id, name, category, is_solo, vst_plugin, expr_map,
       plugin_uid, plugin_state, family, instance_nums, note)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )",
       stmtInsertLibraryInstrument_);
  prep("SELECT id, name, vendor, variant FROM libraries ORDER BY name",
       stmtListLibraries_);
  prep(R"(
    SELECT library_id, sort_order, entity_id, name, category, is_solo, vst_plugin, expr_map,
           plugin_uid, plugin_state, family, instance_nums, note
    FROM library_instruments WHERE library_id = ? ORDER BY sort_order
  )",
       stmtLoadLibraryInstruments_);
  prep(R"(
    SELECT li.library_id, li.sort_order, li.entity_id, li.name, li.category,
           li.is_solo, li.vst_plugin, li.expr_map, li.plugin_uid, li.plugin_state,
           li.family, li.instance_nums, li.note
    FROM library_instruments li
    INNER JOIN libraries l ON li.library_id = l.id
    ORDER BY l.name, li.sort_order
  )",
       stmtLoadAllLibraryInstruments_);
}

void FiddleDatabase::finalizeStatements() {
  auto fin = [](SqliteStatement &stmt) {
    stmt.finalize();
  };
  fin(stmtSaveStrip_);
  fin(stmtSaveBlob_);
  fin(stmtRemoveStrip_);
  fin(stmtClearStrips_);
  fin(stmtLoadStrips_);
  fin(stmtSaveSetting_);
  fin(stmtLoadSetting_);
  fin(stmtSaveConfig_);
  fin(stmtSaveConfigVersion_);
  fin(stmtLoadConfig_);
  fin(stmtLoadConfigVersion_);
  fin(stmtHasConfigVersion_);
  fin(stmtListConfigs_);
  fin(stmtListConfigVersions_);
  fin(stmtDeleteConfig_);
  fin(stmtSavePluginCapability_);
  fin(stmtLoadListenerUids_);
  fin(stmtSaveWindowSettings_);
  fin(stmtLoadWindowSettings_);
  fin(stmtSavePluginCache_);
  fin(stmtLoadPluginCache_);
  fin(stmtRemovePluginCache_);
  fin(stmtClearPluginCache_);
  fin(stmtClearEnsemble_);
  fin(stmtInsertEnsembleSlot_);
  fin(stmtLoadEnsemble_);
  fin(stmtClearAssignments_);
  fin(stmtInsertAssignment_);
  fin(stmtLoadAssignments_);
  fin(stmtClearGraveyard_);
  fin(stmtInsertGraveyard_);
  fin(stmtFindGraveyard_);
  fin(stmtRemoveGraveyard_);
  fin(stmtLoadGraveyard_);
  fin(stmtSaveLibrary_);
  fin(stmtDeleteLibrary_);
  fin(stmtDeleteLibraryInstruments_);
  fin(stmtInsertLibraryInstrument_);
  fin(stmtListLibraries_);
  fin(stmtLoadLibraryInstruments_);
  fin(stmtLoadAllLibraryInstruments_);
}

bool FiddleDatabase::exec(const char *sql) {
  char *err = nullptr;
  int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
  if (rc != SQLITE_OK) {
    std::cerr << "[FiddleDB] exec error: " << (err ? err : "unknown")
              << "\n  SQL: " << sql << std::endl;
    sqlite3_free(err);
    return false;
  }
  return true;
}

// ── Strip operations ─────────────────────────────────────────────────

void FiddleDatabase::saveStrip(const MixerStrip &strip, int position) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSaveStrip_)
    return;

  const auto realtime = strip.realtimeState();

  sqlite3_reset(stmtSaveStrip_);
  sqlite3_bind_text(stmtSaveStrip_, 1, strip.id.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveStrip_, 2, position);
  sqlite3_bind_text(stmtSaveStrip_, 3, strip.library.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveStrip_, 4, strip.family.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveStrip_, 5, strip.isSolo ? 1 : 0);
  sqlite3_bind_int(stmtSaveStrip_, 6, realtime.inputPort);
  sqlite3_bind_int(stmtSaveStrip_, 7, realtime.inputChannel);
  sqlite3_bind_int(stmtSaveStrip_, 8, strip.pluginUid);
  sqlite3_bind_double(stmtSaveStrip_, 9, realtime.gainDb);

  std::string xmapId = strip.expressionMap ? strip.expressionMap->entityID : "";
  sqlite3_bind_text(stmtSaveStrip_, 10, xmapId.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveStrip_, 11, realtime.active ? 1 : 0);

  // Serialize Lua plugin filenames as comma-separated list
  auto luaNames = strip.getLuaPluginFileNames();
  std::string luaPluginsStr;
  for (size_t i = 0; i < luaNames.size(); ++i) {
    if (i > 0) luaPluginsStr += ',';
    luaPluginsStr += luaNames[i];
  }
  sqlite3_bind_text(stmtSaveStrip_, 12, luaPluginsStr.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveStrip_, 13, realtime.muted ? 1 : 0);
  sqlite3_bind_int(stmtSaveStrip_, 14, realtime.soloed ? 1 : 0);

  if (sqlite3_step(stmtSaveStrip_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] saveStrip failed: " << sqlite3_errmsg(db_)
              << std::endl;
  }
}

void FiddleDatabase::savePluginBlob(const juce::String &stripId,
                                    const juce::MemoryBlock &state) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSaveBlob_)
    return;

  sqlite3_reset(stmtSaveBlob_);
  sqlite3_bind_blob(stmtSaveBlob_, 1, state.getData(), (int)state.getSize(),
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveBlob_, 2, stripId.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtSaveBlob_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] savePluginBlob failed: " << sqlite3_errmsg(db_)
              << std::endl;
  }
}

void FiddleDatabase::removeStrip(const juce::String &stripId) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtRemoveStrip_)
    return;

  sqlite3_reset(stmtRemoveStrip_);
  sqlite3_bind_text(stmtRemoveStrip_, 1, stripId.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtRemoveStrip_);
}

void FiddleDatabase::clearStrips() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtClearStrips_)
    return;
  sqlite3_reset(stmtClearStrips_);
  sqlite3_step(stmtClearStrips_);
}

std::vector<StripRow> FiddleDatabase::loadAllStrips() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<StripRow> rows;
  if (!stmtLoadStrips_)
    return rows;

  sqlite3_reset(stmtLoadStrips_);
  while (sqlite3_step(stmtLoadStrips_) == SQLITE_ROW) {
    StripRow row;
    row.id = (const char *)sqlite3_column_text(stmtLoadStrips_, 0);
    row.position = sqlite3_column_int(stmtLoadStrips_, 1);
    row.library = (const char *)sqlite3_column_text(stmtLoadStrips_, 2);
    row.family = (const char *)sqlite3_column_text(stmtLoadStrips_, 3);
    row.isSolo = sqlite3_column_int(stmtLoadStrips_, 4) != 0;
    row.inputPort = sqlite3_column_int(stmtLoadStrips_, 5);
    row.inputChannel = sqlite3_column_int(stmtLoadStrips_, 6);
    row.pluginUid = sqlite3_column_int(stmtLoadStrips_, 7);
    row.gainDb = (float)sqlite3_column_double(stmtLoadStrips_, 8);

    auto *xmap = (const char *)sqlite3_column_text(stmtLoadStrips_, 9);
    row.expressionMapEntityID = xmap ? xmap : "";

    // BLOB
    const void *blobData = sqlite3_column_blob(stmtLoadStrips_, 10);
    int blobSize = sqlite3_column_bytes(stmtLoadStrips_, 10);
    if (blobData && blobSize > 0) {
      row.pluginState.append(blobData, (size_t)blobSize);
    }

    // Active flag (column 11, default true for old rows)
    row.active = sqlite3_column_int(stmtLoadStrips_, 11) != 0;

    // Lua plugins (column 12, comma-separated filenames)
    auto *luaStr = (const char *)sqlite3_column_text(stmtLoadStrips_, 12);
    if (luaStr && std::string(luaStr).length() > 0) {
      juce::StringArray parts =
          juce::StringArray::fromTokens(juce::String(luaStr), ",", "");
      for (const auto &part : parts) {
        auto trimmed = part.trim();
        if (trimmed.isNotEmpty())
          row.luaPluginFileNames.push_back(trimmed.toStdString());
      }
    }

    row.muted = sqlite3_column_int(stmtLoadStrips_, 13) != 0;
    row.soloed = sqlite3_column_int(stmtLoadStrips_, 14) != 0;

    rows.push_back(std::move(row));
  }

  std::cerr << "[FiddleDB] Loaded " << rows.size() << " strips" << std::endl;
  return rows;
}

void FiddleDatabase::saveMasterAudio(const MasterAudioSnapshot &snapshot) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!db_)
    return;

  sqlite3_exec(db_, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);

  sqlite3_stmt *gainStatement = nullptr;
  if (sqlite3_prepare_v2(
          db_, "INSERT OR REPLACE INTO master_audio (singleton_id, gain_db) "
               "VALUES (1, ?)",
          -1, &gainStatement, nullptr) == SQLITE_OK) {
    sqlite3_bind_double(gainStatement, 1, snapshot.gainDb);
    sqlite3_step(gainStatement);
  }
  sqlite3_finalize(gainStatement);

  sqlite3_exec(db_, "DELETE FROM master_inserts", nullptr, nullptr, nullptr);
  sqlite3_stmt *insertStatement = nullptr;
  const char *insertSql =
      "INSERT INTO master_inserts "
      "(slot_id, position, format_name, plugin_uid, file_id, manufacturer, "
      "plugin_name, category, plugin_version, num_inputs, num_outputs, "
      "bypassed, plugin_state) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";
  if (sqlite3_prepare_v2(db_, insertSql, -1, &insertStatement, nullptr) ==
      SQLITE_OK) {
    for (int position = 0;
         position < static_cast<int>(snapshot.inserts.size()); ++position) {
      const auto &slot = snapshot.inserts[static_cast<std::size_t>(position)];
      const auto &description = slot.description;
      sqlite3_reset(insertStatement);
      sqlite3_clear_bindings(insertStatement);
      sqlite3_bind_text(insertStatement, 1, slot.slotId.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_int(insertStatement, 2, position);
      sqlite3_bind_text(insertStatement, 3,
                        description.pluginFormatName.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_int(insertStatement, 4, description.uniqueId);
      sqlite3_bind_text(insertStatement, 5,
                        description.fileOrIdentifier.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insertStatement, 6,
                        description.manufacturerName.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insertStatement, 7, description.name.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insertStatement, 8,
                        description.category.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(insertStatement, 9, description.version.toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_bind_int(insertStatement, 10, description.numInputChannels);
      sqlite3_bind_int(insertStatement, 11, description.numOutputChannels);
      sqlite3_bind_int(insertStatement, 12, slot.bypassed ? 1 : 0);
      if (!slot.pluginState.isEmpty()) {
        sqlite3_bind_blob(insertStatement, 13, slot.pluginState.getData(),
                          static_cast<int>(slot.pluginState.getSize()),
                          SQLITE_TRANSIENT);
      } else {
        sqlite3_bind_null(insertStatement, 13);
      }
      if (sqlite3_step(insertStatement) != SQLITE_DONE)
        std::cerr << "[FiddleDB] saveMasterAudio insert failed: "
                  << sqlite3_errmsg(db_) << std::endl;
    }
  }
  sqlite3_finalize(insertStatement);
  sqlite3_exec(db_, "COMMIT", nullptr, nullptr, nullptr);
}

MasterAudioSnapshot FiddleDatabase::loadMasterAudio() {
  std::lock_guard<std::mutex> lock(mutex_);
  MasterAudioSnapshot snapshot;
  if (!db_)
    return snapshot;

  sqlite3_stmt *gainStatement = nullptr;
  if (sqlite3_prepare_v2(db_,
                         "SELECT gain_db FROM master_audio WHERE singleton_id=1",
                         -1, &gainStatement, nullptr) == SQLITE_OK &&
      sqlite3_step(gainStatement) == SQLITE_ROW) {
    snapshot.gainDb = static_cast<float>(sqlite3_column_double(gainStatement, 0));
  }
  sqlite3_finalize(gainStatement);

  sqlite3_stmt *insertStatement = nullptr;
  const char *selectSql =
      "SELECT slot_id, format_name, plugin_uid, file_id, manufacturer, "
      "plugin_name, category, plugin_version, num_inputs, num_outputs, "
      "bypassed, plugin_state FROM master_inserts ORDER BY position";
  if (sqlite3_prepare_v2(db_, selectSql, -1, &insertStatement, nullptr) ==
      SQLITE_OK) {
    while (sqlite3_step(insertStatement) == SQLITE_ROW) {
      const auto text = [&](int column) {
        const auto *value = sqlite3_column_text(insertStatement, column);
        return juce::String(value ? reinterpret_cast<const char *>(value) : "");
      };
      MasterInsertSnapshot slot;
      slot.slotId = text(0);
      slot.description.pluginFormatName = text(1);
      slot.description.uniqueId = sqlite3_column_int(insertStatement, 2);
      slot.description.fileOrIdentifier = text(3);
      slot.description.manufacturerName = text(4);
      slot.description.name = text(5);
      slot.description.category = text(6);
      slot.description.version = text(7);
      slot.description.numInputChannels = sqlite3_column_int(insertStatement, 8);
      slot.description.numOutputChannels = sqlite3_column_int(insertStatement, 9);
      slot.description.isInstrument = false;
      slot.bypassed = sqlite3_column_int(insertStatement, 10) != 0;
      const void *state = sqlite3_column_blob(insertStatement, 11);
      const int stateSize = sqlite3_column_bytes(insertStatement, 11);
      if (state && stateSize > 0)
        slot.pluginState.append(state, static_cast<std::size_t>(stateSize));
      snapshot.inserts.push_back(std::move(slot));
    }
  }
  sqlite3_finalize(insertStatement);
  return snapshot;
}

// ── Settings ─────────────────────────────────────────────────────────

void FiddleDatabase::saveSetting(const std::string &key,
                                 const std::string &value) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSaveSetting_)
    return;

  sqlite3_reset(stmtSaveSetting_);
  sqlite3_bind_text(stmtSaveSetting_, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveSetting_, 2, value.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmtSaveSetting_);
}

std::string FiddleDatabase::loadSetting(const std::string &key,
                                        const std::string &defaultValue) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtLoadSetting_)
    return defaultValue;

  sqlite3_reset(stmtLoadSetting_);
  sqlite3_bind_text(stmtLoadSetting_, 1, key.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtLoadSetting_) == SQLITE_ROW) {
    auto *val = (const char *)sqlite3_column_text(stmtLoadSetting_, 0);
    return val ? val : defaultValue;
  }
  return defaultValue;
}

// ── Saved configurations ─────────────────────────────────────────────

juce::String FiddleDatabase::saveConfig(const juce::String &name) {
  // Snapshot current strips as JSON
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtLoadStrips_ || !stmtSaveConfigVersion_)
    return {};

  // Generate version timestamp in C++
  juce::String version = juce::Time::getCurrentTime().toISO8601(true);

  // Read all strips into JSON
  juce::Array<juce::var> arr;
  sqlite3_reset(stmtLoadStrips_);
  while (sqlite3_step(stmtLoadStrips_) == SQLITE_ROW) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String((const char *)sqlite3_column_text(
                               stmtLoadStrips_, 0)));
    obj->setProperty("position", sqlite3_column_int(stmtLoadStrips_, 1));
    obj->setProperty("library", juce::String((const char *)sqlite3_column_text(
                                    stmtLoadStrips_, 2)));
    obj->setProperty("family", juce::String((const char *)sqlite3_column_text(
                                   stmtLoadStrips_, 3)));
    obj->setProperty("isSolo", sqlite3_column_int(stmtLoadStrips_, 4) != 0);
    obj->setProperty("inputPort", sqlite3_column_int(stmtLoadStrips_, 5));
    obj->setProperty("inputChannel", sqlite3_column_int(stmtLoadStrips_, 6));
    obj->setProperty("pluginUid", sqlite3_column_int(stmtLoadStrips_, 7));
    obj->setProperty("gainDb", sqlite3_column_double(stmtLoadStrips_, 8));

    auto *xmap = (const char *)sqlite3_column_text(stmtLoadStrips_, 9);
    obj->setProperty("expressionMap", juce::String(xmap ? xmap : ""));

    // Encode BLOB as base64
    const void *blobData = sqlite3_column_blob(stmtLoadStrips_, 10);
    int blobSize = sqlite3_column_bytes(stmtLoadStrips_, 10);
    if (blobData && blobSize > 0) {
      juce::MemoryBlock block(blobData, (size_t)blobSize);
      obj->setProperty("pluginState", block.toBase64Encoding());
    }

    arr.add(juce::var(obj));
  }

  juce::String json = juce::JSON::toString(juce::var(arr), true);

  // Use saveConfigVersion_ stmt (name, version, data)
  sqlite3_reset(stmtSaveConfigVersion_);
  sqlite3_bind_text(stmtSaveConfigVersion_, 1, name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveConfigVersion_, 2, version.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveConfigVersion_, 3, json.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtSaveConfigVersion_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] saveConfig failed: " << sqlite3_errmsg(db_)
              << std::endl;
    return {};
  }

  std::cerr << "[FiddleDB] Saved config '" << name << "' version=" << version
            << std::endl;
  return version;
}

bool FiddleDatabase::loadConfig(const juce::String &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtLoadConfig_ || !stmtClearStrips_ || !stmtSaveStrip_ ||
      !stmtSaveBlob_)
    return false;

  // Load JSON from saved_configs
  sqlite3_reset(stmtLoadConfig_);
  sqlite3_bind_text(stmtLoadConfig_, 1, name.toRawUTF8(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtLoadConfig_) != SQLITE_ROW) {
    std::cerr << "[FiddleDB] Config not found: " << name << std::endl;
    return false;
  }

  auto *jsonStr = (const char *)sqlite3_column_text(stmtLoadConfig_, 0);
  if (!jsonStr)
    return false;

  juce::var parsed = juce::JSON::parse(juce::String(jsonStr));
  auto *arr = parsed.getArray();
  if (!arr)
    return false;

  // Clear current strips
  sqlite3_reset(stmtClearStrips_);
  sqlite3_step(stmtClearStrips_);

  // Restore strips
  for (auto &v : *arr) {
    auto *obj = v.getDynamicObject();
    if (!obj)
      continue;

    sqlite3_reset(stmtSaveStrip_);
    sqlite3_bind_text(stmtSaveStrip_, 1,
                      obj->getProperty("id").toString().toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtSaveStrip_, 2, (int)obj->getProperty("position"));
    sqlite3_bind_text(stmtSaveStrip_, 3,
                      obj->getProperty("library").toString().toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtSaveStrip_, 4,
                      obj->getProperty("family").toString().toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtSaveStrip_, 5,
                     (bool)obj->getProperty("isSolo") ? 1 : 0);
    sqlite3_bind_int(stmtSaveStrip_, 6, (int)obj->getProperty("inputPort"));
    sqlite3_bind_int(stmtSaveStrip_, 7, (int)obj->getProperty("inputChannel"));
    sqlite3_bind_int(stmtSaveStrip_, 8, (int)obj->getProperty("pluginUid"));
    sqlite3_bind_double(stmtSaveStrip_, 9, (double)obj->getProperty("gainDb"));
    sqlite3_bind_text(stmtSaveStrip_, 10,
                      obj->getProperty("expressionMap").toString().toRawUTF8(),
                      -1, SQLITE_TRANSIENT);
    sqlite3_step(stmtSaveStrip_);

    // Restore BLOB if present
    juce::String stateB64 = obj->getProperty("pluginState").toString();
    if (stateB64.isNotEmpty()) {
      juce::MemoryBlock block;
      block.fromBase64Encoding(stateB64);
      sqlite3_reset(stmtSaveBlob_);
      sqlite3_bind_blob(stmtSaveBlob_, 1, block.getData(), (int)block.getSize(),
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(stmtSaveBlob_, 2,
                        obj->getProperty("id").toString().toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_step(stmtSaveBlob_);
    }
  }

  std::cerr << "[FiddleDB] Loaded config '" << name << "' (" << arr->size()
            << " strips)" << std::endl;
  return true;
}

void FiddleDatabase::saveConfigVersion(const juce::String &name,
                                       const juce::String &version,
                                       const juce::String &jsonData) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSaveConfigVersion_)
    return;

  sqlite3_reset(stmtSaveConfigVersion_);
  sqlite3_bind_text(stmtSaveConfigVersion_, 1, name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveConfigVersion_, 2, version.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveConfigVersion_, 3, jsonData.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtSaveConfigVersion_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] saveConfigVersion failed: " << sqlite3_errmsg(db_)
              << std::endl;
  } else {
    std::cerr << "[FiddleDB] Saved config '" << name << "' version=" << version
              << std::endl;
  }
}

bool FiddleDatabase::loadConfigVersion(const juce::String &name,
                                       const juce::String &version) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtLoadConfigVersion_ || !stmtClearStrips_ || !stmtSaveStrip_ ||
      !stmtSaveBlob_)
    return false;

  sqlite3_reset(stmtLoadConfigVersion_);
  sqlite3_bind_text(stmtLoadConfigVersion_, 1, name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtLoadConfigVersion_, 2, version.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtLoadConfigVersion_) != SQLITE_ROW) {
    std::cerr << "[FiddleDB] Config version not found: " << name
              << " v=" << version << std::endl;
    return false;
  }

  auto *jsonStr = (const char *)sqlite3_column_text(stmtLoadConfigVersion_, 0);
  if (!jsonStr)
    return false;

  juce::var parsed = juce::JSON::parse(juce::String(jsonStr));
  auto *arr = parsed.getArray();
  if (!arr)
    return false;

  // Clear current strips
  sqlite3_reset(stmtClearStrips_);
  sqlite3_step(stmtClearStrips_);

  // Restore strips (same logic as loadConfig)
  for (auto &v : *arr) {
    auto *obj = v.getDynamicObject();
    if (!obj)
      continue;

    sqlite3_reset(stmtSaveStrip_);
    sqlite3_bind_text(stmtSaveStrip_, 1,
                      obj->getProperty("id").toString().toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtSaveStrip_, 2, (int)obj->getProperty("position"));
    sqlite3_bind_text(stmtSaveStrip_, 3,
                      obj->getProperty("library").toString().toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtSaveStrip_, 4,
                      obj->getProperty("family").toString().toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtSaveStrip_, 5,
                     (bool)obj->getProperty("isSolo") ? 1 : 0);
    sqlite3_bind_int(stmtSaveStrip_, 6, (int)obj->getProperty("inputPort"));
    sqlite3_bind_int(stmtSaveStrip_, 7, (int)obj->getProperty("inputChannel"));
    sqlite3_bind_int(stmtSaveStrip_, 8, (int)obj->getProperty("pluginUid"));
    sqlite3_bind_double(stmtSaveStrip_, 9, (double)obj->getProperty("gainDb"));
    sqlite3_bind_text(stmtSaveStrip_, 10,
                      obj->getProperty("expressionMap").toString().toRawUTF8(),
                      -1, SQLITE_TRANSIENT);
    sqlite3_step(stmtSaveStrip_);

    // Restore BLOB if present
    juce::String stateB64 = obj->getProperty("pluginState").toString();
    if (stateB64.isNotEmpty()) {
      juce::MemoryBlock block;
      block.fromBase64Encoding(stateB64);
      sqlite3_reset(stmtSaveBlob_);
      sqlite3_bind_blob(stmtSaveBlob_, 1, block.getData(), (int)block.getSize(),
                        SQLITE_TRANSIENT);
      sqlite3_bind_text(stmtSaveBlob_, 2,
                        obj->getProperty("id").toString().toRawUTF8(), -1,
                        SQLITE_TRANSIENT);
      sqlite3_step(stmtSaveBlob_);
    }
  }

  std::cerr << "[FiddleDB] Loaded config '" << name << "' version=" << version
            << " (" << arr->size() << " strips)" << std::endl;
  return true;
}

bool FiddleDatabase::hasConfigVersion(const juce::String &name,
                                      const juce::String &version) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtHasConfigVersion_)
    return false;

  sqlite3_reset(stmtHasConfigVersion_);
  sqlite3_bind_text(stmtHasConfigVersion_, 1, name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtHasConfigVersion_, 2, version.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtHasConfigVersion_) == SQLITE_ROW) {
    return sqlite3_column_int(stmtHasConfigVersion_, 0) > 0;
  }
  return false;
}

std::vector<SavedConfigInfo> FiddleDatabase::listConfigs() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SavedConfigInfo> result;
  if (!stmtListConfigs_)
    return result;

  sqlite3_reset(stmtListConfigs_);
  while (sqlite3_step(stmtListConfigs_) == SQLITE_ROW) {
    SavedConfigInfo info;
    info.name = (const char *)sqlite3_column_text(stmtListConfigs_, 0);
    info.version = (const char *)sqlite3_column_text(stmtListConfigs_, 1);
    result.push_back(std::move(info));
  }
  return result;
}

std::vector<SavedConfigInfo>
FiddleDatabase::listConfigVersions(const juce::String &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<SavedConfigInfo> result;
  if (!stmtListConfigVersions_)
    return result;

  sqlite3_reset(stmtListConfigVersions_);
  sqlite3_bind_text(stmtListConfigVersions_, 1, name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  while (sqlite3_step(stmtListConfigVersions_) == SQLITE_ROW) {
    SavedConfigInfo info;
    info.name = (const char *)sqlite3_column_text(stmtListConfigVersions_, 0);
    info.version =
        (const char *)sqlite3_column_text(stmtListConfigVersions_, 1);
    result.push_back(std::move(info));
  }
  return result;
}

void FiddleDatabase::deleteConfig(const juce::String &name) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtDeleteConfig_)
    return;

  sqlite3_reset(stmtDeleteConfig_);
  sqlite3_bind_text(stmtDeleteConfig_, 1, name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtDeleteConfig_);
}

// ── Plugin capabilities ──────────────────────────────────────────────

void FiddleDatabase::savePluginCapability(int pluginUid) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSavePluginCapability_ || pluginUid == 0)
    return;

  sqlite3_reset(stmtSavePluginCapability_);
  sqlite3_bind_int(stmtSavePluginCapability_, 1, pluginUid);
  sqlite3_step(stmtSavePluginCapability_);
}

std::set<int> FiddleDatabase::loadListenerCapableUids() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::set<int> uids;
  if (!stmtLoadListenerUids_)
    return uids;

  sqlite3_reset(stmtLoadListenerUids_);
  while (sqlite3_step(stmtLoadListenerUids_) == SQLITE_ROW) {
    uids.insert(sqlite3_column_int(stmtLoadListenerUids_, 0));
  }
  return uids;
}

// ── Window settings ──────────────────────────────────────────────────

void FiddleDatabase::saveWindowSettings(const WindowSettings &ws) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSaveWindowSettings_)
    return;

  sqlite3_reset(stmtSaveWindowSettings_);
  sqlite3_bind_text(stmtSaveWindowSettings_, 1, ws.windowId.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveWindowSettings_, 2, ws.x);
  sqlite3_bind_int(stmtSaveWindowSettings_, 3, ws.y);
  sqlite3_bind_int(stmtSaveWindowSettings_, 4, ws.width);
  sqlite3_bind_int(stmtSaveWindowSettings_, 5, ws.height);
  sqlite3_bind_int(stmtSaveWindowSettings_, 6, ws.visible ? 1 : 0);
  sqlite3_bind_text(stmtSaveWindowSettings_, 7, ws.mode.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtSaveWindowSettings_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] saveWindowSettings failed: " << sqlite3_errmsg(db_)
              << std::endl;
  }
}

WindowSettings
FiddleDatabase::loadWindowSettings(const juce::String &windowId) {
  std::lock_guard<std::mutex> lock(mutex_);
  WindowSettings ws;
  ws.windowId = windowId;

  if (!stmtLoadWindowSettings_)
    return ws;

  sqlite3_reset(stmtLoadWindowSettings_);
  sqlite3_bind_text(stmtLoadWindowSettings_, 1, windowId.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtLoadWindowSettings_) == SQLITE_ROW) {
    ws.found = true;
    ws.x = sqlite3_column_int(stmtLoadWindowSettings_, 0);
    ws.y = sqlite3_column_int(stmtLoadWindowSettings_, 1);
    ws.width = sqlite3_column_int(stmtLoadWindowSettings_, 2);
    ws.height = sqlite3_column_int(stmtLoadWindowSettings_, 3);
    ws.visible = sqlite3_column_int(stmtLoadWindowSettings_, 4) != 0;
    auto *mode = (const char *)sqlite3_column_text(stmtLoadWindowSettings_, 5);
    ws.mode = mode ? mode : "mixer";
  }

  return ws;
}

// ── Plugin cache ─────────────────────────────────────────────────────

void FiddleDatabase::savePluginCacheEntry(const PluginCacheRow &row) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSavePluginCache_)
    return;

  sqlite3_reset(stmtSavePluginCache_);
  sqlite3_bind_text(stmtSavePluginCache_, 1, row.path.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmtSavePluginCache_, 2, row.modTime);
  sqlite3_bind_text(stmtSavePluginCache_, 3, row.name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSavePluginCache_, 4, row.manufacturer.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSavePluginCache_, 5, row.category.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSavePluginCache_, 6, row.format.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSavePluginCache_, 7, row.uid);
  sqlite3_bind_int(stmtSavePluginCache_, 8, row.numInputs);
  sqlite3_bind_int(stmtSavePluginCache_, 9, row.numOutputs);
  sqlite3_bind_int(stmtSavePluginCache_, 10, row.isInstrument ? 1 : 0);
  sqlite3_bind_int(stmtSavePluginCache_, 11, row.valid ? 1 : 0);

  if (sqlite3_step(stmtSavePluginCache_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] savePluginCacheEntry failed: "
              << sqlite3_errmsg(db_) << std::endl;
  }
}

std::vector<PluginCacheRow> FiddleDatabase::loadPluginCache() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PluginCacheRow> rows;
  if (!stmtLoadPluginCache_)
    return rows;

  sqlite3_reset(stmtLoadPluginCache_);
  while (sqlite3_step(stmtLoadPluginCache_) == SQLITE_ROW) {
    PluginCacheRow row;
    row.path = (const char *)sqlite3_column_text(stmtLoadPluginCache_, 0);
    row.modTime = sqlite3_column_int64(stmtLoadPluginCache_, 1);
    row.name = (const char *)sqlite3_column_text(stmtLoadPluginCache_, 2);
    row.manufacturer =
        (const char *)sqlite3_column_text(stmtLoadPluginCache_, 3);
    row.category = (const char *)sqlite3_column_text(stmtLoadPluginCache_, 4);
    row.format = (const char *)sqlite3_column_text(stmtLoadPluginCache_, 5);
    row.uid = sqlite3_column_int(stmtLoadPluginCache_, 6);
    row.numInputs = sqlite3_column_int(stmtLoadPluginCache_, 7);
    row.numOutputs = sqlite3_column_int(stmtLoadPluginCache_, 8);
    row.isInstrument = sqlite3_column_int(stmtLoadPluginCache_, 9) != 0;
    row.valid = sqlite3_column_int(stmtLoadPluginCache_, 10) != 0;
    rows.push_back(std::move(row));
  }

  std::cerr << "[FiddleDB] Loaded " << rows.size() << " plugin cache entries"
            << std::endl;
  return rows;
}

void FiddleDatabase::removePluginCacheEntry(const juce::String &path) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtRemovePluginCache_)
    return;

  sqlite3_reset(stmtRemovePluginCache_);
  sqlite3_bind_text(stmtRemovePluginCache_, 1, path.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtRemovePluginCache_);
}

void FiddleDatabase::clearPluginCache() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtClearPluginCache_)
    return;

  sqlite3_reset(stmtClearPluginCache_);
  sqlite3_step(stmtClearPluginCache_);
  std::cerr << "[FiddleDB] Cleared plugin cache" << std::endl;
}

// ── Ensemble slots ─────────────────────────────────────────────────────

void FiddleDatabase::saveEnsemble(const juce::String &slotsJson) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtClearEnsemble_ || !stmtInsertEnsembleSlot_)
    return;

  exec("BEGIN TRANSACTION");

  sqlite3_reset(stmtClearEnsemble_);
  sqlite3_step(stmtClearEnsemble_);

  auto parsed = juce::JSON::parse(slotsJson);
  if (auto *arr = parsed.getArray()) {
    int sortOrder = 0;
    for (const auto &item : *arr) {
      if (auto *obj = item.getDynamicObject()) {
        sqlite3_reset(stmtInsertEnsembleSlot_);
        sqlite3_bind_int(stmtInsertEnsembleSlot_, 1, sortOrder++);
        sqlite3_bind_text(stmtInsertEnsembleSlot_, 2,
                          obj->getProperty("entityID").toString().toRawUTF8(),
                          -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmtInsertEnsembleSlot_, 3,
                          obj->getProperty("name").toString().toRawUTF8(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(stmtInsertEnsembleSlot_, 4,
                          obj->getProperty("family").toString().toRawUTF8(), -1,
                          SQLITE_TRANSIENT);
        sqlite3_bind_text(
            stmtInsertEnsembleSlot_, 5,
            obj->getProperty("musicXMLSoundID").toString().toRawUTF8(), -1,
            SQLITE_TRANSIENT);
        int solo = obj->getProperty("soloCount").isVoid()
                       ? 1
                       : (int)obj->getProperty("soloCount");
        int section = obj->getProperty("sectionCount").isVoid()
                          ? 1
                          : (int)obj->getProperty("sectionCount");
        sqlite3_bind_int(stmtInsertEnsembleSlot_, 6, solo);
        sqlite3_bind_int(stmtInsertEnsembleSlot_, 7, section);
        sqlite3_step(stmtInsertEnsembleSlot_);
      }
    }
  }

  exec("COMMIT");
  std::cerr << "[FiddleDB] Saved ensemble slots" << std::endl;
}

juce::String FiddleDatabase::loadEnsemble() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtLoadEnsemble_)
    return {};

  sqlite3_reset(stmtLoadEnsemble_);

  juce::Array<juce::var> arr;
  while (sqlite3_step(stmtLoadEnsemble_) == SQLITE_ROW) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("entityID", juce::String((const char *)sqlite3_column_text(
                                     stmtLoadEnsemble_, 0)));
    obj->setProperty("name", juce::String((const char *)sqlite3_column_text(
                                 stmtLoadEnsemble_, 1)));
    obj->setProperty("family", juce::String((const char *)sqlite3_column_text(
                                   stmtLoadEnsemble_, 2)));
    obj->setProperty(
        "musicXMLSoundID",
        juce::String((const char *)sqlite3_column_text(stmtLoadEnsemble_, 3)));
    obj->setProperty("soloCount", sqlite3_column_int(stmtLoadEnsemble_, 4));
    obj->setProperty("sectionCount", sqlite3_column_int(stmtLoadEnsemble_, 5));
    arr.add(juce::var(obj));
  }

  if (arr.isEmpty())
    return {};

  return juce::JSON::toString(juce::var(arr), true);
}

// ── Channel assignments ─────────────────────────────────────────────

void FiddleDatabase::saveChannelAssignments(
    const std::vector<ChannelAssignmentRow> &rows) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtClearAssignments_ || !stmtInsertAssignment_)
    return;

  exec("BEGIN TRANSACTION");

  sqlite3_reset(stmtClearAssignments_);
  sqlite3_step(stmtClearAssignments_);

  for (const auto &r : rows) {
    sqlite3_reset(stmtInsertAssignment_);
    sqlite3_bind_int(stmtInsertAssignment_, 1, r.flatIndex);
    sqlite3_bind_text(stmtInsertAssignment_, 2, r.entityID.toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtInsertAssignment_, 3, r.isSolo ? 1 : 0);
    sqlite3_bind_int(stmtInsertAssignment_, 4, r.instanceNum);
    sqlite3_step(stmtInsertAssignment_);
  }

  exec("COMMIT");
  std::cerr << "[FiddleDB] Saved " << rows.size() << " channel assignments"
            << std::endl;
}

std::vector<ChannelAssignmentRow> FiddleDatabase::loadChannelAssignments() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelAssignmentRow> result;
  if (!stmtLoadAssignments_)
    return result;

  sqlite3_reset(stmtLoadAssignments_);
  while (sqlite3_step(stmtLoadAssignments_) == SQLITE_ROW) {
    ChannelAssignmentRow r;
    r.flatIndex = sqlite3_column_int(stmtLoadAssignments_, 0);
    r.entityID = juce::String(
        (const char *)sqlite3_column_text(stmtLoadAssignments_, 1));
    r.isSolo = sqlite3_column_int(stmtLoadAssignments_, 2) != 0;
    r.instanceNum = sqlite3_column_int(stmtLoadAssignments_, 3);
    result.push_back(r);
  }
  return result;
}

void FiddleDatabase::moveToGraveyard(const ChannelAssignmentRow &row) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtInsertGraveyard_)
    return;

  sqlite3_reset(stmtInsertGraveyard_);
  sqlite3_bind_int(stmtInsertGraveyard_, 1, row.flatIndex);
  sqlite3_bind_text(stmtInsertGraveyard_, 2, row.entityID.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtInsertGraveyard_, 3, row.isSolo ? 1 : 0);
  sqlite3_bind_int(stmtInsertGraveyard_, 4, row.instanceNum);
  sqlite3_step(stmtInsertGraveyard_);
}

int FiddleDatabase::reclaimFromGraveyard(const juce::String &entityID,
                                         bool isSolo) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtFindGraveyard_ || !stmtRemoveGraveyard_)
    return -1;

  sqlite3_reset(stmtFindGraveyard_);
  sqlite3_bind_text(stmtFindGraveyard_, 1, entityID.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtFindGraveyard_, 2, isSolo ? 1 : 0);

  int result = -1;
  if (sqlite3_step(stmtFindGraveyard_) == SQLITE_ROW) {
    result = sqlite3_column_int(stmtFindGraveyard_, 0);

    // Remove from graveyard
    sqlite3_reset(stmtRemoveGraveyard_);
    sqlite3_bind_int(stmtRemoveGraveyard_, 1, result);
    sqlite3_step(stmtRemoveGraveyard_);
  }
  return result;
}

std::vector<ChannelAssignmentRow> FiddleDatabase::loadGraveyard() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<ChannelAssignmentRow> result;
  if (!stmtLoadGraveyard_)
    return result;

  sqlite3_reset(stmtLoadGraveyard_);
  while (sqlite3_step(stmtLoadGraveyard_) == SQLITE_ROW) {
    ChannelAssignmentRow r;
    r.flatIndex = sqlite3_column_int(stmtLoadGraveyard_, 0);
    r.entityID =
        juce::String((const char *)sqlite3_column_text(stmtLoadGraveyard_, 1));
    r.isSolo = sqlite3_column_int(stmtLoadGraveyard_, 2) != 0;
    r.instanceNum = sqlite3_column_int(stmtLoadGraveyard_, 3);
    result.push_back(r);
  }
  return result;
}

void FiddleDatabase::clearGraveyard() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtClearGraveyard_)
    return;

  sqlite3_reset(stmtClearGraveyard_);
  sqlite3_step(stmtClearGraveyard_);
}

// ── Libraries ────────────────────────────────────────────────────────

namespace {
/// Safe helper: returns "" if the column is NULL.
inline juce::String colText(sqlite3_stmt *stmt, int col) {
  auto *p = (const char *)sqlite3_column_text(stmt, col);
  return p ? juce::String(p) : juce::String();
}
} // namespace

void FiddleDatabase::saveLibrary(
    const LibraryRow &lib,
    const std::vector<LibraryInstrumentRow> &instruments) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtSaveLibrary_ || !stmtDeleteLibraryInstruments_ ||
      !stmtInsertLibraryInstrument_)
    return;

  exec("BEGIN TRANSACTION");

  // Upsert library header
  sqlite3_reset(stmtSaveLibrary_);
  sqlite3_bind_text(stmtSaveLibrary_, 1, lib.id.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveLibrary_, 2, lib.name.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveLibrary_, 3, lib.vendor.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveLibrary_, 4, lib.variant.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtSaveLibrary_);

  // Replace instruments: delete old, insert new
  sqlite3_reset(stmtDeleteLibraryInstruments_);
  sqlite3_bind_text(stmtDeleteLibraryInstruments_, 1, lib.id.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtDeleteLibraryInstruments_);

  for (const auto &inst : instruments) {
    sqlite3_reset(stmtInsertLibraryInstrument_);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 1, lib.id.toRawUTF8(), -1,
                      SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtInsertLibraryInstrument_, 2, inst.sortOrder);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 3,
                      inst.entityId.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 4, inst.name.toRawUTF8(),
                      -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 5,
                      inst.category.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtInsertLibraryInstrument_, 6, inst.isSolo ? 1 : 0);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 7,
                      inst.vstPlugin.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 8,
                      inst.exprMap.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmtInsertLibraryInstrument_, 9, inst.pluginUid);
    if (inst.pluginState.getSize() > 0)
      sqlite3_bind_blob(stmtInsertLibraryInstrument_, 10,
                        inst.pluginState.getData(),
                        (int)inst.pluginState.getSize(), SQLITE_TRANSIENT);
    else
      sqlite3_bind_null(stmtInsertLibraryInstrument_, 10);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 11,
                      inst.family.toRawUTF8(), -1, SQLITE_TRANSIENT);
    auto insNumsStr = inst.instanceNumsToString();
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 12,
                      insNumsStr.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmtInsertLibraryInstrument_, 13,
                      inst.note.toRawUTF8(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmtInsertLibraryInstrument_);
  }

  exec("COMMIT");
  std::cerr << "[FiddleDB] Saved library '" << lib.name << "' with "
            << instruments.size() << " instruments" << std::endl;
}

std::vector<LibraryRow> FiddleDatabase::listLibraries() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LibraryRow> result;
  if (!stmtListLibraries_)
    return result;

  sqlite3_reset(stmtListLibraries_);
  while (sqlite3_step(stmtListLibraries_) == SQLITE_ROW) {
    LibraryRow row;
    row.id = colText(stmtListLibraries_, 0);
    row.name = colText(stmtListLibraries_, 1);
    row.vendor = colText(stmtListLibraries_, 2);
    row.variant = colText(stmtListLibraries_, 3);
    result.push_back(std::move(row));
  }
  return result;
}

std::vector<LibraryInstrumentRow>
FiddleDatabase::loadLibraryInstruments(const juce::String &libraryId) {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LibraryInstrumentRow> result;
  if (!stmtLoadLibraryInstruments_)
    return result;

  sqlite3_reset(stmtLoadLibraryInstruments_);
  sqlite3_bind_text(stmtLoadLibraryInstruments_, 1, libraryId.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  while (sqlite3_step(stmtLoadLibraryInstruments_) == SQLITE_ROW) {
    LibraryInstrumentRow row;
    row.libraryId = colText(stmtLoadLibraryInstruments_, 0);
    row.sortOrder = sqlite3_column_int(stmtLoadLibraryInstruments_, 1);
    row.entityId = colText(stmtLoadLibraryInstruments_, 2);
    row.name = colText(stmtLoadLibraryInstruments_, 3);
    row.category = colText(stmtLoadLibraryInstruments_, 4);
    row.isSolo = sqlite3_column_int(stmtLoadLibraryInstruments_, 5) != 0;
    row.vstPlugin = colText(stmtLoadLibraryInstruments_, 6);
    row.exprMap = colText(stmtLoadLibraryInstruments_, 7);
    row.pluginUid = sqlite3_column_int(stmtLoadLibraryInstruments_, 8);
    if (auto *blobData = sqlite3_column_blob(stmtLoadLibraryInstruments_, 9)) {
      int blobSize = sqlite3_column_bytes(stmtLoadLibraryInstruments_, 9);
      row.pluginState.append(blobData, (size_t)blobSize);
    }
    row.family = colText(stmtLoadLibraryInstruments_, 10);
    row.instanceNums = LibraryInstrumentRow::parseInstanceNums(
        colText(stmtLoadLibraryInstruments_, 11));
    row.note = colText(stmtLoadLibraryInstruments_, 12);
    result.push_back(std::move(row));
  }
  return result;
}

void FiddleDatabase::deleteLibrary(const juce::String &libraryId) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtDeleteLibraryInstruments_ || !stmtDeleteLibrary_)
    return;

  exec("BEGIN TRANSACTION");

  sqlite3_reset(stmtDeleteLibraryInstruments_);
  sqlite3_bind_text(stmtDeleteLibraryInstruments_, 1, libraryId.toRawUTF8(),
                    -1, SQLITE_TRANSIENT);
  sqlite3_step(stmtDeleteLibraryInstruments_);

  sqlite3_reset(stmtDeleteLibrary_);
  sqlite3_bind_text(stmtDeleteLibrary_, 1, libraryId.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtDeleteLibrary_);

  exec("COMMIT");
  std::cerr << "[FiddleDB] Deleted library " << libraryId << std::endl;
}

std::vector<LibraryInstrumentRow> FiddleDatabase::loadAllLibraryInstruments() {
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<LibraryInstrumentRow> result;
  if (!stmtLoadAllLibraryInstruments_)
    return result;

  sqlite3_reset(stmtLoadAllLibraryInstruments_);
  while (sqlite3_step(stmtLoadAllLibraryInstruments_) == SQLITE_ROW) {
    LibraryInstrumentRow row;
    row.libraryId = colText(stmtLoadAllLibraryInstruments_, 0);
    row.sortOrder = sqlite3_column_int(stmtLoadAllLibraryInstruments_, 1);
    row.entityId = colText(stmtLoadAllLibraryInstruments_, 2);
    row.name = colText(stmtLoadAllLibraryInstruments_, 3);
    row.category = colText(stmtLoadAllLibraryInstruments_, 4);
    row.isSolo = sqlite3_column_int(stmtLoadAllLibraryInstruments_, 5) != 0;
    row.vstPlugin = colText(stmtLoadAllLibraryInstruments_, 6);
    row.exprMap = colText(stmtLoadAllLibraryInstruments_, 7);
    row.pluginUid = sqlite3_column_int(stmtLoadAllLibraryInstruments_, 8);
    if (auto *blobData = sqlite3_column_blob(stmtLoadAllLibraryInstruments_, 9)) {
      int blobSize = sqlite3_column_bytes(stmtLoadAllLibraryInstruments_, 9);
      row.pluginState.append(blobData, (size_t)blobSize);
    }
    row.family = colText(stmtLoadAllLibraryInstruments_, 10);
    row.instanceNums = LibraryInstrumentRow::parseInstanceNums(
        colText(stmtLoadAllLibraryInstruments_, 11));
    row.note = colText(stmtLoadAllLibraryInstruments_, 12);
    result.push_back(std::move(row));
  }
  return result;
}

} // namespace fiddle
