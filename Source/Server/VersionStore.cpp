#include "VersionStore.h"

#include <algorithm>
#include <map>
#include <random>
#include <unordered_set>

namespace fiddle::versioning {

VersionStore::VersionStore(IVersionStorage &storage) : storage_(storage) {}

// ---------------------------------------------------------------------------
// UUID generation
// ---------------------------------------------------------------------------

std::string VersionStore::generateUUID() {
  static std::random_device rd;
  static std::mt19937_64 gen(rd());
  static std::uniform_int_distribution<uint64_t> dist;

  uint64_t a = dist(gen);
  uint64_t b = dist(gen);

  // Format as 8-4-4-4-12 hex
  char buf[37];
  std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                (uint32_t)(a >> 32), (uint16_t)(a >> 16), (uint16_t)(a),
                (uint16_t)(b >> 48),
                (unsigned long long)(b & 0x0000FFFFFFFFFFFFULL));
  return std::string(buf);
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

VersionId VersionStore::initializeEmpty() {
  if (storage_.findBranchByName("Main")) {
    // Already initialized — return current main head
    return storage_.getBranch(*storage_.findBranchByName("Main"))->second;
  }

  // Seed default library
  if (!storage_.getLibraryName(kDefaultLibraryId)) {
    storage_.putLibrary(kDefaultLibraryId, "");
  }

  // Create an empty fiddle state
  FiddleState rootState;
  rootState.globalState.masterGainDb = 0.0f;

  Hash stateHash = rootState.computeHash();
  storage_.putFiddleState(stateHash, rootState);

  // Create the root version with a random VersionId
  BranchId mainBranchId = generateUUID();
  VersionId rootId = generateUUID();

  Version rootVersion;
  rootVersion.id = rootId;
  rootVersion.stateHash = stateHash;
  rootVersion.branchId = mainBranchId;
  // parentId and mergeParentId remain empty (root version)

  storage_.putVersion(rootId, rootVersion);

  // Create the "Main" branch pointing to the root version
  storage_.putBranch(mainBranchId, "Main", rootId);

  return rootId;
}

// ---------------------------------------------------------------------------
// Querying
// ---------------------------------------------------------------------------

std::optional<VersionId>
VersionStore::getBranchHead(const BranchId &branchId) const {
  auto branch = storage_.getBranch(branchId);
  if (!branch)
    return std::nullopt;
  return branch->second;
}

std::optional<Version> VersionStore::getVersion(const VersionId &id) const {
  return storage_.getVersion(id);
}

std::optional<FiddleState> VersionStore::getState(const Hash &hash) const {
  return storage_.getFiddleState(hash);
}

std::optional<StripBlob> VersionStore::getStripBlob(const Hash &hash) const {
  return storage_.getStripBlob(hash);
}

std::vector<VersionId>
VersionStore::getAncestorChain(const VersionId &versionId) const {
  // Walk parent pointers to root, then reverse.
  std::vector<VersionId> chain;
  VersionId current = versionId;

  while (!current.empty()) {
    chain.push_back(current);
    auto ver = storage_.getVersion(current);
    if (!ver)
      break;
    current = ver->parentId;
  }

  std::reverse(chain.begin(), chain.end());
  return chain;
}

std::vector<std::pair<VersionId, Version>>
VersionStore::listAllVersions() const {
  return storage_.listAllVersions();
}

bool VersionStore::isAncestor(const VersionId &candidate,
                              const VersionId &of) const {
  if (candidate == of)
    return true;

  // BFS from `of` walking both parent and merge-parent edges.
  std::unordered_set<VersionId> visited;
  std::vector<VersionId> queue;
  queue.push_back(of);

  while (!queue.empty()) {
    VersionId current = queue.back();
    queue.pop_back();

    if (current == candidate)
      return true;

    if (visited.count(current))
      continue;
    visited.insert(current);

    auto ver = storage_.getVersion(current);
    if (!ver)
      continue;

    if (!ver->parentId.empty())
      queue.push_back(ver->parentId);
    if (!ver->mergeParentId.empty())
      queue.push_back(ver->mergeParentId);
  }

  return false;
}

std::optional<VersionId>
VersionStore::findCommonAncestor(const VersionId &a, const VersionId &b) const {
  // Collect all ancestors of `a`, then BFS from `b` to find nearest match.
  std::unordered_set<VersionId> ancestorsOfA;
  {
    std::vector<VersionId> queue;
    queue.push_back(a);
    while (!queue.empty()) {
      VersionId current = queue.back();
      queue.pop_back();
      if (ancestorsOfA.count(current))
        continue;
      ancestorsOfA.insert(current);
      auto ver = storage_.getVersion(current);
      if (!ver)
        continue;
      if (!ver->parentId.empty())
        queue.push_back(ver->parentId);
      if (!ver->mergeParentId.empty())
        queue.push_back(ver->mergeParentId);
    }
  }

  {
    std::unordered_set<VersionId> visited;
    std::vector<VersionId> queue;
    queue.push_back(b);
    size_t front = 0;

    while (front < queue.size()) {
      VersionId current = queue[front++];

      if (ancestorsOfA.count(current))
        return current;

      if (visited.count(current))
        continue;
      visited.insert(current);

      auto ver = storage_.getVersion(current);
      if (!ver)
        continue;
      if (!ver->parentId.empty())
        queue.push_back(ver->parentId);
      if (!ver->mergeParentId.empty())
        queue.push_back(ver->mergeParentId);
    }
  }

  return std::nullopt;
}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

VersionId VersionStore::commitVersion(const BranchId &branchId,
                                      const FiddleState &state) {
  Hash stateHash = state.computeHash();
  storage_.putFiddleState(stateHash, state);

  // Get the current branch head (becomes the parent)
  auto branch = storage_.getBranch(branchId);
  VersionId parentId = branch ? branch->second : "";

  // Create the new version with a random UUID
  VersionId newId = generateUUID();
  Version ver;
  ver.id = newId;
  ver.stateHash = stateHash;
  ver.branchId = branchId;
  ver.parentId = parentId;
  // mergeParentId stays empty

  storage_.putVersion(newId, ver);

  // Advance the branch head
  storage_.updateBranchHead(branchId, newId);

  return newId;
}

BranchId VersionStore::createBranch(const std::string &name,
                                    const VersionId &fromVersionId) {
  BranchId newBranchId = generateUUID();

  auto sourceVer = storage_.getVersion(fromVersionId);
  if (!sourceVer)
    return ""; // Error: source version not found

  // Create an initial version on the new branch
  VersionId branchVerId = generateUUID();
  Version branchVer;
  branchVer.id = branchVerId;
  branchVer.stateHash = sourceVer->stateHash;
  branchVer.branchId = newBranchId;
  branchVer.parentId = fromVersionId;
  // mergeParentId stays empty

  storage_.putVersion(branchVerId, branchVer);
  storage_.putBranch(newBranchId, name, branchVerId);

  return newBranchId;
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

std::unordered_set<VersionId> VersionStore::collectReachableFromOtherBranches(
    const BranchId &excludeBranchId) const {
  std::unordered_set<VersionId> reachable;
  auto branches = storage_.listBranches();

  for (const auto &[id, name, headId] : branches) {
    if (id == excludeBranchId)
      continue;

    std::vector<VersionId> queue;
    queue.push_back(headId);

    while (!queue.empty()) {
      VersionId current = queue.back();
      queue.pop_back();

      if (current.empty() || reachable.count(current))
        continue;
      reachable.insert(current);

      auto ver = storage_.getVersion(current);
      if (!ver)
        continue;
      if (!ver->parentId.empty())
        queue.push_back(ver->parentId);
      if (!ver->mergeParentId.empty())
        queue.push_back(ver->mergeParentId);
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

  auto reachable = collectReachableFromOtherBranches(branchId);

  std::vector<VersionId> toVisit;
  toVisit.push_back(branch->second);
  std::unordered_set<VersionId> visited;

  while (!toVisit.empty()) {
    VersionId current = toVisit.back();
    toVisit.pop_back();

    if (current.empty() || visited.count(current))
      continue;
    visited.insert(current);

    if (reachable.count(current))
      continue; // Reachable from another branch, keep it

    auto ver = storage_.getVersion(current);
    if (!ver)
      continue;

    if (!ver->parentId.empty())
      toVisit.push_back(ver->parentId);
    if (!ver->mergeParentId.empty())
      toVisit.push_back(ver->mergeParentId);

    storage_.removeVersion(current);
  }

  storage_.deleteBranch(branchId);
  return {true, ""};
}

// ---------------------------------------------------------------------------
// Merge (Phase 4: strip key = (port, channel, libraryId))
// ---------------------------------------------------------------------------

FiddleState VersionStore::threeWayMerge(const FiddleState &ancestor,
                                        const FiddleState &target,
                                        const FiddleState &mergeParent,
                                        bool mpWins) const {
  // Key type: (inputPort, inputChannel, libraryId)
  using StripKey = std::tuple<int, int, std::string>;

  auto buildKeyMap =
      [&](const std::vector<Hash> &hashes) -> std::map<StripKey, Hash> {
    std::map<StripKey, Hash> map;
    for (const auto &h : hashes) {
      auto blob = storage_.getStripBlob(h);
      if (blob)
        map[{blob->inputPort, blob->inputChannel, blob->libraryId}] = h;
    }
    return map;
  };

  auto ancestorMap = buildKeyMap(ancestor.stripHashes);
  auto targetMap = buildKeyMap(target.stripHashes);
  auto mpMap = buildKeyMap(mergeParent.stripHashes);

  // Start with target's strip order; results are collected in a map to
  // preserve stable (port, channel, libraryId) ordering.
  std::map<StripKey, Hash> resultMap;

  // Seed from target
  for (const auto &[key, hash] : targetMap)
    resultMap[key] = hash;

  // Handle MP deletions: if strip in ancestor but not in MP, remove from result
  for (const auto &[key, hash] : ancestorMap) {
    if (mpMap.find(key) == mpMap.end()) {
      resultMap.erase(key);
    }
  }

  // Handle MP additions: if strip in MP but not in ancestor, add to result
  for (const auto &[key, hash] : mpMap) {
    if (ancestorMap.find(key) == ancestorMap.end()) {
      if (resultMap.find(key) == resultMap.end()) {
        resultMap[key] = hash;
      }
    }
  }

  // Apply modifications: if MP changed a strip relative to ancestor → apply
  for (auto &[key, resultHash] : resultMap) {
    auto mpIt = mpMap.find(key);
    auto ancestorIt = ancestorMap.find(key);

    if (mpIt != mpMap.end() && ancestorIt != ancestorMap.end() &&
        mpIt->second != ancestorIt->second) {
      // Both MP and possibly target changed this strip
      auto targetIt = targetMap.find(key);
      bool targetChanged = (targetIt != targetMap.end() &&
                            targetIt->second != ancestorIt->second);
      if (!targetChanged || mpWins) {
        // MP wins (or no conflict)
        resultHash = mpIt->second;
      }
      // else target wins — resultHash already holds target's hash
    }
  }

  FiddleState result;
  result.globalState = target.globalState;
  for (const auto &[key, hash] : resultMap)
    result.stripHashes.push_back(hash);

  return result;
}

MergeResult VersionStore::merge(const BranchId &sourceBranchId,
                                const BranchId &targetBranchId, bool mpWins) {
  MergeResult result;

  auto sourceBranch = storage_.getBranch(sourceBranchId);
  auto targetBranch = storage_.getBranch(targetBranchId);

  if (!sourceBranch || !targetBranch) {
    result.kind = MergeResult::Error;
    result.error = "Branch not found";
    return result;
  }

  VersionId sourceHead = sourceBranch->second;
  VersionId targetHead = targetBranch->second;

  // Cannot merge if source is already an ancestor of target
  if (isAncestor(sourceHead, targetHead)) {
    result.kind = MergeResult::Error;
    result.error = "Source is already an ancestor of target (nothing to merge)";
    return result;
  }

  // Fast-forward: if target is an ancestor of source, just advance
  if (isAncestor(targetHead, sourceHead)) {
    storage_.updateBranchHead(targetBranchId, sourceHead);
    result.kind = MergeResult::FastForward;
    result.newHeadId = sourceHead;
    return result;
  }

  // Three-way merge
  auto lca = findCommonAncestor(sourceHead, targetHead);
  if (!lca) {
    result.kind = MergeResult::Error;
    result.error = "No common ancestor found";
    return result;
  }

  auto ancestorState =
      storage_.getFiddleState(storage_.getVersion(*lca)->stateHash);
  auto targetState =
      storage_.getFiddleState(storage_.getVersion(targetHead)->stateHash);
  auto sourceState =
      storage_.getFiddleState(storage_.getVersion(sourceHead)->stateHash);

  if (!ancestorState || !targetState || !sourceState) {
    result.kind = MergeResult::Error;
    result.error = "Could not load states for merge";
    return result;
  }

  FiddleState mergedState =
      threeWayMerge(*ancestorState, *targetState, *sourceState, mpWins);
  Hash mergedStateHash = mergedState.computeHash();
  storage_.putFiddleState(mergedStateHash, mergedState);

  // Create merge version
  VersionId mergeVerId = generateUUID();
  Version mergeVer;
  mergeVer.id = mergeVerId;
  mergeVer.stateHash = mergedStateHash;
  mergeVer.branchId = targetBranchId;
  mergeVer.parentId = targetHead;
  mergeVer.mergeParentId = sourceHead;

  storage_.putVersion(mergeVerId, mergeVer);
  storage_.updateBranchHead(targetBranchId, mergeVerId);

  result.kind = MergeResult::ThreeWay;
  result.newHeadId = mergeVerId;
  return result;
}

// ---------------------------------------------------------------------------
// Delete (Phase 3: simplified — UUID PK means no re-hashing cascade)
// ---------------------------------------------------------------------------

DeleteResult VersionStore::deleteVersion(const VersionId &versionId) {
  DeleteResult result;

  auto ver = storage_.getVersion(versionId);
  if (!ver) {
    result.error = "Version not found";
    return result;
  }

  // Cannot delete the root version (no parent to re-parent to).
  if (ver->parentId.empty()) {
    result.error = "Cannot delete the root version";
    return result;
  }

  auto children = storage_.getChildVersionIds(versionId);

  // Cannot delete a branch point (2+ children — ambiguous re-parenting).
  if (children.size() > 1) {
    result.error =
        "Cannot delete a branch point (version has multiple children)";
    return result;
  }

  if (children.empty()) {
    // ── Leaf deletion ────────────────────────────────────────────────────────
    // Move the branch head to the parent.
    BranchId owningBranchId;
    auto branches = storage_.listBranches();
    for (const auto &[branchId, branchName, headId] : branches) {
      if (headId == versionId) {
        owningBranchId = branchId;
        storage_.updateBranchHead(branchId, ver->parentId);
      }
    }
    storage_.removeVersion(versionId);

    // Auto-delete the branch if it is now "root-only" (its new head has no
    // parent, i.e., it IS the root version).  Don't delete the last branch.
    if (!owningBranchId.empty()) {
      auto newHead = storage_.getVersion(ver->parentId);
      bool isRootOnly = newHead && newHead->parentId.empty();
      auto survivingBranches = storage_.listBranches();
      bool isLastBranch = (survivingBranches.size() <= 1);

      if (isRootOnly && !isLastBranch) {
        // deleteBranch only removes versions exclusively reachable through this
        // branch. The root is shared, so it will be preserved.
        deleteBranch(owningBranchId);
        result.deletedBranchId = owningBranchId;
      }
    }

    result.success = true;
    return result;
  }

  // ── Middle-node deletion (exactly one child) ─────────────────────────────
  // Because versions now use random UUID PKs (not content hashes), we can
  // directly update the child's parent reference — no cascade re-hashing.
  VersionId childId = children[0];
  storage_.updateVersionParent(childId, ver->parentId);

  // Update any branch heads that pointed to versionId (edge case: shouldn't
  // normally happen for a middle node, but handle robustly).
  auto branches = storage_.listBranches();
  for (const auto &[branchId, branchName, headId] : branches) {
    if (headId == versionId) {
      storage_.updateBranchHead(branchId, ver->parentId);
    }
  }

  storage_.removeVersion(versionId);
  result.success = true;
  return result;
}

// ---------------------------------------------------------------------------
// Import from Dorico blob
// ---------------------------------------------------------------------------

VersionId
VersionStore::importFromBlob(const FiddleState &state,
                             const std::vector<StripBlob> &stripBlobs,
                             const std::vector<VersionId> &ancestorChain,
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
  VersionId closestAncestor;
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
        VersionId current = mainBranch->second;
        while (!current.empty()) {
          auto v = storage_.getVersion(current);
          if (!v || v->parentId.empty()) {
            closestAncestor = current;
            break;
          }
          current = v->parentId;
        }
      }
    }
  }

  // Resolve branch: create if needed
  std::string effectiveName = branchName;
  BranchId branchId;

  auto existingBranch = storage_.findBranchByName(branchName);
  if (existingBranch) {
    branchId = *existingBranch;
  } else {
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

  // Create the new version with a random UUID
  VersionId importVerId = generateUUID();
  Version importVer;
  importVer.id = importVerId;
  importVer.stateHash = stateHash;
  importVer.branchId = branchId;
  importVer.parentId = closestAncestor;

  storage_.putVersion(importVerId, importVer);

  if (existingBranch) {
    storage_.updateBranchHead(branchId, importVerId);
  } else {
    storage_.putBranch(branchId, effectiveName, importVerId);
  }

  return importVerId;
}

} // namespace fiddle::versioning
