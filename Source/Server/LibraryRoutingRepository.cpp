#include "LibraryRoutingRepository.h"

#include <utility>

namespace fiddle {
namespace {

class Statement {
public:
  Statement(sqlite3 *database, const char *sql) {
    sqlite3_prepare_v2(database, sql, -1, &statement_, nullptr);
  }
  ~Statement() {
    if (statement_)
      sqlite3_finalize(statement_);
  }
  Statement(const Statement &) = delete;
  Statement &operator=(const Statement &) = delete;

  sqlite3_stmt *get() const { return statement_; }
  explicit operator bool() const { return statement_ != nullptr; }

private:
  sqlite3_stmt *statement_ = nullptr;
};

bool execute(sqlite3 *database, const char *sql) {
  return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

void bindText(sqlite3_stmt *statement, int index, const std::string &value) {
  sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
}

void bindBlob(sqlite3_stmt *statement, int index,
              const std::vector<std::uint8_t> &value) {
  if (value.empty()) {
    sqlite3_bind_null(statement, index);
    return;
  }
  sqlite3_bind_blob(statement, index, value.data(),
                    static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

std::string columnText(sqlite3_stmt *statement, int column) {
  const auto *text = sqlite3_column_text(statement, column);
  return text ? reinterpret_cast<const char *>(text) : std::string{};
}

std::vector<std::uint8_t> columnBlob(sqlite3_stmt *statement, int column) {
  const auto *data = static_cast<const std::uint8_t *>(
      sqlite3_column_blob(statement, column));
  const int size = sqlite3_column_bytes(statement, column);
  if (!data || size <= 0)
    return {};
  return {data, data + size};
}

LibraryPatchRow readPatch(sqlite3_stmt *statement) {
  LibraryPatchRow patch;
  patch.id = columnText(statement, 0);
  patch.libraryId = columnText(statement, 1);
  patch.position = sqlite3_column_int(statement, 2);
  patch.name = columnText(statement, 3);
  patch.instrumentEntityId = columnText(statement, 4);
  patch.family = columnText(statement, 5);
  patch.character = columnText(statement, 6);
  patch.pluginUid = sqlite3_column_int(statement, 7);
  patch.pluginState = columnBlob(statement, 8);
  patch.expressionMapId = columnText(statement, 9);
  return patch;
}

ChairRow readChair(sqlite3_stmt *statement) {
  ChairRow chair;
  chair.id = columnText(statement, 0);
  chair.instrumentEntityId = columnText(statement, 1);
  chair.name = columnText(statement, 2);
  chair.family = columnText(statement, 3);
  chair.role = sqlite3_column_int(statement, 4) == 0 ? DoricoRole::solo
                                                      : DoricoRole::section;
  chair.ordinal = sqlite3_column_int(statement, 5);
  chair.displayOrder = sqlite3_column_int(statement, 6);
  chair.flatIndex = sqlite3_column_int(statement, 7);
  return chair;
}

LayerRow readLayer(sqlite3_stmt *statement) {
  LayerRow layer;
  layer.id = columnText(statement, 0);
  layer.chairId = columnText(statement, 1);
  layer.patchId = columnText(statement, 2);
  layer.position = sqlite3_column_int(statement, 3);
  layer.active = sqlite3_column_int(statement, 4) != 0;
  layer.muted = sqlite3_column_int(statement, 5) != 0;
  layer.soloed = sqlite3_column_int(statement, 6) != 0;
  layer.gainDb = static_cast<float>(sqlite3_column_double(statement, 7));
  layer.pluginUid = sqlite3_column_int(statement, 8);
  layer.pluginState = columnBlob(statement, 9);
  layer.expressionMapId = columnText(statement, 10);
  return layer;
}

constexpr const char *kPatchColumns =
    "id, library_id, position, name, instrument_entity_id, family, "
    "character, plugin_uid, plugin_state, expression_map_id";
constexpr const char *kChairColumns =
    "id, instrument_entity_id, name, family, dorico_role, ordinal, "
    "display_order, flat_index";
constexpr const char *kLayerColumns =
    "id, chair_id, patch_id, position, active, muted, soloed, gain_db, "
    "plugin_uid, plugin_state, expression_map_id";

} // namespace

LibraryRoutingRepository::LibraryRoutingRepository(
    sqlite3 *database, std::mutex &databaseMutex)
    : database_(database), databaseMutex_(databaseMutex) {}

bool LibraryRoutingRepository::ensureSchema(sqlite3 *database) {
  if (!database)
    return false;

  return execute(database, R"(
    CREATE TABLE IF NOT EXISTS library_patches (
      id                   TEXT PRIMARY KEY,
      library_id           TEXT NOT NULL,
      position             INTEGER NOT NULL DEFAULT 0,
      name                 TEXT NOT NULL,
      instrument_entity_id TEXT NOT NULL DEFAULT '',
      family               TEXT NOT NULL DEFAULT '',
      character            TEXT NOT NULL DEFAULT '',
      plugin_uid           INTEGER NOT NULL DEFAULT 0,
      plugin_state         BLOB,
      expression_map_id    TEXT NOT NULL DEFAULT '',
      FOREIGN KEY (library_id) REFERENCES libraries(id) ON DELETE RESTRICT
    )
  )") &&
         execute(database, R"(
    CREATE INDEX IF NOT EXISTS library_patches_library_position
    ON library_patches(library_id, position)
  )") &&
         execute(database, R"(
    CREATE TABLE IF NOT EXISTS chairs (
      id                   TEXT PRIMARY KEY,
      instrument_entity_id TEXT NOT NULL,
      name                 TEXT NOT NULL,
      family               TEXT NOT NULL DEFAULT '',
      dorico_role          INTEGER NOT NULL CHECK (dorico_role IN (0, 1)),
      ordinal              INTEGER NOT NULL DEFAULT 1 CHECK (ordinal > 0),
      display_order        INTEGER NOT NULL DEFAULT 0,
      flat_index           INTEGER NOT NULL CHECK (flat_index > 0),
      UNIQUE (instrument_entity_id, dorico_role, ordinal),
      UNIQUE (flat_index)
    )
  )") &&
         execute(database, R"(
    CREATE INDEX IF NOT EXISTS chairs_display_order
    ON chairs(display_order)
  )") &&
         execute(database, R"(
    CREATE TABLE IF NOT EXISTS chair_midi_graveyard (
      instrument_entity_id TEXT NOT NULL,
      dorico_role          INTEGER NOT NULL CHECK (dorico_role IN (0, 1)),
      ordinal              INTEGER NOT NULL CHECK (ordinal > 0),
      flat_index           INTEGER NOT NULL UNIQUE CHECK (flat_index > 0),
      deleted_at           INTEGER NOT NULL DEFAULT (unixepoch()),
      PRIMARY KEY (instrument_entity_id, dorico_role, flat_index)
    )
  )") &&
         execute(database, R"(
    CREATE TABLE IF NOT EXISTS layers (
      id                TEXT PRIMARY KEY,
      chair_id          TEXT NOT NULL,
      patch_id          TEXT NOT NULL,
      position          INTEGER NOT NULL DEFAULT 0,
      active            INTEGER NOT NULL DEFAULT 1,
      muted             INTEGER NOT NULL DEFAULT 0,
      soloed            INTEGER NOT NULL DEFAULT 0,
      gain_db           REAL NOT NULL DEFAULT 0.0,
      plugin_uid        INTEGER NOT NULL DEFAULT 0,
      plugin_state      BLOB,
      expression_map_id TEXT NOT NULL DEFAULT '',
      FOREIGN KEY (chair_id) REFERENCES chairs(id) ON DELETE RESTRICT,
      FOREIGN KEY (patch_id) REFERENCES library_patches(id) ON DELETE RESTRICT
    )
  )") &&
         execute(database, R"(
    CREATE INDEX IF NOT EXISTS layers_chair_position
    ON layers(chair_id, position)
  )") &&
         execute(database, R"(
    CREATE INDEX IF NOT EXISTS layers_patch
    ON layers(patch_id)
  )");
}

bool LibraryRoutingRepository::upsertPatch(const LibraryPatchRow &patch) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    INSERT INTO library_patches
      (id, library_id, position, name, instrument_entity_id, family,
       character, plugin_uid, plugin_state, expression_map_id)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      library_id = excluded.library_id,
      position = excluded.position,
      name = excluded.name,
      instrument_entity_id = excluded.instrument_entity_id,
      family = excluded.family,
      character = excluded.character,
      plugin_uid = excluded.plugin_uid,
      plugin_state = excluded.plugin_state,
      expression_map_id = excluded.expression_map_id
  )");
  if (!statement)
    return false;
  bindText(statement.get(), 1, patch.id);
  bindText(statement.get(), 2, patch.libraryId);
  sqlite3_bind_int(statement.get(), 3, patch.position);
  bindText(statement.get(), 4, patch.name);
  bindText(statement.get(), 5, patch.instrumentEntityId);
  bindText(statement.get(), 6, patch.family);
  bindText(statement.get(), 7, patch.character);
  sqlite3_bind_int(statement.get(), 8, patch.pluginUid);
  bindBlob(statement.get(), 9, patch.pluginState);
  bindText(statement.get(), 10, patch.expressionMapId);
  return sqlite3_step(statement.get()) == SQLITE_DONE;
}

std::optional<LibraryPatchRow>
LibraryRoutingRepository::getPatch(const std::string &patchId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  const std::string sql = std::string("SELECT ") + kPatchColumns +
                          " FROM library_patches WHERE id = ?";
  Statement statement(database_, sql.c_str());
  if (!statement)
    return std::nullopt;
  bindText(statement.get(), 1, patchId);
  if (sqlite3_step(statement.get()) != SQLITE_ROW)
    return std::nullopt;
  return readPatch(statement.get());
}

std::vector<LibraryPatchRow>
LibraryRoutingRepository::listPatches(const std::string &libraryId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  const bool filtered = !libraryId.empty();
  const std::string sql = std::string("SELECT ") + kPatchColumns +
      " FROM library_patches" + (filtered ? " WHERE library_id = ?" : "") +
      " ORDER BY library_id, position, name";
  Statement statement(database_, sql.c_str());
  std::vector<LibraryPatchRow> result;
  if (!statement)
    return result;
  if (filtered)
    bindText(statement.get(), 1, libraryId);
  while (sqlite3_step(statement.get()) == SQLITE_ROW)
    result.push_back(readPatch(statement.get()));
  return result;
}

int LibraryRoutingRepository::patchUsageCount(
    const std::string &patchId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, "SELECT COUNT(*) FROM layers WHERE patch_id = ?");
  if (!statement)
    return 0;
  bindText(statement.get(), 1, patchId);
  return sqlite3_step(statement.get()) == SQLITE_ROW
             ? sqlite3_column_int(statement.get(), 0)
             : 0;
}

PatchDeleteResult
LibraryRoutingRepository::deletePatch(const std::string &patchId) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement usage(database_, "SELECT COUNT(*) FROM layers WHERE patch_id = ?");
  if (!usage)
    return PatchDeleteResult::error;
  bindText(usage.get(), 1, patchId);
  if (sqlite3_step(usage.get()) != SQLITE_ROW)
    return PatchDeleteResult::error;
  if (sqlite3_column_int(usage.get(), 0) > 0)
    return PatchDeleteResult::referenced;

  Statement statement(database_, "DELETE FROM library_patches WHERE id = ?");
  if (!statement)
    return PatchDeleteResult::error;
  bindText(statement.get(), 1, patchId);
  if (sqlite3_step(statement.get()) != SQLITE_DONE)
    return PatchDeleteResult::error;
  return sqlite3_changes(database_) == 0 ? PatchDeleteResult::notFound
                                         : PatchDeleteResult::deleted;
}

bool LibraryRoutingRepository::insertChairWithStableAssignment(
    ChairRow &chair) {
  if (chair.id.empty() || chair.instrumentEntityId.empty() ||
      chair.name.empty())
    return false;

  std::lock_guard<std::mutex> lock(databaseMutex_);
  if (!execute(database_, "BEGIN IMMEDIATE"))
    return false;

  const int role = chair.role == DoricoRole::solo ? 0 : 1;
  Statement ordinal(database_, R"(
    SELECT COALESCE(MAX(ordinal), 0) + 1
    FROM chairs WHERE instrument_entity_id = ? AND dorico_role = ?
  )");
  Statement displayOrder(database_,
                         "SELECT COALESCE(MAX(display_order), -1) + 1 "
                         "FROM chairs");
  Statement reclaimed(database_, R"(
    SELECT ordinal, flat_index FROM chair_midi_graveyard
    WHERE instrument_entity_id = ? AND dorico_role = ?
    ORDER BY flat_index LIMIT 1
  )");
  Statement nextIndex(database_, R"(
    SELECT COALESCE(MAX(flat_index), 0) + 1 FROM (
      SELECT flat_index FROM chairs
      UNION ALL
      SELECT flat_index FROM chair_midi_graveyard
    )
  )");
  if (!ordinal || !displayOrder || !reclaimed || !nextIndex) {
    execute(database_, "ROLLBACK");
    return false;
  }

  bindText(ordinal.get(), 1, chair.instrumentEntityId);
  sqlite3_bind_int(ordinal.get(), 2, role);
  bindText(reclaimed.get(), 1, chair.instrumentEntityId);
  sqlite3_bind_int(reclaimed.get(), 2, role);
  if (sqlite3_step(ordinal.get()) != SQLITE_ROW ||
      sqlite3_step(displayOrder.get()) != SQLITE_ROW) {
    execute(database_, "ROLLBACK");
    return false;
  }
  chair.displayOrder = sqlite3_column_int(displayOrder.get(), 0);

  const bool isReclaimed = sqlite3_step(reclaimed.get()) == SQLITE_ROW;
  if (isReclaimed) {
    chair.ordinal = sqlite3_column_int(reclaimed.get(), 0);
    chair.flatIndex = sqlite3_column_int(reclaimed.get(), 1);
  } else {
    chair.ordinal = sqlite3_column_int(ordinal.get(), 0);
    if (sqlite3_step(nextIndex.get()) != SQLITE_ROW) {
      execute(database_, "ROLLBACK");
      return false;
    }
    chair.flatIndex = sqlite3_column_int(nextIndex.get(), 0);
  }

  // Port 0/channel 1 is reserved for the metronome. Keep the established
  // 240-slot ceiling used by MasterInstrumentList's stable assignments.
  if (chair.flatIndex <= 0 || chair.flatIndex >= 240) {
    execute(database_, "ROLLBACK");
    return false;
  }

  Statement insert(database_, R"(
    INSERT INTO chairs
      (id, instrument_entity_id, name, family, dorico_role, ordinal,
       display_order, flat_index)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  )");
  if (!insert) {
    execute(database_, "ROLLBACK");
    return false;
  }
  bindText(insert.get(), 1, chair.id);
  bindText(insert.get(), 2, chair.instrumentEntityId);
  bindText(insert.get(), 3, chair.name);
  bindText(insert.get(), 4, chair.family);
  sqlite3_bind_int(insert.get(), 5, role);
  sqlite3_bind_int(insert.get(), 6, chair.ordinal);
  sqlite3_bind_int(insert.get(), 7, chair.displayOrder);
  sqlite3_bind_int(insert.get(), 8, chair.flatIndex);
  if (sqlite3_step(insert.get()) != SQLITE_DONE) {
    execute(database_, "ROLLBACK");
    return false;
  }

  if (isReclaimed) {
    Statement removeGraveyard(database_, R"(
      DELETE FROM chair_midi_graveyard
      WHERE instrument_entity_id = ? AND dorico_role = ? AND flat_index = ?
    )");
    if (!removeGraveyard) {
      execute(database_, "ROLLBACK");
      return false;
    }
    bindText(removeGraveyard.get(), 1, chair.instrumentEntityId);
    sqlite3_bind_int(removeGraveyard.get(), 2, role);
    sqlite3_bind_int(removeGraveyard.get(), 3, chair.flatIndex);
    if (sqlite3_step(removeGraveyard.get()) != SQLITE_DONE) {
      execute(database_, "ROLLBACK");
      return false;
    }
  }

  if (!execute(database_, "COMMIT")) {
    execute(database_, "ROLLBACK");
    return false;
  }
  return true;
}

bool LibraryRoutingRepository::upsertChair(const ChairRow &chair) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    INSERT INTO chairs
      (id, instrument_entity_id, name, family, dorico_role, ordinal,
       display_order, flat_index)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      instrument_entity_id = excluded.instrument_entity_id,
      name = excluded.name,
      family = excluded.family,
      dorico_role = excluded.dorico_role,
      ordinal = excluded.ordinal,
      display_order = excluded.display_order,
      flat_index = excluded.flat_index
  )");
  if (!statement)
    return false;
  bindText(statement.get(), 1, chair.id);
  bindText(statement.get(), 2, chair.instrumentEntityId);
  bindText(statement.get(), 3, chair.name);
  bindText(statement.get(), 4, chair.family);
  sqlite3_bind_int(statement.get(), 5,
                   chair.role == DoricoRole::solo ? 0 : 1);
  sqlite3_bind_int(statement.get(), 6, chair.ordinal);
  sqlite3_bind_int(statement.get(), 7, chair.displayOrder);
  sqlite3_bind_int(statement.get(), 8, chair.flatIndex);
  return sqlite3_step(statement.get()) == SQLITE_DONE;
}

std::optional<ChairRow>
LibraryRoutingRepository::getChair(const std::string &chairId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  const std::string sql = std::string("SELECT ") + kChairColumns +
                          " FROM chairs WHERE id = ?";
  Statement statement(database_, sql.c_str());
  if (!statement)
    return std::nullopt;
  bindText(statement.get(), 1, chairId);
  if (sqlite3_step(statement.get()) != SQLITE_ROW)
    return std::nullopt;
  return readChair(statement.get());
}

std::vector<ChairRow> LibraryRoutingRepository::listChairs() const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  const std::string sql = std::string("SELECT ") + kChairColumns +
                          " FROM chairs ORDER BY display_order, flat_index";
  Statement statement(database_, sql.c_str());
  std::vector<ChairRow> result;
  if (!statement)
    return result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW)
    result.push_back(readChair(statement.get()));
  return result;
}

bool LibraryRoutingRepository::deleteChairAndLayers(
    const std::string &chairId) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  if (!execute(database_, "BEGIN IMMEDIATE"))
    return false;

  Statement findChair(database_, R"(
    SELECT instrument_entity_id, dorico_role, ordinal, flat_index
    FROM chairs WHERE id = ?
  )");
  if (!findChair) {
    execute(database_, "ROLLBACK");
    return false;
  }
  bindText(findChair.get(), 1, chairId);
  if (sqlite3_step(findChair.get()) != SQLITE_ROW) {
    execute(database_, "ROLLBACK");
    return false;
  }
  const auto entityId = columnText(findChair.get(), 0);
  const int role = sqlite3_column_int(findChair.get(), 1);
  const int chairOrdinal = sqlite3_column_int(findChair.get(), 2);
  const int flatIndex = sqlite3_column_int(findChair.get(), 3);

  Statement rememberAssignment(database_, R"(
    INSERT OR REPLACE INTO chair_midi_graveyard
      (instrument_entity_id, dorico_role, ordinal, flat_index, deleted_at)
    VALUES (?, ?, ?, ?, unixepoch())
  )");
  Statement deleteLayers(database_, "DELETE FROM layers WHERE chair_id = ?");
  Statement deleteChair(database_, "DELETE FROM chairs WHERE id = ?");
  if (!rememberAssignment || !deleteLayers || !deleteChair) {
    execute(database_, "ROLLBACK");
    return false;
  }
  bindText(rememberAssignment.get(), 1, entityId);
  sqlite3_bind_int(rememberAssignment.get(), 2, role);
  sqlite3_bind_int(rememberAssignment.get(), 3, chairOrdinal);
  sqlite3_bind_int(rememberAssignment.get(), 4, flatIndex);
  bindText(deleteLayers.get(), 1, chairId);
  bindText(deleteChair.get(), 1, chairId);
  const bool succeeded =
      sqlite3_step(rememberAssignment.get()) == SQLITE_DONE &&
      sqlite3_step(deleteLayers.get()) == SQLITE_DONE &&
      sqlite3_step(deleteChair.get()) == SQLITE_DONE &&
      sqlite3_changes(database_) > 0;
  if (!succeeded || !execute(database_, "COMMIT")) {
    execute(database_, "ROLLBACK");
    return false;
  }
  return true;
}

bool LibraryRoutingRepository::upsertLayer(const LayerRow &layer) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    INSERT INTO layers
      (id, chair_id, patch_id, position, active, muted, soloed, gain_db,
       plugin_uid, plugin_state, expression_map_id)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      chair_id = excluded.chair_id,
      patch_id = excluded.patch_id,
      position = excluded.position,
      active = excluded.active,
      muted = excluded.muted,
      soloed = excluded.soloed,
      gain_db = excluded.gain_db,
      plugin_uid = excluded.plugin_uid,
      plugin_state = excluded.plugin_state,
      expression_map_id = excluded.expression_map_id
  )");
  if (!statement)
    return false;
  bindText(statement.get(), 1, layer.id);
  bindText(statement.get(), 2, layer.chairId);
  bindText(statement.get(), 3, layer.patchId);
  sqlite3_bind_int(statement.get(), 4, layer.position);
  sqlite3_bind_int(statement.get(), 5, layer.active ? 1 : 0);
  sqlite3_bind_int(statement.get(), 6, layer.muted ? 1 : 0);
  sqlite3_bind_int(statement.get(), 7, layer.soloed ? 1 : 0);
  sqlite3_bind_double(statement.get(), 8, layer.gainDb);
  sqlite3_bind_int(statement.get(), 9, layer.pluginUid);
  bindBlob(statement.get(), 10, layer.pluginState);
  bindText(statement.get(), 11, layer.expressionMapId);
  return sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool LibraryRoutingRepository::createLayerFromPatch(
    const std::string &layerId, const std::string &chairId,
    const std::string &patchId, int position) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    INSERT INTO layers
      (id, chair_id, patch_id, position, plugin_uid, plugin_state,
       expression_map_id)
    SELECT ?, ?, id, ?, plugin_uid, plugin_state, expression_map_id
    FROM library_patches WHERE id = ?
  )");
  if (!statement)
    return false;
  bindText(statement.get(), 1, layerId);
  bindText(statement.get(), 2, chairId);
  sqlite3_bind_int(statement.get(), 3, position);
  bindText(statement.get(), 4, patchId);
  return sqlite3_step(statement.get()) == SQLITE_DONE &&
         sqlite3_changes(database_) == 1;
}

std::optional<LayerRow>
LibraryRoutingRepository::getLayer(const std::string &layerId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  const std::string sql = std::string("SELECT ") + kLayerColumns +
                          " FROM layers WHERE id = ?";
  Statement statement(database_, sql.c_str());
  if (!statement)
    return std::nullopt;
  bindText(statement.get(), 1, layerId);
  if (sqlite3_step(statement.get()) != SQLITE_ROW)
    return std::nullopt;
  return readLayer(statement.get());
}

std::vector<LayerRow>
LibraryRoutingRepository::listLayers(const std::string &chairId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  const bool filtered = !chairId.empty();
  const std::string sql = std::string("SELECT ") + kLayerColumns +
      " FROM layers" + (filtered ? " WHERE chair_id = ?" : "") +
      " ORDER BY chair_id, position, id";
  Statement statement(database_, sql.c_str());
  std::vector<LayerRow> result;
  if (!statement)
    return result;
  if (filtered)
    bindText(statement.get(), 1, chairId);
  while (sqlite3_step(statement.get()) == SQLITE_ROW)
    result.push_back(readLayer(statement.get()));
  return result;
}

bool LibraryRoutingRepository::deleteLayer(const std::string &layerId) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, "DELETE FROM layers WHERE id = ?");
  if (!statement)
    return false;
  bindText(statement.get(), 1, layerId);
  return sqlite3_step(statement.get()) == SQLITE_DONE &&
         sqlite3_changes(database_) == 1;
}

} // namespace fiddle
