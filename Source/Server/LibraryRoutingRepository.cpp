#include "LibraryRoutingRepository.h"

#include <set>
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

bool hasColumn(sqlite3 *database, const char *table, const char *column) {
  Statement statement(database,
                      (std::string("PRAGMA table_info(") + table + ")").c_str());
  if (!statement)
    return false;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const auto *text = sqlite3_column_text(statement.get(), 1);
    if (text && std::string(reinterpret_cast<const char *>(text)) == column)
      return true;
  }
  return false;
}

bool ensureColumn(sqlite3 *database, const char *table, const char *column,
                  const char *definition) {
  if (hasColumn(database, table, column))
    return true;
  return execute(database, (std::string("ALTER TABLE ") + table +
                            " ADD COLUMN " + definition)
                               .c_str());
}

bool layersHaveCatalogForeignKey(sqlite3 *database) {
  Statement statement(database, "PRAGMA foreign_key_list(layers)");
  if (!statement)
    return false;
  while (sqlite3_step(statement.get()) == SQLITE_ROW) {
    const auto *text = sqlite3_column_text(statement.get(), 2);
    if (text && std::string(reinterpret_cast<const char *>(text)) ==
                    "library_patches")
      return true;
  }
  return false;
}

bool removeLayersCatalogForeignKey(sqlite3 *database) {
  if (!layersHaveCatalogForeignKey(database))
    return true;

  // SQLite cannot drop one foreign key in place. Rebuild this small routing
  // table so a historical layer can deliberately retain a missing patch ID.
  if (!execute(database, "PRAGMA foreign_keys=OFF"))
    return false;
  if (!execute(database, "BEGIN IMMEDIATE")) {
    execute(database, "PRAGMA foreign_keys=ON");
    return false;
  }
  const bool succeeded = execute(database, R"(
    ALTER TABLE layers RENAME TO layers_with_catalog_fk
  )") &&
      execute(database, R"(
    CREATE TABLE layers (
      id TEXT PRIMARY KEY, chair_id TEXT NOT NULL, patch_id TEXT NOT NULL,
      patch_name TEXT NOT NULL DEFAULT '', library_id TEXT NOT NULL DEFAULT '',
      library_name TEXT NOT NULL DEFAULT '', position INTEGER NOT NULL DEFAULT 0,
      active INTEGER NOT NULL DEFAULT 1, muted INTEGER NOT NULL DEFAULT 0,
      soloed INTEGER NOT NULL DEFAULT 0, gain_db REAL NOT NULL DEFAULT 0.0,
      plugin_uid INTEGER NOT NULL DEFAULT 0, plugin_state BLOB,
      expression_map_id TEXT NOT NULL DEFAULT '',
      source_patch_revision INTEGER NOT NULL DEFAULT 0,
      plugin_state_edited INTEGER NOT NULL DEFAULT 0,
      FOREIGN KEY (chair_id) REFERENCES chairs(id) ON DELETE RESTRICT
    )
  )") &&
      execute(database, R"(
    INSERT INTO layers
      (id, chair_id, patch_id, patch_name, library_id, library_name, position,
       active, muted, soloed, gain_db, plugin_uid, plugin_state,
       expression_map_id, source_patch_revision, plugin_state_edited)
    SELECT id, chair_id, patch_id, patch_name, library_id, library_name,
           position, active, muted, soloed, gain_db, plugin_uid, plugin_state,
           expression_map_id, source_patch_revision, plugin_state_edited
    FROM layers_with_catalog_fk
  )") &&
      execute(database, "DROP TABLE layers_with_catalog_fk") &&
      execute(database, "COMMIT");
  if (!succeeded)
    execute(database, "ROLLBACK");
  const bool foreignKeysRestored = execute(database, "PRAGMA foreign_keys=ON");
  return succeeded && foreignKeysRestored;
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
  patch.revision = sqlite3_column_int(statement, 10);
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
  layer.patchName = columnText(statement, 3);
  layer.libraryId = columnText(statement, 4);
  layer.libraryName = columnText(statement, 5);
  layer.position = sqlite3_column_int(statement, 6);
  layer.active = sqlite3_column_int(statement, 7) != 0;
  layer.muted = sqlite3_column_int(statement, 8) != 0;
  layer.soloed = sqlite3_column_int(statement, 9) != 0;
  layer.gainDb = static_cast<float>(sqlite3_column_double(statement, 10));
  layer.pluginUid = sqlite3_column_int(statement, 11);
  layer.pluginState = columnBlob(statement, 12);
  layer.expressionMapId = columnText(statement, 13);
  layer.sourcePatchRevision = sqlite3_column_int(statement, 14);
  layer.pluginStateEdited = sqlite3_column_int(statement, 15) != 0;
  return layer;
}

constexpr const char *kPatchColumns =
    "id, library_id, position, name, instrument_entity_id, family, "
    "character, plugin_uid, plugin_state, expression_map_id, revision";
constexpr const char *kChairColumns =
    "id, instrument_entity_id, name, family, dorico_role, ordinal, "
    "display_order, flat_index";
constexpr const char *kLayerColumns =
    "id, chair_id, patch_id, patch_name, library_id, library_name, position, "
    "active, muted, soloed, gain_db, plugin_uid, plugin_state, "
    "expression_map_id, source_patch_revision, plugin_state_edited";

} // namespace

LibraryRoutingRepository::LibraryRoutingRepository(
    sqlite3 *database, std::mutex &databaseMutex)
    : database_(database), databaseMutex_(databaseMutex) {}

bool LibraryRoutingRepository::ensureSchema(sqlite3 *database) {
  if (!database)
    return false;

  const bool hadSourcePatchRevision =
      hasColumn(database, "layers", "source_patch_revision");

  const bool baseSchema = execute(database, R"(
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
      revision             INTEGER NOT NULL DEFAULT 1,
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
      patch_name        TEXT NOT NULL DEFAULT '',
      library_id        TEXT NOT NULL DEFAULT '',
      library_name      TEXT NOT NULL DEFAULT '',
      position          INTEGER NOT NULL DEFAULT 0,
      active            INTEGER NOT NULL DEFAULT 1,
      muted             INTEGER NOT NULL DEFAULT 0,
      soloed            INTEGER NOT NULL DEFAULT 0,
      gain_db           REAL NOT NULL DEFAULT 0.0,
      plugin_uid        INTEGER NOT NULL DEFAULT 0,
      plugin_state      BLOB,
      expression_map_id TEXT NOT NULL DEFAULT '',
      source_patch_revision INTEGER NOT NULL DEFAULT 0,
      plugin_state_edited INTEGER NOT NULL DEFAULT 0,
      FOREIGN KEY (chair_id) REFERENCES chairs(id) ON DELETE RESTRICT
    )
  )");
  if (!baseSchema ||
      !ensureColumn(database, "library_patches", "revision",
                    "revision INTEGER NOT NULL DEFAULT 1") ||
      !ensureColumn(database, "layers", "patch_name",
                    "patch_name TEXT NOT NULL DEFAULT ''") ||
      !ensureColumn(database, "layers", "library_id",
                    "library_id TEXT NOT NULL DEFAULT ''") ||
      !ensureColumn(database, "layers", "library_name",
                    "library_name TEXT NOT NULL DEFAULT ''") ||
      !ensureColumn(database, "layers", "source_patch_revision",
                    "source_patch_revision INTEGER NOT NULL DEFAULT 0") ||
      !ensureColumn(database, "layers", "plugin_state_edited",
                    "plugin_state_edited INTEGER NOT NULL DEFAULT 0") ||
      !removeLayersCatalogForeignKey(database))
    return false;

  // Existing rows predate stable synchronization tracking. Treat their
  // current setup as the baseline once, rather than reporting every VST's
  // harmless state reserialization as an outstanding library change.
  if (!hadSourcePatchRevision && !execute(database, R"(
    UPDATE layers
    SET source_patch_revision = COALESCE(
      (SELECT revision FROM library_patches
       WHERE id = layers.patch_id), 0)
  )"))
    return false;

  return execute(database, R"(
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
      revision = CASE WHEN
        library_patches.library_id <> excluded.library_id OR
        library_patches.name <> excluded.name OR
        library_patches.instrument_entity_id <>
          excluded.instrument_entity_id OR
        library_patches.family <> excluded.family OR
        library_patches.character <> excluded.character OR
        library_patches.plugin_uid <> excluded.plugin_uid OR
        COALESCE(library_patches.plugin_state, X'') <>
          COALESCE(excluded.plugin_state, X'') OR
        library_patches.expression_map_id <> excluded.expression_map_id
        THEN library_patches.revision + 1
        ELSE library_patches.revision END,
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

int LibraryRoutingRepository::outOfDateLayerCount(
    const std::string &patchId) const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    SELECT COUNT(*)
    FROM layers layer
    JOIN library_patches patch ON patch.id = layer.patch_id
    LEFT JOIN libraries library ON library.id = patch.library_id
    WHERE patch.id = ? AND (
      layer.patch_name <> patch.name OR
      layer.library_id <> patch.library_id OR
      layer.library_name <> COALESCE(library.name, '') OR
      layer.plugin_uid <> patch.plugin_uid OR
      layer.expression_map_id <> patch.expression_map_id OR
      layer.source_patch_revision <> patch.revision OR
      layer.plugin_state_edited <> 0
    )
  )");
  if (!statement)
    return 0;
  bindText(statement.get(), 1, patchId);
  return sqlite3_step(statement.get()) == SQLITE_ROW
             ? sqlite3_column_int(statement.get(), 0)
             : 0;
}

std::vector<std::string>
LibraryRoutingRepository::outOfDateLayerIds() const {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    SELECT layer.id
    FROM layers layer
    JOIN library_patches patch ON patch.id = layer.patch_id
    LEFT JOIN libraries library ON library.id = patch.library_id
    WHERE
      layer.patch_name <> patch.name OR
      layer.library_id <> patch.library_id OR
      layer.library_name <> COALESCE(library.name, '') OR
      layer.plugin_uid <> patch.plugin_uid OR
      layer.expression_map_id <> patch.expression_map_id OR
      layer.source_patch_revision <> patch.revision OR
      layer.plugin_state_edited <> 0
    ORDER BY layer.id
  )");
  std::vector<std::string> result;
  if (!statement)
    return result;
  while (sqlite3_step(statement.get()) == SQLITE_ROW)
    result.push_back(columnText(statement.get(), 0));
  return result;
}

std::optional<int>
LibraryRoutingRepository::updateLayersFromPatch(
    const std::string &patchId) {
  std::lock_guard<std::mutex> lock(databaseMutex_);

  std::string patchName;
  std::string libraryId;
  std::string libraryName;
  int pluginUid = 0;
  std::vector<std::uint8_t> pluginState;
  std::string expressionMapId;
  int patchRevision = 0;
  {
    Statement patchStatement(database_, R"(
      SELECT p.name, p.library_id, COALESCE(l.name, ''), p.plugin_uid,
             p.plugin_state, p.expression_map_id, p.revision
      FROM library_patches p
      LEFT JOIN libraries l ON l.id = p.library_id
      WHERE p.id = ?
    )");
    if (!patchStatement)
      return std::nullopt;
    bindText(patchStatement.get(), 1, patchId);
    if (sqlite3_step(patchStatement.get()) != SQLITE_ROW)
      return std::nullopt;

    patchName = columnText(patchStatement.get(), 0);
    libraryId = columnText(patchStatement.get(), 1);
    libraryName = columnText(patchStatement.get(), 2);
    pluginUid = sqlite3_column_int(patchStatement.get(), 3);
    pluginState = columnBlob(patchStatement.get(), 4);
    expressionMapId = columnText(patchStatement.get(), 5);
    patchRevision = sqlite3_column_int(patchStatement.get(), 6);
  }

  Statement update(database_, R"(
    UPDATE layers SET
      patch_name = ?, library_id = ?, library_name = ?, plugin_uid = ?,
      plugin_state = ?, expression_map_id = ?, source_patch_revision = ?,
      plugin_state_edited = 0
    WHERE patch_id = ?
  )");
  if (!update)
    return std::nullopt;
  bindText(update.get(), 1, patchName);
  bindText(update.get(), 2, libraryId);
  bindText(update.get(), 3, libraryName);
  sqlite3_bind_int(update.get(), 4, pluginUid);
  bindBlob(update.get(), 5, pluginState);
  bindText(update.get(), 6, expressionMapId);
  sqlite3_bind_int(update.get(), 7, patchRevision);
  bindText(update.get(), 8, patchId);
  if (sqlite3_step(update.get()) != SQLITE_DONE)
    return std::nullopt;
  return sqlite3_changes(database_);
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

PatchReplaceResult LibraryRoutingRepository::replaceLibraryPatches(
    const std::string &libraryId,
    const std::vector<LibraryPatchRow> &patches) {
  if (libraryId.empty())
    return PatchReplaceResult::error;

  std::set<std::string> retainedIds;
  for (const auto &patch : patches) {
    if (patch.id.empty() || patch.libraryId != libraryId ||
        !retainedIds.insert(patch.id).second)
      return PatchReplaceResult::error;
  }

  std::lock_guard<std::mutex> lock(databaseMutex_);
  std::vector<std::string> removedIds;
  {
    Statement existing(
        database_, "SELECT id FROM library_patches WHERE library_id = ?");
    if (!existing)
      return PatchReplaceResult::error;
    bindText(existing.get(), 1, libraryId);
    while (sqlite3_step(existing.get()) == SQLITE_ROW) {
      const auto id = columnText(existing.get(), 0);
      if (retainedIds.count(id) == 0)
        removedIds.push_back(id);
    }
  }

  {
    Statement owner(database_,
                    "SELECT library_id FROM library_patches WHERE id = ?");
    Statement usage(database_,
                    "SELECT COUNT(*) FROM layers WHERE patch_id = ?");
    if (!owner || !usage)
      return PatchReplaceResult::error;

    for (const auto &patch : patches) {
      sqlite3_reset(owner.get());
      sqlite3_clear_bindings(owner.get());
      bindText(owner.get(), 1, patch.id);
      if (sqlite3_step(owner.get()) == SQLITE_ROW &&
          columnText(owner.get(), 0) != libraryId)
        return PatchReplaceResult::error;
    }
    for (const auto &id : removedIds) {
      sqlite3_reset(usage.get());
      sqlite3_clear_bindings(usage.get());
      bindText(usage.get(), 1, id);
      if (sqlite3_step(usage.get()) != SQLITE_ROW)
        return PatchReplaceResult::error;
      if (sqlite3_column_int(usage.get(), 0) > 0)
        return PatchReplaceResult::referenced;
    }
  }

  if (!execute(database_, "BEGIN IMMEDIATE"))
    return PatchReplaceResult::error;

  Statement upsert(database_, R"(
    INSERT INTO library_patches
      (id, library_id, position, name, instrument_entity_id, family,
       character, plugin_uid, plugin_state, expression_map_id)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      revision = CASE WHEN
        library_patches.name <> excluded.name OR
        library_patches.instrument_entity_id <>
          excluded.instrument_entity_id OR
        library_patches.family <> excluded.family OR
        library_patches.character <> excluded.character OR
        library_patches.plugin_uid <> excluded.plugin_uid OR
        COALESCE(library_patches.plugin_state, X'') <>
          COALESCE(excluded.plugin_state, X'') OR
        library_patches.expression_map_id <> excluded.expression_map_id
        THEN library_patches.revision + 1
        ELSE library_patches.revision END,
      position = excluded.position,
      name = excluded.name,
      instrument_entity_id = excluded.instrument_entity_id,
      family = excluded.family,
      character = excluded.character,
      plugin_uid = excluded.plugin_uid,
      plugin_state = excluded.plugin_state,
      expression_map_id = excluded.expression_map_id
  )");
  Statement remove(database_, "DELETE FROM library_patches WHERE id = ?");
  bool succeeded = upsert && remove;

  for (const auto &patch : patches) {
    if (!succeeded)
      break;
    sqlite3_reset(upsert.get());
    sqlite3_clear_bindings(upsert.get());
    bindText(upsert.get(), 1, patch.id);
    bindText(upsert.get(), 2, patch.libraryId);
    sqlite3_bind_int(upsert.get(), 3, patch.position);
    bindText(upsert.get(), 4, patch.name);
    bindText(upsert.get(), 5, patch.instrumentEntityId);
    bindText(upsert.get(), 6, patch.family);
    bindText(upsert.get(), 7, patch.character);
    sqlite3_bind_int(upsert.get(), 8, patch.pluginUid);
    bindBlob(upsert.get(), 9, patch.pluginState);
    bindText(upsert.get(), 10, patch.expressionMapId);
    succeeded = sqlite3_step(upsert.get()) == SQLITE_DONE;
  }

  for (const auto &id : removedIds) {
    if (!succeeded)
      break;
    sqlite3_reset(remove.get());
    sqlite3_clear_bindings(remove.get());
    bindText(remove.get(), 1, id);
    succeeded = sqlite3_step(remove.get()) == SQLITE_DONE;
  }

  if (!succeeded || !execute(database_, "COMMIT")) {
    execute(database_, "ROLLBACK");
    return PatchReplaceResult::error;
  }
  return PatchReplaceResult::replaced;
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

bool LibraryRoutingRepository::changeChairRole(const std::string &chairId,
                                               DoricoRole role) {
  if (chairId.empty())
    return false;

  std::lock_guard<std::mutex> lock(databaseMutex_);
  if (!execute(database_, "BEGIN IMMEDIATE"))
    return false;

  Statement find(database_, R"(
    SELECT instrument_entity_id, dorico_role, ordinal
    FROM chairs WHERE id = ?
  )");
  if (!find) {
    execute(database_, "ROLLBACK");
    return false;
  }
  bindText(find.get(), 1, chairId);
  if (sqlite3_step(find.get()) != SQLITE_ROW) {
    execute(database_, "ROLLBACK");
    return false;
  }

  const auto entityId = columnText(find.get(), 0);
  const int currentRole = sqlite3_column_int(find.get(), 1);
  int ordinal = sqlite3_column_int(find.get(), 2);
  const int requestedRole = role == DoricoRole::solo ? 0 : 1;
  if (currentRole == requestedRole)
    return execute(database_, "COMMIT");

  Statement conflict(database_, R"(
    SELECT 1 FROM chairs
    WHERE instrument_entity_id = ? AND dorico_role = ? AND ordinal = ?
      AND id <> ?
    LIMIT 1
  )");
  Statement nextOrdinal(database_, R"(
    SELECT COALESCE(MAX(ordinal), 0) + 1 FROM chairs
    WHERE instrument_entity_id = ? AND dorico_role = ?
  )");
  if (!conflict || !nextOrdinal) {
    execute(database_, "ROLLBACK");
    return false;
  }

  bindText(conflict.get(), 1, entityId);
  sqlite3_bind_int(conflict.get(), 2, requestedRole);
  sqlite3_bind_int(conflict.get(), 3, ordinal);
  bindText(conflict.get(), 4, chairId);
  if (sqlite3_step(conflict.get()) == SQLITE_ROW) {
    bindText(nextOrdinal.get(), 1, entityId);
    sqlite3_bind_int(nextOrdinal.get(), 2, requestedRole);
    if (sqlite3_step(nextOrdinal.get()) != SQLITE_ROW) {
      execute(database_, "ROLLBACK");
      return false;
    }
    ordinal = sqlite3_column_int(nextOrdinal.get(), 0);
  }

  Statement update(database_, R"(
    UPDATE chairs SET dorico_role = ?, ordinal = ? WHERE id = ?
  )");
  if (!update) {
    execute(database_, "ROLLBACK");
    return false;
  }
  sqlite3_bind_int(update.get(), 1, requestedRole);
  sqlite3_bind_int(update.get(), 2, ordinal);
  bindText(update.get(), 3, chairId);
  if (sqlite3_step(update.get()) != SQLITE_DONE) {
    execute(database_, "ROLLBACK");
    return false;
  }

  if (!execute(database_, "COMMIT")) {
    execute(database_, "ROLLBACK");
    return false;
  }
  return true;
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
      (id, chair_id, patch_id, patch_name, library_id, library_name, position,
       active, muted, soloed, gain_db, plugin_uid, plugin_state,
       expression_map_id, source_patch_revision, plugin_state_edited)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    ON CONFLICT(id) DO UPDATE SET
      chair_id = excluded.chair_id,
      patch_id = excluded.patch_id,
      patch_name = excluded.patch_name,
      library_id = excluded.library_id,
      library_name = excluded.library_name,
      position = excluded.position,
      active = excluded.active,
      muted = excluded.muted,
      soloed = excluded.soloed,
      gain_db = excluded.gain_db,
      plugin_uid = excluded.plugin_uid,
      plugin_state = excluded.plugin_state,
      expression_map_id = excluded.expression_map_id,
      source_patch_revision = excluded.source_patch_revision,
      plugin_state_edited = excluded.plugin_state_edited
  )");
  if (!statement)
    return false;
  bindText(statement.get(), 1, layer.id);
  bindText(statement.get(), 2, layer.chairId);
  bindText(statement.get(), 3, layer.patchId);
  bindText(statement.get(), 4, layer.patchName);
  bindText(statement.get(), 5, layer.libraryId);
  bindText(statement.get(), 6, layer.libraryName);
  sqlite3_bind_int(statement.get(), 7, layer.position);
  sqlite3_bind_int(statement.get(), 8, layer.active ? 1 : 0);
  sqlite3_bind_int(statement.get(), 9, layer.muted ? 1 : 0);
  sqlite3_bind_int(statement.get(), 10, layer.soloed ? 1 : 0);
  sqlite3_bind_double(statement.get(), 11, layer.gainDb);
  sqlite3_bind_int(statement.get(), 12, layer.pluginUid);
  bindBlob(statement.get(), 13, layer.pluginState);
  bindText(statement.get(), 14, layer.expressionMapId);
  sqlite3_bind_int(statement.get(), 15, layer.sourcePatchRevision);
  sqlite3_bind_int(statement.get(), 16, layer.pluginStateEdited ? 1 : 0);
  return sqlite3_step(statement.get()) == SQLITE_DONE;
}

bool LibraryRoutingRepository::createLayerFromPatch(
    const std::string &layerId, const std::string &chairId,
    const std::string &patchId, int position) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(database_, R"(
    INSERT INTO layers
      (id, chair_id, patch_id, patch_name, library_id, library_name, position,
       plugin_uid, plugin_state, expression_map_id, source_patch_revision,
       plugin_state_edited)
    SELECT ?, ?, p.id, p.name, p.library_id, COALESCE(l.name, ''), ?,
           p.plugin_uid, p.plugin_state, p.expression_map_id, p.revision, 0
    FROM library_patches p
    LEFT JOIN libraries l ON l.id = p.library_id
    WHERE p.id = ?
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

bool LibraryRoutingRepository::markLayerPluginStateEdited(
    const std::string &layerId) {
  std::lock_guard<std::mutex> lock(databaseMutex_);
  Statement statement(
      database_,
      "UPDATE layers SET plugin_state_edited = 1 WHERE id = ?");
  if (!statement)
    return false;
  bindText(statement.get(), 1, layerId);
  return sqlite3_step(statement.get()) == SQLITE_DONE &&
         sqlite3_changes(database_) == 1;
}

bool LibraryRoutingRepository::replaceTopology(
    const std::vector<ChairRow> &chairs,
    const std::vector<LayerRow> &layers) {
  std::lock_guard<std::mutex> lock(databaseMutex_);

  std::set<std::string> chairIds;
  std::set<std::string> chairDestinations;
  std::set<int> flatIndices;
  for (const auto &chair : chairs) {
    const auto destination = chair.instrumentEntityId + "\n" +
                             std::to_string(static_cast<int>(chair.role)) +
                             "\n" + std::to_string(chair.ordinal);
    if (chair.id.empty() || chair.instrumentEntityId.empty() ||
        chair.flatIndex <= 0 || !chairIds.insert(chair.id).second ||
        !chairDestinations.insert(destination).second ||
        !flatIndices.insert(chair.flatIndex).second)
      return false;
  }
  std::set<std::string> layerIds;
  for (const auto &layer : layers) {
    if (layer.id.empty() || chairIds.count(layer.chairId) == 0 ||
        !layerIds.insert(layer.id).second)
      return false;
  }

  if (!execute(database_, "BEGIN IMMEDIATE"))
    return false;
  bool succeeded = execute(database_, "DELETE FROM layers") &&
                   execute(database_, "DELETE FROM chairs");

  Statement removeGraveyard(
      database_, "DELETE FROM chair_midi_graveyard WHERE flat_index = ?");
  Statement insertChair(database_, R"(
    INSERT INTO chairs
      (id, instrument_entity_id, name, family, dorico_role, ordinal,
       display_order, flat_index)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  )");
  Statement insertLayer(database_, R"(
    INSERT INTO layers
      (id, chair_id, patch_id, patch_name, library_id, library_name, position,
       active, muted, soloed, gain_db, plugin_uid, plugin_state,
       expression_map_id, source_patch_revision, plugin_state_edited)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
  )");
  succeeded = succeeded && removeGraveyard && insertChair && insertLayer;

  for (const auto &chair : chairs) {
    if (!succeeded)
      break;
    sqlite3_reset(removeGraveyard.get());
    sqlite3_clear_bindings(removeGraveyard.get());
    sqlite3_bind_int(removeGraveyard.get(), 1, chair.flatIndex);
    succeeded = sqlite3_step(removeGraveyard.get()) == SQLITE_DONE;

    sqlite3_reset(insertChair.get());
    sqlite3_clear_bindings(insertChair.get());
    bindText(insertChair.get(), 1, chair.id);
    bindText(insertChair.get(), 2, chair.instrumentEntityId);
    bindText(insertChair.get(), 3, chair.name);
    bindText(insertChair.get(), 4, chair.family);
    sqlite3_bind_int(insertChair.get(), 5,
                     chair.role == DoricoRole::solo ? 0 : 1);
    sqlite3_bind_int(insertChair.get(), 6, chair.ordinal);
    sqlite3_bind_int(insertChair.get(), 7, chair.displayOrder);
    sqlite3_bind_int(insertChair.get(), 8, chair.flatIndex);
    succeeded = succeeded &&
                sqlite3_step(insertChair.get()) == SQLITE_DONE;
  }

  for (const auto &layer : layers) {
    if (!succeeded)
      break;
    sqlite3_reset(insertLayer.get());
    sqlite3_clear_bindings(insertLayer.get());
    bindText(insertLayer.get(), 1, layer.id);
    bindText(insertLayer.get(), 2, layer.chairId);
    bindText(insertLayer.get(), 3, layer.patchId);
    bindText(insertLayer.get(), 4, layer.patchName);
    bindText(insertLayer.get(), 5, layer.libraryId);
    bindText(insertLayer.get(), 6, layer.libraryName);
    sqlite3_bind_int(insertLayer.get(), 7, layer.position);
    sqlite3_bind_int(insertLayer.get(), 8, layer.active ? 1 : 0);
    sqlite3_bind_int(insertLayer.get(), 9, layer.muted ? 1 : 0);
    sqlite3_bind_int(insertLayer.get(), 10, layer.soloed ? 1 : 0);
    sqlite3_bind_double(insertLayer.get(), 11, layer.gainDb);
    sqlite3_bind_int(insertLayer.get(), 12, layer.pluginUid);
    bindBlob(insertLayer.get(), 13, layer.pluginState);
    bindText(insertLayer.get(), 14, layer.expressionMapId);
    sqlite3_bind_int(insertLayer.get(), 15, layer.sourcePatchRevision);
    sqlite3_bind_int(insertLayer.get(), 16,
                     layer.pluginStateEdited ? 1 : 0);
    succeeded = sqlite3_step(insertLayer.get()) == SQLITE_DONE;
  }

  if (!succeeded || !execute(database_, "COMMIT")) {
    execute(database_, "ROLLBACK");
    return false;
  }
  return true;
}

} // namespace fiddle
