#pragma once

#include "VersionStoreInterface.h"
#include "VersionTypes.h"

#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fiddle::versioning {

/// Result of a merge operation.
struct MergeResult {
  enum Kind { FastForward, ThreeWay, Error };
  Kind kind = Error;
  VersionId newHeadId; ///< The new head of the target branch.
  std::string error;   ///< Non-empty if kind == Error.
};

/// Result of a delete operation.
struct DeleteResult {
  bool success = false;
  std::string error; ///< Non-empty if success == false.

  /// If the deletion caused the owning branch to become empty (only the
  /// root version remains), the branch is auto-deleted and its ID is stored
  /// here so the caller can switch the current branch to a survivor.
  std::string deletedBranchId;
};

/// Result of a branch deletion check.
struct BranchDeleteCheck {
  bool canDelete = false;
  bool isMerged = true; ///< False if the branch has unmerged work.
  std::string error;
};

/// Pure-logic versioning engine. All DAG algorithms live here.
/// No JUCE, no SQLite — operates entirely through IVersionStorage.
class VersionStore {
public:
  explicit VersionStore(IVersionStorage &storage);

  IVersionStorage &getStorage() { return storage_; }
  const IVersionStorage &getStorage() const { return storage_; }

  // ── Initialization ───────────────────────────────────────────────────

  /// Create the root version with an empty state and a "Main" branch.
  /// Also seeds the default library. Returns the VersionId of the root version.
  VersionId initializeEmpty();

  // ── Querying ─────────────────────────────────────────────────────────

  /// Get the head version ID of a branch.
  std::optional<VersionId> getBranchHead(const BranchId &branchId) const;

  /// Get a version by ID.
  std::optional<Version> getVersion(const VersionId &id) const;

  /// Get a fiddle state by hash.
  std::optional<FiddleState> getState(const Hash &hash) const;

  /// Get a strip blob by hash.
  std::optional<StripBlob> getStripBlob(const Hash &hash) const;

  /// Get the ordered ancestor chain from root to the given version.
  std::vector<VersionId> getAncestorChain(const VersionId &versionId) const;

  /// Retrieve all versions in the DAG.
  std::vector<std::pair<VersionId, Version>> listAllVersions() const;

  /// Check if `candidate` is an ancestor of `of` in the DAG.
  bool isAncestor(const VersionId &candidate, const VersionId &of) const;

  /// Find the nearest common ancestor (LCA) of two versions.
  std::optional<VersionId> findCommonAncestor(const VersionId &a,
                                              const VersionId &b) const;

  // ── Mutations ────────────────────────────────────────────────────────

  /// Commit a new version on the given branch.
  /// Stores the state (and any new strip blobs), creates a Version node,
  /// and advances the branch head.
  /// Returns the VersionId of the new version.
  VersionId commitVersion(const BranchId &branchId, const FiddleState &state);

  /// Create a new branch from an existing version.
  /// Returns the new branch ID.
  BranchId createBranch(const std::string &name,
                        const VersionId &fromVersionId);

  /// Rename a branch. Returns false if the new name is already taken.
  bool renameBranch(const BranchId &branchId, const std::string &newName);

  /// Check if a branch can be deleted and whether it has unmerged work.
  BranchDeleteCheck checkBranchDelete(const BranchId &branchId) const;

  /// Delete a branch and all versions only reachable through it.
  std::pair<bool, std::string> deleteBranch(const BranchId &branchId);

  /// Merge sourceBranch onto targetBranch.
  /// @param mpWins  if true (default), source (merge-parent) wins on conflict.
  MergeResult merge(const BranchId &sourceBranchId,
                    const BranchId &targetBranchId, bool mpWins = true);

  /// Delete a version (remove it from the DAG).
  /// For middle-node deletion, simply re-parents the child to the deleted
  /// version's primary parent (no re-hashing cascade needed now).
  DeleteResult deleteVersion(const VersionId &versionId);

  // ── Import (for Dorico load) ─────────────────────────────────────────

  /// Import state from a Dorico blob. Adds missing strip blobs, fiddle
  /// states, and versions. Returns the VersionId of the imported state.
  VersionId importFromBlob(const FiddleState &state,
                           const std::vector<StripBlob> &stripBlobs,
                           const std::vector<VersionId> &ancestorChain,
                           const std::string &branchName);

private:
  IVersionStorage &storage_;

  /// Generate a UUID (for branch IDs, version IDs).
  static std::string generateUUID();

  /// Helper: collect all version IDs reachable from any branch
  /// (excluding the given branch).
  std::unordered_set<VersionId>
  collectReachableFromOtherBranches(const BranchId &excludeBranchId) const;

  /// Helper: perform the three-way merge algorithm.
  /// Keys strips by (inputPort, inputChannel, libraryId).
  FiddleState threeWayMerge(const FiddleState &ancestor,
                            const FiddleState &target,
                            const FiddleState &mergeParent, bool mpWins) const;
};

} // namespace fiddle::versioning
