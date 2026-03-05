#include "FiddleDatabase.h"
#include <iostream>

namespace fiddle {

FiddleDatabase::FiddleDatabase() = default;

FiddleDatabase::~FiddleDatabase() { close(); }

bool FiddleDatabase::open(const juce::File &dbFile) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (db_) {
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
  exec("PRAGMA journal_mode=WAL");
  exec("PRAGMA synchronous=NORMAL");

  createSchema();
  prepareStatements();

  std::cerr << "[FiddleDB] Opened: " << dbFile.getFullPathName() << std::endl;
  return true;
}

void FiddleDatabase::close() {
  std::lock_guard<std::mutex> lock(mutex_);
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
      plugin_state   BLOB
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
      valid        INTEGER NOT NULL DEFAULT 1
    )
  )");

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
}

void FiddleDatabase::prepareStatements() {
  auto prep = [this](const char *sql, sqlite3_stmt **stmt) {
    int rc = sqlite3_prepare_v2(db_, sql, -1, stmt, nullptr);
    if (rc != SQLITE_OK) {
      std::cerr << "[FiddleDB] Prepare failed: " << sqlite3_errmsg(db_)
                << "\n  SQL: " << sql << std::endl;
    }
  };

  prep(R"(
    INSERT OR REPLACE INTO strips
      (id, position, library, family, is_solo, input_port, input_channel,
       plugin_uid, gain_db, expression_map)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )",
       &stmtSaveStrip_);

  prep(R"(
    UPDATE strips SET plugin_state = ? WHERE id = ?
  )",
       &stmtSaveBlob_);

  prep("DELETE FROM strips WHERE id = ?", &stmtRemoveStrip_);

  prep("DELETE FROM strips", &stmtClearStrips_);

  prep("SELECT id, position, library, family, is_solo, input_port, "
       "input_channel, plugin_uid, gain_db, expression_map, plugin_state "
       "FROM strips ORDER BY position",
       &stmtLoadStrips_);

  prep("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)",
       &stmtSaveSetting_);

  prep("SELECT value FROM settings WHERE key = ?", &stmtLoadSetting_);

  prep(R"(
    INSERT INTO saved_configs (name, version, data)
    VALUES (?, datetime('now'), ?)
  )",
       &stmtSaveConfig_);

  prep(R"(
    INSERT INTO saved_configs (name, version, data)
    VALUES (?, ?, ?)
  )",
       &stmtSaveConfigVersion_);

  prep(R"(
    SELECT data FROM saved_configs
    WHERE name = ? ORDER BY version DESC LIMIT 1
  )",
       &stmtLoadConfig_);

  prep(R"(
    SELECT data FROM saved_configs
    WHERE name = ? AND version = ?
  )",
       &stmtLoadConfigVersion_);

  prep(R"(
    SELECT COUNT(*) FROM saved_configs
    WHERE name = ? AND version = ?
  )",
       &stmtHasConfigVersion_);

  prep(R"(
    SELECT name, MAX(version) as version FROM saved_configs
    GROUP BY name ORDER BY version DESC
  )",
       &stmtListConfigs_);

  prep(R"(
    SELECT name, version FROM saved_configs
    WHERE name = ? ORDER BY version DESC
  )",
       &stmtListConfigVersions_);

  prep("DELETE FROM saved_configs WHERE name = ?", &stmtDeleteConfig_);

  prep(R"(
    INSERT OR REPLACE INTO plugin_capabilities (plugin_uid, uses_listener)
    VALUES (?, 1)
  )",
       &stmtSavePluginCapability_);

  prep("SELECT plugin_uid FROM plugin_capabilities WHERE uses_listener = 1",
       &stmtLoadListenerUids_);

  prep(R"(
    INSERT OR REPLACE INTO window_settings
      (window_id, x, y, width, height, visible, mode)
    VALUES (?, ?, ?, ?, ?, ?, ?)
  )",
       &stmtSaveWindowSettings_);

  prep("SELECT x, y, width, height, visible, mode FROM window_settings "
       "WHERE window_id = ?",
       &stmtLoadWindowSettings_);

  prep(R"(
    INSERT OR REPLACE INTO plugin_cache
      (path, mod_time, name, manufacturer, category, format,
       uid, num_inputs, num_outputs, valid)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )",
       &stmtSavePluginCache_);

  prep("SELECT path, mod_time, name, manufacturer, category, format, "
       "uid, num_inputs, num_outputs, valid FROM plugin_cache",
       &stmtLoadPluginCache_);

  prep("DELETE FROM plugin_cache WHERE path = ?", &stmtRemovePluginCache_);

  prep("DELETE FROM plugin_cache", &stmtClearPluginCache_);

  // Ensemble
  prep("DELETE FROM ensemble_slots", &stmtClearEnsemble_);
  prep("INSERT INTO ensemble_slots "
       "(sort_order, entity_id, name, family, music_xml_sound_id, "
       "solo_count, section_count) VALUES (?,?,?,?,?,?,?)",
       &stmtInsertEnsembleSlot_);
  prep("SELECT entity_id, name, family, music_xml_sound_id, solo_count, "
       "section_count FROM ensemble_slots ORDER BY sort_order",
       &stmtLoadEnsemble_);

  // Channel assignments
  prep("DELETE FROM channel_assignments", &stmtClearAssignments_);
  prep("INSERT OR REPLACE INTO channel_assignments "
       "(flat_index, entity_id, is_solo, instance_num) VALUES (?,?,?,?)",
       &stmtInsertAssignment_);
  prep("SELECT flat_index, entity_id, is_solo, instance_num "
       "FROM channel_assignments ORDER BY flat_index",
       &stmtLoadAssignments_);

  // Channel graveyard
  prep("DELETE FROM channel_graveyard", &stmtClearGraveyard_);
  prep("INSERT OR REPLACE INTO channel_graveyard "
       "(flat_index, entity_id, is_solo, instance_num) VALUES (?,?,?,?)",
       &stmtInsertGraveyard_);
  prep("SELECT flat_index FROM channel_graveyard "
       "WHERE entity_id = ? AND is_solo = ? ORDER BY instance_num LIMIT 1",
       &stmtFindGraveyard_);
  prep("DELETE FROM channel_graveyard WHERE flat_index = ?",
       &stmtRemoveGraveyard_);
  prep("SELECT flat_index, entity_id, is_solo, instance_num "
       "FROM channel_graveyard ORDER BY flat_index",
       &stmtLoadGraveyard_);
}

void FiddleDatabase::finalizeStatements() {
  auto fin = [](sqlite3_stmt *&stmt) {
    if (stmt) {
      sqlite3_finalize(stmt);
      stmt = nullptr;
    }
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

  sqlite3_reset(stmtSaveStrip_);
  sqlite3_bind_text(stmtSaveStrip_, 1, strip.id.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveStrip_, 2, position);
  sqlite3_bind_text(stmtSaveStrip_, 3, strip.library.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveStrip_, 4, strip.family.toRawUTF8(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtSaveStrip_, 5, strip.isSolo ? 1 : 0);
  sqlite3_bind_int(stmtSaveStrip_, 6, strip.inputPort);
  sqlite3_bind_int(stmtSaveStrip_, 7, strip.inputChannel);
  sqlite3_bind_int(stmtSaveStrip_, 8, strip.pluginUid);
  sqlite3_bind_double(stmtSaveStrip_, 9,
                      strip.gainDb.load(std::memory_order_relaxed));

  std::string xmapId = strip.expressionMap ? strip.expressionMap->entityID : "";
  sqlite3_bind_text(stmtSaveStrip_, 10, xmapId.c_str(), -1, SQLITE_TRANSIENT);

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

    rows.push_back(std::move(row));
  }

  std::cerr << "[FiddleDB] Loaded " << rows.size() << " strips" << std::endl;
  return rows;
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
  sqlite3_bind_int(stmtSavePluginCache_, 10, row.valid ? 1 : 0);

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
    row.valid = sqlite3_column_int(stmtLoadPluginCache_, 9) != 0;
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

} // namespace fiddle
