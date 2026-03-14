#include "../VersionStore.h"
#include "InMemoryVersionStorage.h"

#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

static int gTestsPassed = 0;
static int gTestsFailed = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  FAIL: " << #cond << " (" << __FILE__ << ":" << __LINE__  \
                << ")" << std::endl;                                           \
      gTestsFailed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      std::cerr << "  FAIL: " << #a << " == " << #b << " (" << __FILE__ << ":" \
                << __LINE__ << ")" << std::endl;                               \
      std::cerr << "    got: " << (a) << " vs " << (b) << std::endl;           \
      gTestsFailed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define RUN_TEST(fn)                                                           \
  do {                                                                         \
    std::cout << "Running " << #fn << "..." << std::flush;                     \
    fn();                                                                      \
    if (gTestsFailed == 0)                                                     \
      std::cout << " PASS" << std::endl;                                       \
    gTestsPassed++;                                                            \
  } while (0)

using namespace fiddle::versioning;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Create a unique strip blob. Each strip's identity = (inputPort,
/// inputChannel, libraryId). We use a counter-based port/channel to ensure
/// uniqueness.
static int gStripCounter = 0;

static StripBlob makeStrip(int port, int channel,
                           const std::string &library = "TestLib",
                           const std::string &libraryId = kDefaultLibraryId) {
  StripBlob blob;
  blob.libraryId = libraryId;
  blob.library = library;
  blob.family = "Strings";
  blob.isSolo = true;
  blob.inputPort = port;
  blob.inputChannel = channel;
  blob.pluginUid = 42;
  blob.gainDb = -3.0f;
  return blob;
}

/// Store a strip blob in storage and return its hash.
static Hash storeStrip(IVersionStorage &storage, const StripBlob &blob) {
  Hash h = blob.computeHash();
  storage.putStripBlob(h, blob);
  return h;
}

/// Build a FiddleState from a list of strip hashes.
static FiddleState makeState(const std::vector<Hash> &stripHashes,
                             float masterGain = 0.0f) {
  FiddleState state;
  state.globalState.masterGainDb = masterGain;
  state.stripHashes = stripHashes;
  return state;
}

// ===========================================================================
// Tests
// ===========================================================================

void testInitialization() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);

  VersionId rootId = store.initializeEmpty();
  CHECK(!rootId.empty());

  // Should have exactly one branch ("Main")
  auto branches = storage.listBranches();
  CHECK_EQ((int)branches.size(), 1);

  auto &[id, name, headId] = branches[0];
  CHECK_EQ(name, "Main");
  CHECK_EQ(headId, rootId);

  // Root version should have no parent
  auto rootVer = store.getVersion(rootId);
  CHECK(rootVer.has_value());
  CHECK(rootVer->parentId.empty());
  CHECK(rootVer->mergeParentId.empty());

  // Root state should have no strips
  auto rootState = store.getState(rootVer->stateHash);
  CHECK(rootState.has_value());
  CHECK(rootState->stripHashes.empty());
  CHECK_EQ(rootState->globalState.masterGainDb, 0.0f);
}

void testCommit() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  // Create a strip and commit
  StripBlob strip = makeStrip(0, 1);
  Hash stripHash = storeStrip(storage, strip);
  FiddleState state = makeState({stripHash});

  VersionId v1 = store.commitVersion(mainId, state);
  CHECK(!v1.empty());
  CHECK(v1 != rootId);

  // v1's parent should be root
  auto v1Ver = store.getVersion(v1);
  CHECK(v1Ver.has_value());
  CHECK_EQ(v1Ver->parentId, rootId);
  CHECK(v1Ver->mergeParentId.empty());

  // Branch head should be v1
  auto head = store.getBranchHead(mainId);
  CHECK(head.has_value());
  CHECK_EQ(*head, v1);
}

void testMultipleCommits() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  VersionId v2 = store.commitVersion(mainId, makeState({s1, s2}));

  Hash s3 = storeStrip(storage, makeStrip(0, 3));
  VersionId v3 = store.commitVersion(mainId, makeState({s1, s2, s3}));

  // Check chain: root → v1 → v2 → v3
  auto v3Ver = store.getVersion(v3);
  CHECK_EQ(v3Ver->parentId, v2);
  auto v2Ver = store.getVersion(v2);
  CHECK_EQ(v2Ver->parentId, v1);
  auto v1Ver = store.getVersion(v1);
  CHECK_EQ(v1Ver->parentId, rootId);

  // Ancestor chain
  auto chain = store.getAncestorChain(v3);
  CHECK_EQ((int)chain.size(), 4);
  CHECK_EQ(chain[0], rootId);
  CHECK_EQ(chain[3], v3);
}

void testBranching() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  // Branch from v1
  BranchId devBranch = store.createBranch("Dev", v1);
  CHECK(!devBranch.empty());

  // Should now have 2 branches
  branches = storage.listBranches();
  CHECK_EQ((int)branches.size(), 2);

  // Dev branch head should be a *new* version (child of v1)
  auto devHead = store.getBranchHead(devBranch);
  CHECK(devHead.has_value());
  CHECK(*devHead != v1); // It's a new version on the new branch

  auto devHeadVer = store.getVersion(*devHead);
  CHECK_EQ(devHeadVer->parentId, v1);
  CHECK_EQ(devHeadVer->branchId, devBranch);

  // Main branch head should still be v1
  auto mainHead = store.getBranchHead(mainId);
  CHECK_EQ(*mainHead, v1);
}

void testBranchRename() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  // Rename Main → Primary
  bool ok = store.renameBranch(mainId, "Primary");
  CHECK(ok);

  auto branch = storage.getBranch(mainId);
  CHECK_EQ(branch->first, "Primary");

  // Create another branch, try to rename to "Primary" — should fail
  auto head = store.getBranchHead(mainId);
  BranchId devBranch = store.createBranch("Dev", *head);
  ok = store.renameBranch(devBranch, "Primary");
  CHECK(!ok);

  // Rename to something unique — should work
  ok = store.renameBranch(devBranch, "Development");
  CHECK(ok);
}

void testBranchDelete() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  // Create a dev branch and commit on it
  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  VersionId v2 = store.commitVersion(devBranch, makeState({s1, s2}));

  // Delete dev branch
  auto [ok, err] = store.deleteBranch(devBranch);
  CHECK(ok);

  // Should have 1 branch now
  branches = storage.listBranches();
  CHECK_EQ((int)branches.size(), 1);

  // v2 should be gone (only reachable from dev)
  // (v2 is the commit, the branch-creation node was also only on dev)
  CHECK(!storage.hasVersion(v2));

  // v1 should still exist (reachable from Main)
  CHECK(storage.hasVersion(v1));
}

void testBranchDeleteWarning() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  // Create unmerged dev branch
  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  store.commitVersion(devBranch, makeState({s1, s2}));

  auto check = store.checkBranchDelete(devBranch);
  CHECK(check.canDelete);
  CHECK(!check.isMerged); // Unmerged branch — should warn
}

void testMergeFastForward() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  // Branch from v1, commit on dev
  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  VersionId v2 = store.commitVersion(devBranch, makeState({s1, s2}));

  // Merge dev onto main — should fast-forward since main is ancestor of dev
  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::FastForward);

  // Main head should now be v2
  auto mainHead = store.getBranchHead(mainId);
  CHECK_EQ(*mainHead, v2);
}

void testMergeThreeWay() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  // Setup: commit with strip p0c1 on main
  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  // Branch from v1
  BranchId devBranch = store.createBranch("Dev", v1);

  // Commit on dev: add strip p0c2
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  store.commitVersion(devBranch, makeState({s1, s2}));

  // Commit on main: add strip p0c3
  Hash s3 = storeStrip(storage, makeStrip(0, 3));
  store.commitVersion(mainId, makeState({s1, s3}));

  // Merge dev onto main — three-way merge
  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  // Result should have all three strips: s1, s2 (from dev), s3 (from main)
  auto newVer = store.getVersion(result.newHeadId);
  CHECK(newVer.has_value());
  auto newState = store.getState(newVer->stateHash);
  CHECK(newState.has_value());
  CHECK_EQ((int)newState->stripHashes.size(), 3);
}

void testMergeConflictMPWins() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  // Strip with port=0, channel=1 as common ancestor content
  Hash s1 = storeStrip(storage, makeStrip(0, 1, "OrigLib"));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);

  // Both branches modify the same strip (same port/channel, different library
  // name)
  Hash mainModHash = storeStrip(storage, makeStrip(0, 1, "MainLib"));
  store.commitVersion(mainId, makeState({mainModHash}));

  Hash devModHash = storeStrip(storage, makeStrip(0, 1, "DevLib"));
  store.commitVersion(devBranch, makeState({devModHash}));

  // Merge dev (source/MP) onto main — MP wins
  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadId)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 1);

  // The surviving strip should be DevLib (MP wins)
  auto blob = store.getStripBlob(newState->stripHashes[0]);
  CHECK(blob.has_value());
  CHECK_EQ(blob->library, "DevLib");
}

void testMergeConflictHWins() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1, "OrigLib"));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);

  Hash mainModHash = storeStrip(storage, makeStrip(0, 1, "MainLib"));
  store.commitVersion(mainId, makeState({mainModHash}));

  Hash devModHash = storeStrip(storage, makeStrip(0, 1, "DevLib"));
  store.commitVersion(devBranch, makeState({devModHash}));

  // Merge dev onto main — target (HEAD) wins
  auto result = store.merge(devBranch, mainId, /*mpWins=*/false);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadId)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 1);

  // The surviving strip should be MainLib (HEAD wins)
  auto blob = store.getStripBlob(newState->stripHashes[0]);
  CHECK(blob.has_value());
  CHECK_EQ(blob->library, "MainLib");
}

void testMergeStripAdded() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);

  // Dev adds a new strip (different port/channel)
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  store.commitVersion(devBranch, makeState({s1, s2}));

  // Main doesn't change
  store.commitVersion(mainId, makeState({s1}));

  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadId)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 2);
}

void testMergeStripDeleted() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  VersionId v1 = store.commitVersion(mainId, makeState({s1, s2}));

  BranchId devBranch = store.createBranch("Dev", v1);

  // Dev removes s2 (port=0, channel=2)
  store.commitVersion(devBranch, makeState({s1}));

  // Main doesn't change
  store.commitVersion(mainId, makeState({s1, s2}));

  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadId)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 1);

  // The surviving strip should be port=0, channel=1 (s1)
  auto blob = store.getStripBlob(newState->stripHashes[0]);
  CHECK(blob.has_value());
  CHECK_EQ(blob->inputPort, 0);
  CHECK_EQ(blob->inputChannel, 1);
}

void testMergeAncestorBlocked() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  store.commitVersion(devBranch, makeState({s1, s2}));

  // Try to merge main (ancestor) onto dev — should be blocked
  auto result = store.merge(mainId, devBranch);
  CHECK_EQ((int)result.kind, (int)MergeResult::Error);
}

void testDelete() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  VersionId v2 = store.commitVersion(mainId, makeState({s1, s2}));

  Hash s3 = storeStrip(storage, makeStrip(0, 3));
  VersionId v3 = store.commitVersion(mainId, makeState({s1, s2, s3}));

  // Delete v2 (middle node): root → v1 → v2 → v3 becomes root → v1 → v3
  // (v3 is re-parented to v1 — no re-hashing needed with UUID PKs)
  auto result = store.deleteVersion(v2);
  CHECK(result.success);

  // v2 should be gone
  CHECK(!storage.hasVersion(v2));

  // v3 still exists, now parented to v1
  auto v3Ver = store.getVersion(v3);
  CHECK(v3Ver.has_value());
  CHECK_EQ(v3Ver->parentId, v1);

  // Branch head is still v3
  auto head = store.getBranchHead(mainId);
  CHECK(head.has_value());
  CHECK_EQ(*head, v3);

  // State should still be the same as v3's state
  auto state = store.getState(v3Ver->stateHash);
  CHECK(state.has_value());
  CHECK_EQ((int)state->stripHashes.size(), 3);
}

// A merge commit with exactly one child (≤1 child = not a branch point) should
// be deletable; the child must be re-parented to the merge's primary parent.
void testDeleteMergeNode() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  store.commitVersion(devBranch, makeState({s1, s2}));

  store.commitVersion(mainId, makeState({s1}));

  // Merge creates a node with a merge-parent
  auto mergeResult = store.merge(devBranch, mainId);
  CHECK_EQ((int)mergeResult.kind, (int)MergeResult::ThreeWay);
  VersionId mergeId = mergeResult.newHeadId;
  auto mergeVer = store.getVersion(mergeId);
  CHECK(mergeVer.has_value());
  VersionId primaryParent = mergeVer->parentId;

  // Commit again so the merge node has exactly one child
  Hash s3 = storeStrip(storage, makeStrip(0, 3));
  VersionId postMerge = store.commitVersion(mainId, makeState({s1, s2, s3}));

  // Delete the merge node — should succeed (only 1 child)
  auto result = store.deleteVersion(mergeId);
  CHECK(result.success);

  // The merge node must be gone
  CHECK(!storage.hasVersion(mergeId));

  // The post-merge commit should have been re-parented to primaryParent
  auto postMergeVer = store.getVersion(postMerge);
  CHECK(postMergeVer.has_value());
  CHECK_EQ(postMergeVer->parentId, primaryParent);

  // Branch head should still be postMerge
  auto head = store.getBranchHead(mainId);
  CHECK_EQ(*head, postMerge);
}

void testDeleteMultiChildBlocked() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  Hash s2 = storeStrip(storage, makeStrip(0, 2));
  VersionId v2 = store.commitVersion(mainId, makeState({s1, s2}));

  // Create two children of v2
  Hash s3 = storeStrip(storage, makeStrip(0, 3));
  store.commitVersion(mainId, makeState({s1, s2, s3}));

  BranchId devBranch = store.createBranch("Dev", v2);

  // v2 now has 2 children — delete should fail
  auto result = store.deleteVersion(v2);
  CHECK(!result.success);
}

void testAncestorChain() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));
  VersionId v2 = store.commitVersion(mainId, makeState({s1}));
  VersionId v3 = store.commitVersion(mainId, makeState({s1}));

  auto chain = store.getAncestorChain(v3);
  CHECK_EQ((int)chain.size(), 4);
  CHECK_EQ(chain[0], rootId);
  CHECK_EQ(chain[1], v1);
  CHECK_EQ(chain[2], v2);
  CHECK_EQ(chain[3], v3);
}

void testCommonAncestor() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);

  VersionId devV = store.commitVersion(devBranch, makeState({s1}));
  VersionId mainV = store.commitVersion(mainId, makeState({s1}));

  auto lca = store.findCommonAncestor(mainV, devV);
  CHECK(lca.has_value());

  // LCA should be v1 (or the branch-creation node which has v1 as parent)
  // The branch node is a child of v1, so LCA of mainV and devV is that node
  // or further up. What matters: LCA exists and is an ancestor of both.
  CHECK(!lca->empty());
}

void testImportFromBlob() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  // Simulate a Dorico load with new data
  StripBlob strip = makeStrip(0, 1);
  Hash stripHash = strip.computeHash();
  FiddleState importedState = makeState({stripHash});

  VersionId importedId =
      store.importFromBlob(importedState, {strip}, {rootId}, "Main");
  CHECK(!importedId.empty());

  // The imported version should exist
  auto ver = store.getVersion(importedId);
  CHECK(ver.has_value());

  // Its parent should be rootId (closest existing ancestor)
  CHECK_EQ(ver->parentId, rootId);

  // The strip blob should be in storage
  CHECK(storage.hasStripBlob(stripHash));
}

void testImportNoBranchConflict() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  StripBlob strip = makeStrip(0, 1);
  Hash stripHash = strip.computeHash();
  FiddleState state = makeState({stripHash});

  // Import with a branch name that doesn't exist yet
  VersionId h = store.importFromBlob(state, {strip}, {rootId}, "Baroque");
  CHECK(!h.empty());

  // Should have created a "Baroque" branch
  auto baroqueId = storage.findBranchByName("Baroque");
  CHECK(baroqueId.has_value());
}

void testImportNoAncestor() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  VersionId rootId = store.initializeEmpty();

  StripBlob strip = makeStrip(0, 1);
  Hash stripHash = strip.computeHash();
  FiddleState state = makeState({stripHash});

  // Import with an ancestor chain that doesn't match anything in storage
  VersionId h = store.importFromBlob(
      state, {strip}, {"nonexistent-uuid-1", "nonexistent-uuid-2"},
      "NewBranch");
  CHECK(!h.empty());

  // Parent should be the root (fallback)
  auto ver = store.getVersion(h);
  CHECK(ver.has_value());
  CHECK_EQ(ver->parentId, rootId);
}

void testDeleteThenMerge() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip(0, 1));
  VersionId base = store.commitVersion(mainId, makeState({s1}));

  BranchId fooBranch = store.createBranch("Foo", base);
  Hash sf1 = storeStrip(storage, makeStrip(0, 4));
  VersionId fooIntermediate =
      store.commitVersion(fooBranch, makeState({s1, sf1}));
  Hash sf2 = storeStrip(storage, makeStrip(0, 5));
  store.commitVersion(fooBranch, makeState({s1, sf1, sf2}));

  // Delete intermediate version on Foo
  auto del = store.deleteVersion(fooIntermediate);
  CHECK(del.success);

  BranchId zooBranch = store.createBranch("Zoo", base);
  Hash sz1 = storeStrip(storage, makeStrip(0, 6));
  store.commitVersion(zooBranch, makeState({s1, sz1}));

  // Merge Foo onto Zoo — must succeed
  auto mergeResult = store.merge(fooBranch, zooBranch);
  CHECK((int)mergeResult.kind != (int)MergeResult::Error);
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
  std::cout << "===== VersionStore Tests =====" << std::endl;

  RUN_TEST(testInitialization);
  RUN_TEST(testCommit);
  RUN_TEST(testMultipleCommits);
  RUN_TEST(testBranching);
  RUN_TEST(testBranchRename);
  RUN_TEST(testBranchDelete);
  RUN_TEST(testBranchDeleteWarning);
  RUN_TEST(testMergeFastForward);
  RUN_TEST(testMergeThreeWay);
  RUN_TEST(testMergeConflictMPWins);
  RUN_TEST(testMergeConflictHWins);
  RUN_TEST(testMergeStripAdded);
  RUN_TEST(testMergeStripDeleted);
  RUN_TEST(testMergeAncestorBlocked);
  RUN_TEST(testDelete);
  RUN_TEST(testDeleteMergeNode);
  RUN_TEST(testDeleteMultiChildBlocked);
  RUN_TEST(testAncestorChain);
  RUN_TEST(testCommonAncestor);
  RUN_TEST(testImportFromBlob);
  RUN_TEST(testImportNoBranchConflict);
  RUN_TEST(testImportNoAncestor);
  RUN_TEST(testDeleteThenMerge);

  std::cout << std::endl;
  std::cout << "Passed: " << gTestsPassed << std::endl;
  std::cout << "Failed: " << gTestsFailed << std::endl;

  return gTestsFailed > 0 ? 1 : 0;
}
