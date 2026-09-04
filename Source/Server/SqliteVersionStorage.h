#pragma once

#include "VersionStoreInterface.h"
#include <mutex>
#include <sqlite3.h>
#include <string>
#include <vector>

namespace fiddle::versioning {

struct GarbageCollectionResult {
  int fiddleStatesRemoved = 0;
  int stripBlobsRemoved = 0;
  bool compacted = false;
  std::string error;

  bool succeeded() const { return error.empty(); }
};

/**
 * SQLite-backed implementation of the version storage interface.
 * Requires an external sqlite3 handle and mutex (usually from FiddleDatabase).
 *
 * Schema changes (stability redesign):
 *   strip_blobs: uuid column → library_id (strip identity is
 * (port,channel,library_id)) versions:    hash PK → id UUID, parent_hash →
 * parent_id, merge_parent_hash → merge_parent_id branches:    head_hash →
 * head_id libraries:   new table (id, name)
 */
class SqliteVersionStorage : public IVersionStorage {
public:
  SqliteVersionStorage(sqlite3 *db, std::mutex &dbMutex);
  ~SqliteVersionStorage() override;

  // ── StripBlobs ──────────────────────────────────────────────────────────
  void putStripBlob(const Hash &hash, const StripBlob &blob) override;
  std::optional<StripBlob> getStripBlob(const Hash &hash) const override;
  bool hasStripBlob(const Hash &hash) const override;

  // ── FiddleState ─────────────────────────────────────────────────────────
  void putFiddleState(const Hash &hash, const FiddleState &state) override;
  std::optional<FiddleState> getFiddleState(const Hash &hash) const override;
  bool hasFiddleState(const Hash &hash) const override;

  // ── Versions ────────────────────────────────────────────────────────────
  void putVersion(const VersionId &id, const Version &ver) override;
  std::optional<Version> getVersion(const VersionId &id) const override;
  bool hasVersion(const VersionId &id) const override;
  void removeVersion(const VersionId &id) override;
  void updateVersionParent(const VersionId &id,
                           const VersionId &newParentId) override;
  std::vector<VersionId>
  getChildVersionIds(const VersionId &parentId) const override;
  std::vector<std::pair<VersionId, Version>> listAllVersions() const override;

  // ── Branches ────────────────────────────────────────────────────────────
  void putBranch(const BranchId &id, const std::string &name,
                 const VersionId &headId) override;
  std::optional<std::pair<std::string, VersionId>>
  getBranch(const BranchId &id) const override;
  std::optional<BranchId>
  findBranchByName(const std::string &name) const override;
  std::vector<std::tuple<BranchId, std::string, VersionId>>
  listBranches() const override;
  void updateBranchHead(const BranchId &id, const VersionId &headId) override;
  void deleteBranch(const BranchId &id) override;
  bool branchNameExists(const std::string &name) const override;

  // ── Libraries ───────────────────────────────────────────────────────────
  void putLibrary(const LibraryId &id, const std::string &name) override;
  std::optional<std::string> getLibraryName(const LibraryId &id) const override;
  std::vector<std::pair<LibraryId, std::string>> listLibraries() const override;
  bool libraryNameExists(const std::string &name) const override;
  bool renameLibrary(const LibraryId &id, const std::string &name) override;

  /// Remove content-addressed objects not referenced by any version. Versions,
  /// branches, and every object reachable from a version are retained.
  /// If compact is true, VACUUM is run only when rows were removed.
  GarbageCollectionResult
  garbageCollectUnreferencedObjects(bool compact = false);

private:
  void prepareStatements();
  void finalizeStatements();

  sqlite3 *db_;
  std::mutex &dbMutex_;

  // Prepared statements — StripBlobs
  sqlite3_stmt *stmtPutStripBlob_ = nullptr;
  sqlite3_stmt *stmtGetStripBlob_ = nullptr;
  sqlite3_stmt *stmtHasStripBlob_ = nullptr;

  // Prepared statements — FiddleState
  sqlite3_stmt *stmtPutFiddleState_ = nullptr;
  sqlite3_stmt *stmtGetFiddleState_ = nullptr;
  sqlite3_stmt *stmtHasFiddleState_ = nullptr;

  // Prepared statements — Versions
  sqlite3_stmt *stmtPutVersion_ = nullptr;
  sqlite3_stmt *stmtGetVersion_ = nullptr;
  sqlite3_stmt *stmtHasVersion_ = nullptr;
  sqlite3_stmt *stmtRemoveVersion_ = nullptr;
  sqlite3_stmt *stmtUpdateVersionParent_ = nullptr;
  sqlite3_stmt *stmtGetChildVersions_ = nullptr;
  sqlite3_stmt *stmtListVersions_ = nullptr;

  // Prepared statements — Branches
  sqlite3_stmt *stmtPutBranch_ = nullptr;
  sqlite3_stmt *stmtGetBranch_ = nullptr;
  sqlite3_stmt *stmtFindBranchByName_ = nullptr;
  sqlite3_stmt *stmtListBranches_ = nullptr;
  sqlite3_stmt *stmtUpdateBranchHead_ = nullptr;
  sqlite3_stmt *stmtDeleteBranch_ = nullptr;
  sqlite3_stmt *stmtBranchNameExists_ = nullptr;

  // Prepared statements — Libraries
  sqlite3_stmt *stmtPutLibrary_ = nullptr;
  sqlite3_stmt *stmtGetLibraryName_ = nullptr;
  sqlite3_stmt *stmtListLibraries_ = nullptr;
  sqlite3_stmt *stmtLibraryNameExists_ = nullptr;
  sqlite3_stmt *stmtRenameLibrary_ = nullptr;
};

} // namespace fiddle::versioning
