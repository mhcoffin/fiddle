#pragma once

#include "VersionTypes.h"

#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace fiddle::versioning {

/// Abstract storage interface for versioning data.
///
/// Implementations:
///   - InMemoryVersionStorage (tests)
///   - SqliteVersionStorage  (production)
class IVersionStorage {
public:
  virtual ~IVersionStorage() = default;

  // ── Strip blobs ──────────────────────────────────────────────────────

  virtual void putStripBlob(const Hash &hash, const StripBlob &blob) = 0;
  virtual std::optional<StripBlob> getStripBlob(const Hash &hash) const = 0;
  virtual bool hasStripBlob(const Hash &hash) const = 0;

  // ── Fiddle states ────────────────────────────────────────────────────

  virtual void putFiddleState(const Hash &hash, const FiddleState &state) = 0;
  virtual std::optional<FiddleState> getFiddleState(const Hash &hash) const = 0;
  virtual bool hasFiddleState(const Hash &hash) const = 0;

  // ── Versions ─────────────────────────────────────────────────────────

  /// Store a version by its random UUID primary key.
  virtual void putVersion(const VersionId &id, const Version &version) = 0;
  virtual std::optional<Version> getVersion(const VersionId &id) const = 0;
  virtual bool hasVersion(const VersionId &id) const = 0;

  /// Remove a version from storage.
  virtual void removeVersion(const VersionId &id) = 0;

  /// Update a version's parentId in-place (for delete reparenting).
  virtual void updateVersionParent(const VersionId &id,
                                   const VersionId &newParentId) = 0;

  /// Find all versions whose parentId or mergeParentId equals the given ID.
  virtual std::vector<VersionId>
  getChildVersionIds(const VersionId &parentId) const = 0;

  /// Retrieve all versions in the DB (for graph visualization).
  virtual std::vector<std::pair<VersionId, Version>>
  listAllVersions() const = 0;

  // ── Branches ─────────────────────────────────────────────────────────

  /// Store a new branch (id → name + head version id).
  virtual void putBranch(const BranchId &id, const std::string &name,
                         const VersionId &headId) = 0;

  /// Get branch name and head version id by branch ID. Returns nullopt if not
  /// found.
  virtual std::optional<std::pair<std::string, VersionId>>
  getBranch(const BranchId &id) const = 0;

  /// Update just the head version id of an existing branch.
  virtual void updateBranchHead(const BranchId &id,
                                const VersionId &newHeadId) = 0;

  /// Delete a branch record.
  virtual void deleteBranch(const BranchId &id) = 0;

  /// List all branches as (id, name, headVersionId) tuples.
  virtual std::vector<std::tuple<BranchId, std::string, VersionId>>
  listBranches() const = 0;

  /// Check if any branch already uses the given name.
  virtual bool branchNameExists(const std::string &name) const = 0;

  /// Find the branch ID that has the given name. Returns nullopt if not found.
  virtual std::optional<BranchId>
  findBranchByName(const std::string &name) const = 0;

  // ── Libraries ────────────────────────────────────────────────────────

  /// Upsert a library record (id → name).
  virtual void putLibrary(const LibraryId &id, const std::string &name) = 0;

  /// Get a library's name by ID. Returns nullopt if not found.
  virtual std::optional<std::string>
  getLibraryName(const LibraryId &id) const = 0;

  /// List all libraries as (id, name) pairs.
  virtual std::vector<std::pair<LibraryId, std::string>>
  listLibraries() const = 0;

  /// Check if any library already uses the given name.
  virtual bool libraryNameExists(const std::string &name) const = 0;

  /// Rename a library. Returns false if the ID is not found.
  virtual bool renameLibrary(const LibraryId &id, const std::string &name) = 0;
};

} // namespace fiddle::versioning
