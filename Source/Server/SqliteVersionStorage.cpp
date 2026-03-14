#include "SqliteVersionStorage.h"
#include <iostream>

namespace fiddle::versioning {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string joinStrings(const std::vector<std::string> &v) {
  std::string result;
  for (size_t i = 0; i < v.size(); ++i) {
    result += v[i];
    if (i + 1 < v.size())
      result += ",";
  }
  return result;
}

static std::vector<std::string> splitStrings(const std::string &str) {
  std::vector<std::string> result;
  if (str.empty())
    return result;
  size_t start = 0;
  while (start < str.length()) {
    size_t end = str.find(',', start);
    if (end == std::string::npos) {
      result.push_back(str.substr(start));
      break;
    }
    result.push_back(str.substr(start, end - start));
    start = end + 1;
  }
  return result;
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

SqliteVersionStorage::SqliteVersionStorage(sqlite3 *db, std::mutex &dbMutex)
    : db_(db), dbMutex_(dbMutex) {
  prepareStatements();
}

SqliteVersionStorage::~SqliteVersionStorage() { finalizeStatements(); }

// ---------------------------------------------------------------------------
// Statements Initialization
// ---------------------------------------------------------------------------

void SqliteVersionStorage::prepareStatements() {
  // Lock is already held by FiddleDatabase during construction

  auto prep = [&](const char *sql, sqlite3_stmt **stmt) {
    if (sqlite3_prepare_v2(db_, sql, -1, stmt, nullptr) != SQLITE_OK) {
      std::cerr << "[SqliteVersionStorage] Failed to prepare: " << sql
                << "\n  Error: " << sqlite3_errmsg(db_) << std::endl;
    }
  };

  // StripBlobs — library_id replaces uuid
  prep("INSERT OR REPLACE INTO strip_blobs (hash, library_id, library, family, "
       "is_solo, input_port, input_channel, plugin_uid, gain_db, "
       "expression_map, plugin_state) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
       &stmtPutStripBlob_);
  prep("SELECT library_id, library, family, is_solo, input_port, "
       "input_channel, plugin_uid, gain_db, expression_map, plugin_state "
       "FROM strip_blobs WHERE hash = ?",
       &stmtGetStripBlob_);
  prep("SELECT 1 FROM strip_blobs WHERE hash = ?", &stmtHasStripBlob_);

  // FiddleState
  prep("INSERT OR REPLACE INTO fiddle_states (hash, master_gain, strip_hashes) "
       "VALUES (?, ?, ?)",
       &stmtPutFiddleState_);
  prep("SELECT master_gain, strip_hashes FROM fiddle_states WHERE hash = ?",
       &stmtGetFiddleState_);
  prep("SELECT 1 FROM fiddle_states WHERE hash = ?", &stmtHasFiddleState_);

  // Versions — id UUID PK, parent_id / merge_parent_id
  prep("INSERT OR REPLACE INTO versions (id, state_hash, branch_id, "
       "parent_id, merge_parent_id) VALUES (?, ?, ?, ?, ?)",
       &stmtPutVersion_);
  prep("SELECT state_hash, branch_id, parent_id, merge_parent_id, created_at "
       "FROM versions WHERE id = ?",
       &stmtGetVersion_);
  prep("SELECT 1 FROM versions WHERE id = ?", &stmtHasVersion_);
  prep("DELETE FROM versions WHERE id = ?", &stmtRemoveVersion_);
  prep("UPDATE versions SET parent_id = ? WHERE id = ?",
       &stmtUpdateVersionParent_);
  prep("SELECT id FROM versions WHERE parent_id = ? OR merge_parent_id = ?",
       &stmtGetChildVersions_);
  prep("SELECT id, state_hash, branch_id, parent_id, merge_parent_id, "
       "created_at FROM versions ORDER BY created_at DESC",
       &stmtListVersions_);

  // Branches — head_id instead of head_hash
  prep("INSERT OR REPLACE INTO branches (id, name, head_id) VALUES (?, ?, ?)",
       &stmtPutBranch_);
  prep("SELECT name, head_id FROM branches WHERE id = ?", &stmtGetBranch_);
  prep("SELECT id FROM branches WHERE name = ?", &stmtFindBranchByName_);
  prep("SELECT id, name, head_id FROM branches ORDER BY name ASC",
       &stmtListBranches_);
  prep("UPDATE branches SET head_id = ? WHERE id = ?", &stmtUpdateBranchHead_);
  prep("DELETE FROM branches WHERE id = ?", &stmtDeleteBranch_);
  prep("SELECT 1 FROM branches WHERE name = ?", &stmtBranchNameExists_);

  // Libraries
  prep("INSERT OR REPLACE INTO libraries (id, name) VALUES (?, ?)",
       &stmtPutLibrary_);
  prep("SELECT name FROM libraries WHERE id = ?", &stmtGetLibraryName_);
  prep("SELECT id, name FROM libraries ORDER BY name ASC", &stmtListLibraries_);
  prep("SELECT 1 FROM libraries WHERE name = ?", &stmtLibraryNameExists_);
  prep("UPDATE libraries SET name = ? WHERE id = ?", &stmtRenameLibrary_);
}

void SqliteVersionStorage::finalizeStatements() {
  sqlite3_finalize(stmtPutStripBlob_);
  sqlite3_finalize(stmtGetStripBlob_);
  sqlite3_finalize(stmtHasStripBlob_);

  sqlite3_finalize(stmtPutFiddleState_);
  sqlite3_finalize(stmtGetFiddleState_);
  sqlite3_finalize(stmtHasFiddleState_);

  sqlite3_finalize(stmtPutVersion_);
  sqlite3_finalize(stmtGetVersion_);
  sqlite3_finalize(stmtHasVersion_);
  sqlite3_finalize(stmtRemoveVersion_);
  sqlite3_finalize(stmtUpdateVersionParent_);
  sqlite3_finalize(stmtGetChildVersions_);
  sqlite3_finalize(stmtListVersions_);

  sqlite3_finalize(stmtPutBranch_);
  sqlite3_finalize(stmtGetBranch_);
  sqlite3_finalize(stmtFindBranchByName_);
  sqlite3_finalize(stmtListBranches_);
  sqlite3_finalize(stmtUpdateBranchHead_);
  sqlite3_finalize(stmtDeleteBranch_);
  sqlite3_finalize(stmtBranchNameExists_);

  sqlite3_finalize(stmtPutLibrary_);
  sqlite3_finalize(stmtGetLibraryName_);
  sqlite3_finalize(stmtListLibraries_);
  sqlite3_finalize(stmtLibraryNameExists_);
  sqlite3_finalize(stmtRenameLibrary_);
}

// ---------------------------------------------------------------------------
// Strip Blobs
// ---------------------------------------------------------------------------

void SqliteVersionStorage::putStripBlob(const Hash &hash,
                                        const StripBlob &blob) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtPutStripBlob_);
  sqlite3_clear_bindings(stmtPutStripBlob_);

  sqlite3_bind_text(stmtPutStripBlob_, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutStripBlob_, 2, blob.libraryId.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutStripBlob_, 3, blob.library.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutStripBlob_, 4, blob.family.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_int(stmtPutStripBlob_, 5, blob.isSolo ? 1 : 0);
  sqlite3_bind_int(stmtPutStripBlob_, 6, blob.inputPort);
  sqlite3_bind_int(stmtPutStripBlob_, 7, blob.inputChannel);
  sqlite3_bind_int(stmtPutStripBlob_, 8, blob.pluginUid);
  sqlite3_bind_double(stmtPutStripBlob_, 9, blob.gainDb);
  sqlite3_bind_text(stmtPutStripBlob_, 10, blob.expressionMapEntityId.c_str(),
                    -1, SQLITE_TRANSIENT);

  if (blob.pluginState.empty()) {
    sqlite3_bind_null(stmtPutStripBlob_, 11);
  } else {
    sqlite3_bind_blob(stmtPutStripBlob_, 11, blob.pluginState.data(),
                      (int)blob.pluginState.size(), SQLITE_TRANSIENT);
  }

  sqlite3_step(stmtPutStripBlob_);
}

std::optional<StripBlob>
SqliteVersionStorage::getStripBlob(const Hash &hash) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtGetStripBlob_);
  sqlite3_clear_bindings(stmtGetStripBlob_);

  sqlite3_bind_text(stmtGetStripBlob_, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtGetStripBlob_) == SQLITE_ROW) {
    StripBlob blob;
    blob.libraryId = (const char *)sqlite3_column_text(stmtGetStripBlob_, 0);
    blob.library = (const char *)sqlite3_column_text(stmtGetStripBlob_, 1);
    blob.family = (const char *)sqlite3_column_text(stmtGetStripBlob_, 2);
    blob.isSolo = sqlite3_column_int(stmtGetStripBlob_, 3) != 0;
    blob.inputPort = sqlite3_column_int(stmtGetStripBlob_, 4);
    blob.inputChannel = sqlite3_column_int(stmtGetStripBlob_, 5);
    blob.pluginUid = sqlite3_column_int(stmtGetStripBlob_, 6);
    blob.gainDb = (float)sqlite3_column_double(stmtGetStripBlob_, 7);
    blob.expressionMapEntityId =
        (const char *)sqlite3_column_text(stmtGetStripBlob_, 8);

    if (sqlite3_column_type(stmtGetStripBlob_, 9) != SQLITE_NULL) {
      int size = sqlite3_column_bytes(stmtGetStripBlob_, 9);
      const uint8_t *data =
          (const uint8_t *)sqlite3_column_blob(stmtGetStripBlob_, 9);
      if (size > 0 && data) {
        blob.pluginState.assign(data, data + size);
      }
    }
    return blob;
  }
  return std::nullopt;
}

bool SqliteVersionStorage::hasStripBlob(const Hash &hash) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtHasStripBlob_);
  sqlite3_clear_bindings(stmtHasStripBlob_);
  sqlite3_bind_text(stmtHasStripBlob_, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  return sqlite3_step(stmtHasStripBlob_) == SQLITE_ROW;
}

// ---------------------------------------------------------------------------
// Fiddle States
// ---------------------------------------------------------------------------

void SqliteVersionStorage::putFiddleState(const Hash &hash,
                                          const FiddleState &state) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtPutFiddleState_);
  sqlite3_clear_bindings(stmtPutFiddleState_);

  std::string hashesStr = joinStrings(state.stripHashes);

  sqlite3_bind_text(stmtPutFiddleState_, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmtPutFiddleState_, 2, state.globalState.masterGainDb);
  sqlite3_bind_text(stmtPutFiddleState_, 3, hashesStr.c_str(), -1,
                    SQLITE_TRANSIENT);

  sqlite3_step(stmtPutFiddleState_);
}

std::optional<FiddleState>
SqliteVersionStorage::getFiddleState(const Hash &hash) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtGetFiddleState_);
  sqlite3_clear_bindings(stmtGetFiddleState_);

  sqlite3_bind_text(stmtGetFiddleState_, 1, hash.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtGetFiddleState_) == SQLITE_ROW) {
    FiddleState state;
    state.globalState.masterGainDb =
        (float)sqlite3_column_double(stmtGetFiddleState_, 0);

    const char *hashesStr =
        (const char *)sqlite3_column_text(stmtGetFiddleState_, 1);
    if (hashesStr) {
      state.stripHashes = splitStrings(std::string(hashesStr));
    }
    return state;
  }
  return std::nullopt;
}

bool SqliteVersionStorage::hasFiddleState(const Hash &hash) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtHasFiddleState_);
  sqlite3_clear_bindings(stmtHasFiddleState_);
  sqlite3_bind_text(stmtHasFiddleState_, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
  return sqlite3_step(stmtHasFiddleState_) == SQLITE_ROW;
}

// ---------------------------------------------------------------------------
// Versions
// ---------------------------------------------------------------------------

void SqliteVersionStorage::putVersion(const VersionId &id, const Version &ver) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtPutVersion_);
  sqlite3_clear_bindings(stmtPutVersion_);

  sqlite3_bind_text(stmtPutVersion_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutVersion_, 2, ver.stateHash.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutVersion_, 3, ver.branchId.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutVersion_, 4, ver.parentId.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutVersion_, 5, ver.mergeParentId.c_str(), -1,
                    SQLITE_TRANSIENT);

  sqlite3_step(stmtPutVersion_);
}

std::optional<Version>
SqliteVersionStorage::getVersion(const VersionId &id) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtGetVersion_);
  sqlite3_clear_bindings(stmtGetVersion_);

  sqlite3_bind_text(stmtGetVersion_, 1, id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtGetVersion_) == SQLITE_ROW) {
    Version ver;
    ver.id = id;
    ver.stateHash = (const char *)sqlite3_column_text(stmtGetVersion_, 0);
    ver.branchId = (const char *)sqlite3_column_text(stmtGetVersion_, 1);

    auto *pId = (const char *)sqlite3_column_text(stmtGetVersion_, 2);
    if (pId)
      ver.parentId = pId;

    auto *mpId = (const char *)sqlite3_column_text(stmtGetVersion_, 3);
    if (mpId)
      ver.mergeParentId = mpId;

    auto *ts = (const char *)sqlite3_column_text(stmtGetVersion_, 4);
    if (ts)
      ver.createdAt = ts;

    return ver;
  }
  return std::nullopt;
}

bool SqliteVersionStorage::hasVersion(const VersionId &id) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtHasVersion_);
  sqlite3_clear_bindings(stmtHasVersion_);
  sqlite3_bind_text(stmtHasVersion_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  return sqlite3_step(stmtHasVersion_) == SQLITE_ROW;
}

void SqliteVersionStorage::removeVersion(const VersionId &id) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtRemoveVersion_);
  sqlite3_clear_bindings(stmtRemoveVersion_);
  sqlite3_bind_text(stmtRemoveVersion_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmtRemoveVersion_);
}

void SqliteVersionStorage::updateVersionParent(const VersionId &id,
                                               const VersionId &newParentId) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtUpdateVersionParent_);
  sqlite3_clear_bindings(stmtUpdateVersionParent_);
  sqlite3_bind_text(stmtUpdateVersionParent_, 1, newParentId.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtUpdateVersionParent_, 2, id.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_step(stmtUpdateVersionParent_);
}

std::vector<VersionId>
SqliteVersionStorage::getChildVersionIds(const VersionId &parentId) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtGetChildVersions_);
  sqlite3_clear_bindings(stmtGetChildVersions_);

  sqlite3_bind_text(stmtGetChildVersions_, 1, parentId.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtGetChildVersions_, 2, parentId.c_str(), -1,
                    SQLITE_TRANSIENT);

  std::vector<VersionId> children;
  while (sqlite3_step(stmtGetChildVersions_) == SQLITE_ROW) {
    children.push_back(
        (const char *)sqlite3_column_text(stmtGetChildVersions_, 0));
  }
  return children;
}

std::vector<std::pair<VersionId, Version>>
SqliteVersionStorage::listAllVersions() const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtListVersions_);
  sqlite3_clear_bindings(stmtListVersions_);

  std::vector<std::pair<VersionId, Version>> versions;
  while (sqlite3_step(stmtListVersions_) == SQLITE_ROW) {
    VersionId verId = (const char *)sqlite3_column_text(stmtListVersions_, 0);
    Version ver;
    ver.id = verId;
    ver.stateHash = (const char *)sqlite3_column_text(stmtListVersions_, 1);

    auto *bId = (const char *)sqlite3_column_text(stmtListVersions_, 2);
    if (bId)
      ver.branchId = bId;

    auto *pId = (const char *)sqlite3_column_text(stmtListVersions_, 3);
    if (pId)
      ver.parentId = pId;

    auto *mpId = (const char *)sqlite3_column_text(stmtListVersions_, 4);
    if (mpId)
      ver.mergeParentId = mpId;

    auto *ts = (const char *)sqlite3_column_text(stmtListVersions_, 5);
    if (ts)
      ver.createdAt = ts;

    versions.emplace_back(verId, ver);
  }
  return versions;
}

// ---------------------------------------------------------------------------
// Branches
// ---------------------------------------------------------------------------

void SqliteVersionStorage::putBranch(const BranchId &id,
                                     const std::string &name,
                                     const VersionId &headId) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtPutBranch_);
  sqlite3_clear_bindings(stmtPutBranch_);

  sqlite3_bind_text(stmtPutBranch_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutBranch_, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutBranch_, 3, headId.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmtPutBranch_);
}

std::optional<std::pair<std::string, VersionId>>
SqliteVersionStorage::getBranch(const BranchId &id) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtGetBranch_);
  sqlite3_clear_bindings(stmtGetBranch_);

  sqlite3_bind_text(stmtGetBranch_, 1, id.c_str(), -1, SQLITE_TRANSIENT);

  if (sqlite3_step(stmtGetBranch_) == SQLITE_ROW) {
    return std::pair<std::string, VersionId>{
        (const char *)sqlite3_column_text(stmtGetBranch_, 0),
        (const char *)sqlite3_column_text(stmtGetBranch_, 1)};
  }
  return std::nullopt;
}

std::optional<BranchId>
SqliteVersionStorage::findBranchByName(const std::string &name) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtFindBranchByName_);
  sqlite3_clear_bindings(stmtFindBranchByName_);

  sqlite3_bind_text(stmtFindBranchByName_, 1, name.c_str(), -1,
                    SQLITE_TRANSIENT);

  if (sqlite3_step(stmtFindBranchByName_) == SQLITE_ROW) {
    return std::string(
        (const char *)sqlite3_column_text(stmtFindBranchByName_, 0));
  }
  return std::nullopt;
}

std::vector<std::tuple<BranchId, std::string, VersionId>>
SqliteVersionStorage::listBranches() const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtListBranches_);
  sqlite3_clear_bindings(stmtListBranches_);

  std::vector<std::tuple<BranchId, std::string, VersionId>> result;
  while (sqlite3_step(stmtListBranches_) == SQLITE_ROW) {
    result.emplace_back(
        (const char *)sqlite3_column_text(stmtListBranches_, 0),
        (const char *)sqlite3_column_text(stmtListBranches_, 1),
        (const char *)sqlite3_column_text(stmtListBranches_, 2));
  }
  return result;
}

void SqliteVersionStorage::updateBranchHead(const BranchId &id,
                                            const VersionId &headId) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtUpdateBranchHead_);
  sqlite3_clear_bindings(stmtUpdateBranchHead_);

  sqlite3_bind_text(stmtUpdateBranchHead_, 1, headId.c_str(), -1,
                    SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtUpdateBranchHead_, 2, id.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmtUpdateBranchHead_);
}

void SqliteVersionStorage::deleteBranch(const BranchId &id) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtDeleteBranch_);
  sqlite3_clear_bindings(stmtDeleteBranch_);
  sqlite3_bind_text(stmtDeleteBranch_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmtDeleteBranch_);
}

bool SqliteVersionStorage::branchNameExists(const std::string &name) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtBranchNameExists_);
  sqlite3_clear_bindings(stmtBranchNameExists_);
  sqlite3_bind_text(stmtBranchNameExists_, 1, name.c_str(), -1,
                    SQLITE_TRANSIENT);
  return sqlite3_step(stmtBranchNameExists_) == SQLITE_ROW;
}

// ---------------------------------------------------------------------------
// Libraries
// ---------------------------------------------------------------------------

void SqliteVersionStorage::putLibrary(const LibraryId &id,
                                      const std::string &name) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtPutLibrary_);
  sqlite3_clear_bindings(stmtPutLibrary_);
  sqlite3_bind_text(stmtPutLibrary_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtPutLibrary_, 2, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmtPutLibrary_);
}

std::optional<std::string>
SqliteVersionStorage::getLibraryName(const LibraryId &id) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtGetLibraryName_);
  sqlite3_clear_bindings(stmtGetLibraryName_);
  sqlite3_bind_text(stmtGetLibraryName_, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(stmtGetLibraryName_) == SQLITE_ROW) {
    return std::string(
        (const char *)sqlite3_column_text(stmtGetLibraryName_, 0));
  }
  return std::nullopt;
}

std::vector<std::pair<LibraryId, std::string>>
SqliteVersionStorage::listLibraries() const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtListLibraries_);
  sqlite3_clear_bindings(stmtListLibraries_);
  std::vector<std::pair<LibraryId, std::string>> result;
  while (sqlite3_step(stmtListLibraries_) == SQLITE_ROW) {
    result.emplace_back(
        (const char *)sqlite3_column_text(stmtListLibraries_, 0),
        (const char *)sqlite3_column_text(stmtListLibraries_, 1));
  }
  return result;
}

bool SqliteVersionStorage::libraryNameExists(const std::string &name) const {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtLibraryNameExists_);
  sqlite3_clear_bindings(stmtLibraryNameExists_);
  sqlite3_bind_text(stmtLibraryNameExists_, 1, name.c_str(), -1,
                    SQLITE_TRANSIENT);
  return sqlite3_step(stmtLibraryNameExists_) == SQLITE_ROW;
}

bool SqliteVersionStorage::renameLibrary(const LibraryId &id,
                                         const std::string &name) {
  std::lock_guard<std::mutex> lock(dbMutex_);
  sqlite3_reset(stmtRenameLibrary_);
  sqlite3_clear_bindings(stmtRenameLibrary_);
  sqlite3_bind_text(stmtRenameLibrary_, 1, name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmtRenameLibrary_, 2, id.c_str(), -1, SQLITE_TRANSIENT);
  int rc = sqlite3_step(stmtRenameLibrary_);
  return rc == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

} // namespace fiddle::versioning
