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
  Hash newHeadHash;    ///< The new head of the target branch.
  std::string error;   ///< Non-empty if kind == Error.
};

/// Result of a squash operation.
struct SquashResult {
  bool success = false;
  std::string error; ///< Non-empty if success == false.
};

/// Result of a branch deletion check.
struct BranchDeleteCheck {
  bool canDelete = false;
  bool isMerged = true;     ///< False if the branch has unmerged work.
  std::string error;
};

/// Pure-logic versioning engine. All DAG algorithms live here.
/// No JUCE, no SQLite — operates entirely through IVersionStorage.
class VersionStore {
public:
  explicit VersionStore(IVersionStorage &storage);

  // ── Initialization ───────────────────────────────────────────────────

  /// Create the root version with an empty state and a "Main" branch.
  /// Returns the hash of the root version.
  Hash initializeEmpty();

  // ── Querying ─────────────────────────────────────────────────────────

  /// Get the head version hash of a branch.
  std::optional<Hash> getBranchHead(const BranchId &branchId) const;

  /// Get a version by hash.
  std::optional<Version> getVersion(const Hash &hash) const;

  /// Get a fiddle state by hash.
  std::optional<FiddleState> getState(const Hash &hash) const;

  /// Get a strip blob by hash.
  std::optional<StripBlob> getStripBlob(const Hash &hash) const;

  /// Get the ordered ancestor chain from root to the given version.
  std::vector<Hash> getAncestorChain(const Hash &versionHash) const;

  /// Check if `candidate` is an ancestor of `of` in the DAG.
  bool isAncestor(const Hash &candidate, const Hash &of) const;

  /// Find the nearest common ancestor (LCA) of two versions.
  std::optional<Hash> findCommonAncestor(const Hash &a, const Hash &b) const;

  // ── Mutations ────────────────────────────────────────────────────────

  /// Commit a new version on the given branch.
  /// Stores the state (and any new strip blobs), creates a Version node,
  /// and advances the branch head.
  /// Returns the hash of the new version.
  Hash commitVersion(const BranchId &branchId, const FiddleState &state);

  /// Create a new branch from an existing version.
  /// Returns the new branch ID.
  BranchId createBranch(const std::string &name, const Hash &fromVersionHash);

  /// Rename a branch. Returns false if the new name is already taken.
  bool renameBranch(const BranchId &branchId, const std::string &newName);

  /// Check if a branch can be deleted and whether it has unmerged work.
  BranchDeleteCheck checkBranchDelete(const BranchId &branchId) const;

  /// Delete a branch and all versions only reachable through it.
  /// Returns false with an error message if the branch cannot be deleted.
  std::pair<bool, std::string> deleteBranch(const BranchId &branchId);

  /// Merge sourceBranch onto targetBranch.
  MergeResult merge(const BranchId &sourceBranchId,
                    const BranchId &targetBranchId);

  /// Squash a version (remove it from the DAG).
  SquashResult squash(const Hash &versionHash);

  // ── Import (for Dorico load) ─────────────────────────────────────────

  /// Import state from a Dorico blob. Adds missing strip blobs, fiddle
  /// states, and versions. Returns the version hash of the imported state.
  Hash importFromBlob(const FiddleState &state,
                      const std::vector<StripBlob> &stripBlobs,
                      const std::vector<Hash> &ancestorChain,
                      const std::string &branchName);

private:
  IVersionStorage &storage_;

  /// Generate a UUID (for branch IDs).
  static BranchId generateUUID();

  /// Helper: collect all version hashes reachable from any branch
  /// (excluding the given branch).
  std::unordered_set<Hash>
  collectReachableFromOtherBranches(const BranchId &excludeBranchId) const;

  /// Helper: perform the three-way merge algorithm.
  FiddleState threeWayMerge(const FiddleState &ancestor,
                            const FiddleState &target,
                            const FiddleState &mergeParent) const;
};

} // namespace fiddle::versioning
