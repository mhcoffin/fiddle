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
  virtual std::optional<FiddleState>
  getFiddleState(const Hash &hash) const = 0;
  virtual bool hasFiddleState(const Hash &hash) const = 0;

  // ── Versions ─────────────────────────────────────────────────────────

  virtual void putVersion(const Hash &hash, const Version &version) = 0;
  virtual std::optional<Version> getVersion(const Hash &hash) const = 0;
  virtual bool hasVersion(const Hash &hash) const = 0;

  /// Remove a version from storage.
  virtual void removeVersion(const Hash &hash) = 0;

  /// Find all versions whose parentHash or mergeParentHash equals the given
  /// hash.
  virtual std::vector<Hash> getChildVersionHashes(const Hash &hash) const = 0;

  // ── Branches ─────────────────────────────────────────────────────────

  /// Store a new branch (id → name + head hash).
  virtual void putBranch(const BranchId &id, const std::string &name,
                         const Hash &headHash) = 0;

  /// Get branch name and head hash by branch ID. Returns nullopt if not found.
  virtual std::optional<std::pair<std::string, Hash>>
  getBranch(const BranchId &id) const = 0;

  /// Update just the head hash of an existing branch.
  virtual void updateBranchHead(const BranchId &id, const Hash &newHead) = 0;

  /// Delete a branch record.
  virtual void deleteBranch(const BranchId &id) = 0;

  /// List all branches as (id, name, headHash) tuples.
  virtual std::vector<std::tuple<BranchId, std::string, Hash>>
  listBranches() const = 0;

  /// Check if any branch already uses the given name.
  virtual bool branchNameExists(const std::string &name) const = 0;

  /// Find the branch ID that has the given name. Returns nullopt if not found.
  virtual std::optional<BranchId>
  findBranchByName(const std::string &name) const = 0;
};

} // namespace fiddle::versioning
