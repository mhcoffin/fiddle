#include "VersionStore.h"

#include <algorithm>
#include <random>
#include <sstream>
#include <unordered_set>

namespace fiddle::versioning {

VersionStore::VersionStore(IVersionStorage &storage) : storage_(storage) {}

// ---------------------------------------------------------------------------
// UUID generation
// ---------------------------------------------------------------------------

BranchId VersionStore::generateUUID() {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<uint64_t> dist;

  uint64_t a = dist(gen);
  uint64_t b = dist(gen);

  // Format as 8-4-4-4-12 hex
  char buf[37];
  std::snprintf(buf, sizeof(buf),
                "%08x-%04x-%04x-%04x-%012llx",
                (uint32_t)(a >> 32),
                (uint16_t)(a >> 16),
                (uint16_t)(a),
                (uint16_t)(b >> 48),
                (unsigned long long)(b & 0x0000FFFFFFFFFFFFULL));
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

Hash VersionStore::initializeEmpty() {
  // Create an empty fiddle state
  FiddleState rootState;
  rootState.globalState.masterGainDb = 0.0f;
  // No strips.

  Hash stateHash = rootState.computeHash();
  storage_.putFiddleState(stateHash, rootState);

  // Create the root version (no parent, no merge-parent)
  BranchId mainBranchId = generateUUID();

  Version rootVersion;
  rootVersion.stateHash = stateHash;
  rootVersion.branchId = mainBranchId;
  // parentHash and mergeParentHash remain empty

  Hash versionHash = rootVersion.computeHash();
  storage_.putVersion(versionHash, rootVersion);

  // Create the "Main" branch pointing to the root version
  storage_.putBranch(mainBranchId, "Main", versionHash);

  return versionHash;
}

// ---------------------------------------------------------------------------
// Querying
// ---------------------------------------------------------------------------

std::optional<Hash>
VersionStore::getBranchHead(const BranchId &branchId) const {
  auto branch = storage_.getBranch(branchId);
  if (!branch)
    return std::nullopt;
  return branch->second;
}

std::optional<Version> VersionStore::getVersion(const Hash &hash) const {
  return storage_.getVersion(hash);
}

std::optional<FiddleState> VersionStore::getState(const Hash &hash) const {
  return storage_.getFiddleState(hash);
}

std::optional<StripBlob> VersionStore::getStripBlob(const Hash &hash) const {
  return storage_.getStripBlob(hash);
}

std::vector<Hash>
VersionStore::getAncestorChain(const Hash &versionHash) const {
  // Walk parent pointers to root, then reverse.
  std::vector<Hash> chain;
  Hash current = versionHash;

  while (!current.empty()) {
    chain.push_back(current);
    auto ver = storage_.getVersion(current);
    if (!ver)
      break;
    current = ver->parentHash;
  }

  std::reverse(chain.begin(), chain.end());
  return chain;
}

bool VersionStore::isAncestor(const Hash &candidate, const Hash &of) const {
  if (candidate == of)
    return true;

  // BFS from `of` walking both parent and merge-parent edges.
  std::unordered_set<Hash> visited;
  std::vector<Hash> queue;
  queue.push_back(of);

  while (!queue.empty()) {
    Hash current = queue.back();
    queue.pop_back();

    if (current == candidate)
      return true;

    if (visited.count(current))
      continue;
    visited.insert(current);

    auto ver = storage_.getVersion(current);
    if (!ver)
      continue;

    if (!ver->parentHash.empty())
      queue.push_back(ver->parentHash);
    if (!ver->mergeParentHash.empty())
      queue.push_back(ver->mergeParentHash);
  }

  return false;
}

std::optional<Hash> VersionStore::findCommonAncestor(const Hash &a,
                                                     const Hash &b) const {
  // Collect all ancestors of `a`, then walk ancestors of `b` to find the
  // first match. This finds *a* common ancestor; for LCA we'd need a more
  // sophisticated algorithm, but for our merge use case this is sufficient
  // since we primarily follow parent edges.

  // Collect ancestors of `a` (BFS over parent + merge-parent)
  std::unordered_set<Hash> ancestorsOfA;
  {
    std::vector<Hash> queue;
    queue.push_back(a);
    while (!queue.empty()) {
      Hash current = queue.back();
      queue.pop_back();
      if (ancestorsOfA.count(current))
        continue;
      ancestorsOfA.insert(current);
      auto ver = storage_.getVersion(current);
      if (!ver)
        continue;
      if (!ver->parentHash.empty())
        queue.push_back(ver->parentHash);
      if (!ver->mergeParentHash.empty())
        queue.push_back(ver->mergeParentHash);
    }
  }

  // BFS from `b`, return first node in ancestorsOfA.
  // We use BFS to find the *nearest* common ancestor.
  {
    std::unordered_set<Hash> visited;
    std::vector<Hash> queue;
    queue.push_back(b);
    size_t front = 0;

    while (front < queue.size()) {
      Hash current = queue[front++];

      if (ancestorsOfA.count(current))
        return current;

      if (visited.count(current))
        continue;
      visited.insert(current);

      auto ver = storage_.getVersion(current);
      if (!ver)
        continue;
      if (!ver->parentHash.empty())
        queue.push_back(ver->parentHash);
      if (!ver->mergeParentHash.empty())
        queue.push_back(ver->mergeParentHash);
    }
  }

  return std::nullopt;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

Hash VersionStore::commitVersion(const BranchId &branchId,
                                 const FiddleState &state) {
  // Store strip blobs (caller should have already stored them,
  // but we ensure the state is stored)
  Hash stateHash = state.computeHash();
  storage_.putFiddleState(stateHash, state);

  // Get the current branch head (which becomes the parent)
  auto branch = storage_.getBranch(branchId);
  Hash parentHash = branch ? branch->second : "";

  // Create the version
  Version ver;
  ver.stateHash = stateHash;
  ver.branchId = branchId;
  ver.parentHash = parentHash;
  // mergeParentHash stays empty

  Hash versionHash = ver.computeHash();
  storage_.putVersion(versionHash, ver);

  // Advance the branch head
  storage_.updateBranchHead(branchId, versionHash);

  return versionHash;
}

BranchId VersionStore::createBranch(const std::string &name,
                                    const Hash &fromVersionHash) {
  BranchId newId = generateUUID();

  // The new branch initially points to the same version
  // (which is on its original branch — that's fine).
  // We create a new version on the new branch with the same state.
  auto sourceVer = storage_.getVersion(fromVersionHash);
  if (!sourceVer)
    return ""; // Error: source version not found

  Version branchVer;
  branchVer.stateHash = sourceVer->stateHash;
  branchVer.branchId = newId;
  branchVer.parentHash = fromVersionHash;
  // mergeParentHash stays empty

  Hash branchVerHash = branchVer.computeHash();
  storage_.putVersion(branchVerHash, branchVer);
  storage_.putBranch(newId, name, branchVerHash);

  return newId;
}

bool VersionStore::renameBranch(const BranchId &branchId,
                                const std::string &newName) {
  auto branch = storage_.getBranch(branchId);
  if (!branch)
    return false;

  if (storage_.branchNameExists(newName))
    return false;

  // Re-store with new name, same head
  storage_.putBranch(branchId, newName, branch->second);
  return true;
}

// ---------------------------------------------------------------------------
// Branch deletion
// ---------------------------------------------------------------------------

std::unordered_set<Hash> VersionStore::collectReachableFromOtherBranches(
    const BranchId &excludeBranchId) const {
  std::unordered_set<Hash> reachable;
  auto branches = storage_.listBranches();

  for (const auto &[id, name, headHash] : branches) {
    if (id == excludeBranchId)
      continue;

    // BFS from this branch's head
    std::vector<Hash> queue;
    queue.push_back(headHash);

    while (!queue.empty()) {
      Hash current = queue.back();
      queue.pop_back();

      if (current.empty() || reachable.count(current))
        continue;
      reachable.insert(current);

      auto ver = storage_.getVersion(current);
      if (!ver)
        continue;
      if (!ver->parentHash.empty())
        queue.push_back(ver->parentHash);
      if (!ver->mergeParentHash.empty())
        queue.push_back(ver->mergeParentHash);
    }
  }

  return reachable;
}

BranchDeleteCheck
VersionStore::checkBranchDelete(const BranchId &branchId) const {
  BranchDeleteCheck result;

  auto branch = storage_.getBranch(branchId);
  if (!branch) {
    result.error = "Branch not found";
    return result;
  }

  // Check if the branch head is reachable from any other branch
  auto reachable = collectReachableFromOtherBranches(branchId);
  result.isMerged = reachable.count(branch->second) > 0;
  result.canDelete = true;
  return result;
}

std::pair<bool, std::string>
VersionStore::deleteBranch(const BranchId &branchId) {
  auto branch = storage_.getBranch(branchId);
  if (!branch)
    return {false, "Branch not found"};

  // Collect all versions reachable from other branches
  auto reachable = collectReachableFromOtherBranches(branchId);

  // Walk from the branch head and delete any version not in `reachable`
  std::vector<Hash> toVisit;
  toVisit.push_back(branch->second);
  std::unordered_set<Hash> visited;

  while (!toVisit.empty()) {
    Hash current = toVisit.back();
    toVisit.pop_back();

    if (current.empty() || visited.count(current))
      continue;
    visited.insert(current);

    if (reachable.count(current))
      continue; // This version is reachable from another branch, keep it

    auto ver = storage_.getVersion(current);
    if (!ver)
      continue;

    // Queue parents for visiting before we delete
    if (!ver->parentHash.empty())
      toVisit.push_back(ver->parentHash);
    if (!ver->mergeParentHash.empty())
      toVisit.push_back(ver->mergeParentHash);

    storage_.removeVersion(current);
  }

  storage_.deleteBranch(branchId);
  return {true, ""};
}

// ---------------------------------------------------------------------------
// Merge
// ---------------------------------------------------------------------------

FiddleState
VersionStore::threeWayMerge(const FiddleState &ancestor,
                            const FiddleState &target,
                            const FiddleState &mergeParent) const {
  // Build UUID → hash maps for each state.
  // We need to extract the UUID from each strip blob to identify strips.

  auto buildUuidMap =
      [&](const std::vector<Hash> &hashes) -> std::unordered_map<std::string, Hash> {
    std::unordered_map<std::string, Hash> map;
    for (const auto &h : hashes) {
      auto blob = storage_.getStripBlob(h);
      if (blob)
        map[blob->uuid] = h;
    }
    return map;
  };

  auto ancestorMap = buildUuidMap(ancestor.stripHashes);
  auto targetMap = buildUuidMap(target.stripHashes);
  auto mpMap = buildUuidMap(mergeParent.stripHashes);

  // Start with target's strips
  FiddleState result;
  result.globalState = target.globalState; // Keep target's global state

  // Collect the UUIDs in target order first
  std::vector<std::string> resultUuids;
  for (const auto &h : target.stripHashes) {
    auto blob = storage_.getStripBlob(h);
    if (blob)
      resultUuids.push_back(blob->uuid);
  }

  // Handle deletions: if strip is in ancestor but not in MP, remove from result
  for (const auto &[uuid, hash] : ancestorMap) {
    if (mpMap.find(uuid) == mpMap.end()) {
      // MP deleted this strip — remove from result
      resultUuids.erase(
          std::remove(resultUuids.begin(), resultUuids.end(), uuid),
          resultUuids.end());
    }
  }

  // Handle additions: if strip is in MP but not in ancestor, add to result
  for (const auto &[uuid, hash] : mpMap) {
    if (ancestorMap.find(uuid) == ancestorMap.end()) {
      // MP added this strip — add to result (if not already there)
      if (std::find(resultUuids.begin(), resultUuids.end(), uuid) ==
          resultUuids.end()) {
        resultUuids.push_back(uuid);
      }
    }
  }

  // Build result strip hashes, applying modifications
  for (const auto &uuid : resultUuids) {
    // Check if MP modified this strip relative to ancestor
    auto mpIt = mpMap.find(uuid);
    auto ancestorIt = ancestorMap.find(uuid);

    if (mpIt != mpMap.end() && ancestorIt != ancestorMap.end() &&
        mpIt->second != ancestorIt->second) {
      // MP modified this strip — use MP's version (MP wins on conflict too)
      result.stripHashes.push_back(mpIt->second);
    } else if (mpIt != mpMap.end() && ancestorIt == ancestorMap.end()) {
      // This strip was added by MP, use MP's version
      result.stripHashes.push_back(mpIt->second);
    } else {
      // Use target's version (or whatever is available)
      auto targetIt = targetMap.find(uuid);
      if (targetIt != targetMap.end()) {
        result.stripHashes.push_back(targetIt->second);
      }
    }
  }

  return result;
}

MergeResult VersionStore::merge(const BranchId &sourceBranchId,
                                const BranchId &targetBranchId) {
  MergeResult result;

  auto sourceBranch = storage_.getBranch(sourceBranchId);
  auto targetBranch = storage_.getBranch(targetBranchId);

  if (!sourceBranch || !targetBranch) {
    result.kind = MergeResult::Error;
    result.error = "Branch not found";
    return result;
  }

  Hash sourceHead = sourceBranch->second;
  Hash targetHead = targetBranch->second;

  // Check: source must not be an ancestor of target (would be a no-op)
  if (isAncestor(sourceHead, targetHead)) {
    result.kind = MergeResult::Error;
    result.error = "Source is already an ancestor of target (nothing to merge)";
    return result;
  }

  // Fast-forward: if target is an ancestor of source, just advance
  if (isAncestor(targetHead, sourceHead)) {
    storage_.updateBranchHead(targetBranchId, sourceHead);
    result.kind = MergeResult::FastForward;
    result.newHeadHash = sourceHead;
    return result;
  }

  // Three-way merge
  auto lca = findCommonAncestor(sourceHead, targetHead);
  if (!lca) {
    result.kind = MergeResult::Error;
    result.error = "No common ancestor found";
    return result;
  }

  auto ancestorState = storage_.getFiddleState(
      storage_.getVersion(*lca)->stateHash);
  auto targetState = storage_.getFiddleState(
      storage_.getVersion(targetHead)->stateHash);
  auto sourceState = storage_.getFiddleState(
      storage_.getVersion(sourceHead)->stateHash);

  if (!ancestorState || !targetState || !sourceState) {
    result.kind = MergeResult::Error;
    result.error = "Could not load states for merge";
    return result;
  }

  FiddleState mergedState =
      threeWayMerge(*ancestorState, *targetState, *sourceState);
  Hash mergedStateHash = mergedState.computeHash();
  storage_.putFiddleState(mergedStateHash, mergedState);

  // Create merge version
  Version mergeVer;
  mergeVer.stateHash = mergedStateHash;
  mergeVer.branchId = targetBranchId;
  mergeVer.parentHash = targetHead;
  mergeVer.mergeParentHash = sourceHead;

  Hash mergeVerHash = mergeVer.computeHash();
  storage_.putVersion(mergeVerHash, mergeVer);
  storage_.updateBranchHead(targetBranchId, mergeVerHash);

  result.kind = MergeResult::ThreeWay;
  result.newHeadHash = mergeVerHash;
  return result;
}

// ---------------------------------------------------------------------------
// Squash
// ---------------------------------------------------------------------------

SquashResult VersionStore::squash(const Hash &versionHash) {
  SquashResult result;

  auto ver = storage_.getVersion(versionHash);
  if (!ver) {
    result.error = "Version not found";
    return result;
  }

  // Must have exactly one parent (no merge-parent)
  if (ver->parentHash.empty()) {
    result.error = "Cannot squash the root version";
    return result;
  }
  if (!ver->mergeParentHash.empty()) {
    result.error = "Cannot squash a version with a merge-parent";
    return result;
  }

  // Must have exactly one child
  auto children = storage_.getChildVersionHashes(versionHash);
  if (children.size() != 1) {
    result.error = children.empty()
                       ? "Cannot squash a version with no children (it is a "
                         "branch head)"
                       : "Cannot squash a version with multiple children";
    return result;
  }

  Hash childHash = children[0];
  auto childVer = storage_.getVersion(childHash);
  if (!childVer) {
    result.error = "Child version not found";
    return result;
  }

  // Re-parent the child: child's parent becomes the squashed version's parent
  Version updatedChild = *childVer;
  updatedChild.parentHash = ver->parentHash;

  // The child's hash changes because its parent changed
  Hash newChildHash = updatedChild.computeHash();

  // We need to update anything that points to the old child hash.
  // This includes: other children of the old child, and branch heads.

  // First, update all grandchildren's parent pointers
  auto grandchildren = storage_.getChildVersionHashes(childHash);
  for (const auto &gcHash : grandchildren) {
    auto gc = storage_.getVersion(gcHash);
    if (!gc)
      continue;
    Version updatedGc = *gc;
    if (updatedGc.parentHash == childHash)
      updatedGc.parentHash = newChildHash;
    if (updatedGc.mergeParentHash == childHash)
      updatedGc.mergeParentHash = newChildHash;
    Hash newGcHash = updatedGc.computeHash();
    storage_.removeVersion(gcHash);
    storage_.putVersion(newGcHash, updatedGc);
    // Note: this could cascade. For simplicity, we only handle one level.
    // Deep cascading squash is a future enhancement.
  }

  // Update branch heads that point to the old child hash
  auto branches = storage_.listBranches();
  for (const auto &[branchId, branchName, headHash] : branches) {
    if (headHash == childHash) {
      storage_.updateBranchHead(branchId, newChildHash);
    }
  }

  // Remove the old child and the squashed version, insert the updated child
  storage_.removeVersion(childHash);
  storage_.removeVersion(versionHash);
  storage_.putVersion(newChildHash, updatedChild);

  result.success = true;
  return result;
}

// ---------------------------------------------------------------------------
// Import from Dorico blob
// ---------------------------------------------------------------------------

Hash VersionStore::importFromBlob(const FiddleState &state,
                                  const std::vector<StripBlob> &stripBlobs,
                                  const std::vector<Hash> &ancestorChain,
                                  const std::string &branchName) {
  // Store any missing strip blobs
  for (const auto &blob : stripBlobs) {
    Hash h = blob.computeHash();
    if (!storage_.hasStripBlob(h))
      storage_.putStripBlob(h, blob);
  }

  // Store the fiddle state if missing
  Hash stateHash = state.computeHash();
  if (!storage_.hasFiddleState(stateHash))
    storage_.putFiddleState(stateHash, state);

  // Find the closest existing ancestor
  Hash closestAncestor;
  for (auto it = ancestorChain.rbegin(); it != ancestorChain.rend(); ++it) {
    if (storage_.hasVersion(*it)) {
      closestAncestor = *it;
      break;
    }
  }

  // If no ancestor found, use the root (head of "Main")
  if (closestAncestor.empty()) {
    auto mainBranchId = storage_.findBranchByName("Main");
    if (mainBranchId) {
      auto mainBranch = storage_.getBranch(*mainBranchId);
      if (mainBranch) {
        // Walk to root
        Hash current = mainBranch->second;
        while (!current.empty()) {
          auto ver = storage_.getVersion(current);
          if (!ver || ver->parentHash.empty()) {
            closestAncestor = current;
            break;
          }
          current = ver->parentHash;
        }
      }
    }
  }

  // Resolve branch: create if needed, handle name conflicts
  std::string effectiveName = branchName;
  BranchId branchId;

  auto existingBranch = storage_.findBranchByName(branchName);
  if (existingBranch) {
    branchId = *existingBranch;
  } else {
    // Check if name conflicts, generate a unique one
    if (storage_.branchNameExists(effectiveName)) {
      int suffix = 1;
      while (storage_.branchNameExists(effectiveName + " " +
                                       std::to_string(suffix))) {
        ++suffix;
      }
      effectiveName = effectiveName + " " + std::to_string(suffix);
    }
    branchId = generateUUID();
  }

  // Create the new version
  Version importVer;
  importVer.stateHash = stateHash;
  importVer.branchId = branchId;
  importVer.parentHash = closestAncestor;
  // mergeParentHash stays empty

  Hash importVerHash = importVer.computeHash();

  if (!storage_.hasVersion(importVerHash)) {
    storage_.putVersion(importVerHash, importVer);
  }

  // Create or update the branch
  if (existingBranch) {
    // Branch exists — only update head if the imported version is new
    storage_.updateBranchHead(branchId, importVerHash);
  } else {
    storage_.putBranch(branchId, effectiveName, importVerHash);
  }

  return importVerHash;
}

} // namespace fiddle::versioning
