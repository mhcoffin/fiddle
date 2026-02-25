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
    SELECT data FROM saved_configs
    WHERE name = ? ORDER BY version DESC LIMIT 1
  )",
       &stmtLoadConfig_);

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
  fin(stmtLoadConfig_);
  fin(stmtListConfigs_);
  fin(stmtListConfigVersions_);
  fin(stmtDeleteConfig_);
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

void FiddleDatabase::saveConfig(const juce::String &name) {
  // Snapshot current strips as JSON
  std::lock_guard<std::mutex> lock(mutex_);
  if (!stmtLoadStrips_ || !stmtSaveConfig_)
    return;

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

  sqlite3_reset(stmtSaveConfig_);
  sqlite3_bind_text(stmtSaveConfig_, 1, name.toRawUTF8(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtSaveConfig_, 2, json.toRawUTF8(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtSaveConfig_) != SQLITE_DONE) {
    std::cerr << "[FiddleDB] saveConfig failed: " << sqlite3_errmsg(db_)
              << std::endl;
  } else {
    std::cerr << "[FiddleDB] Saved config '" << name << "' (new version)"
              << std::endl;
  }
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

} // namespace fiddle
