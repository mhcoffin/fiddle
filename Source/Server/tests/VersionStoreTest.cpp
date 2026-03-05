#include "../VersionStore.h"
#include "InMemoryVersionStorage.h"

#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness (same style as ExpressionMapParserTest)
// ---------------------------------------------------------------------------

static int gTestsPassed = 0;
static int gTestsFailed = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  FAIL: " << #cond << " (" << __FILE__ << ":"             \
                << __LINE__ << ")" << std::endl;                               \
      gTestsFailed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define CHECK_EQ(a, b)                                                         \
  do {                                                                         \
    if ((a) != (b)) {                                                          \
      std::cerr << "  FAIL: " << #a << " == " << #b << " (" << __FILE__       \
                << ":" << __LINE__ << ")" << std::endl;                        \
      std::cerr << "    got: " << (a) << " vs " << (b) << std::endl;          \
      gTestsFailed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define RUN_TEST(fn)                                                           \
  do {                                                                         \
    std::cout << "Running " << #fn << "..." << std::flush;                     \
    fn();                                                                       \
    std::cout << " PASS" << std::endl;                                         \
    gTestsPassed++;                                                            \
  } while (0)

using namespace fiddle::versioning;

// ---------------------------------------------------------------------------
// Helper: create a strip blob with a given UUID and library name
// ---------------------------------------------------------------------------

static StripBlob makeStrip(const std::string &uuid,
                           const std::string &library = "TestLib") {
  StripBlob blob;
  blob.uuid = uuid;
  blob.library = library;
  blob.family = "Strings";
  blob.isSolo = true;
  blob.inputPort = 0;
  blob.inputChannel = 1;
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

  Hash rootHash = store.initializeEmpty();
  CHECK(!rootHash.empty());

  // Should have exactly one branch ("Main")
  auto branches = storage.listBranches();
  CHECK_EQ((int)branches.size(), 1);

  auto &[id, name, headHash] = branches[0];
  CHECK_EQ(name, "Main");
  CHECK_EQ(headHash, rootHash);

  // Root version should have no parent
  auto rootVer = store.getVersion(rootHash);
  CHECK(rootVer.has_value());
  CHECK(rootVer->parentHash.empty());
  CHECK(rootVer->mergeParentHash.empty());

  // Root state should have no strips
  auto rootState = store.getState(rootVer->stateHash);
  CHECK(rootState.has_value());
  CHECK(rootState->stripHashes.empty());
  CHECK_EQ(rootState->globalState.masterGainDb, 0.0f);
}

void testCommit() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  // Create a strip and commit
  StripBlob strip = makeStrip("violin-1");
  Hash stripHash = storeStrip(storage, strip);
  FiddleState state = makeState({stripHash});

  Hash v1 = store.commitVersion(mainId, state);
  CHECK(!v1.empty());
  CHECK(v1 != rootHash);

  // v1's parent should be root
  auto v1Ver = store.getVersion(v1);
  CHECK(v1Ver.has_value());
  CHECK_EQ(v1Ver->parentHash, rootHash);
  CHECK(v1Ver->mergeParentHash.empty());

  // Branch head should be v1
  auto head = store.getBranchHead(mainId);
  CHECK(head.has_value());
  CHECK_EQ(*head, v1);
}

void testMultipleCommits() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash v2 = store.commitVersion(mainId, makeState({s1, s2}));

  Hash s3 = storeStrip(storage, makeStrip("cello-1"));
  Hash v3 = store.commitVersion(mainId, makeState({s1, s2, s3}));

  // Check chain: root → v1 → v2 → v3
  auto v3Ver = store.getVersion(v3);
  CHECK_EQ(v3Ver->parentHash, v2);
  auto v2Ver = store.getVersion(v2);
  CHECK_EQ(v2Ver->parentHash, v1);
  auto v1Ver = store.getVersion(v1);
  CHECK_EQ(v1Ver->parentHash, rootHash);

  // Ancestor chain
  auto chain = store.getAncestorChain(v3);
  CHECK_EQ((int)chain.size(), 4);
  CHECK_EQ(chain[0], rootHash);
  CHECK_EQ(chain[3], v3);
}

void testBranching() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

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
  CHECK_EQ(devHeadVer->parentHash, v1);
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
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  // Create a dev branch and commit on it
  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash v2 = store.commitVersion(devBranch, makeState({s1, s2}));

  // Delete dev branch
  auto [ok, err] = store.deleteBranch(devBranch);
  CHECK(ok);

  // Should have 1 branch now
  branches = storage.listBranches();
  CHECK_EQ((int)branches.size(), 1);

  // v2 should be gone (only reachable from dev)
  CHECK(!storage.hasVersion(v2));

  // v1 should still exist (reachable from Main)
  CHECK(storage.hasVersion(v1));
}

void testBranchDeleteWarning() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  // Create unmerged dev branch
  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
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

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  // Branch from v1, commit on dev
  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash v2 = store.commitVersion(devBranch, makeState({s1, s2}));

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

  // Setup: commit with violin on main
  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  // Branch from v1
  BranchId devBranch = store.createBranch("Dev", v1);

  // Commit on dev: add viola
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash devV = store.commitVersion(devBranch, makeState({s1, s2}));

  // Commit on main: add cello
  Hash s3 = storeStrip(storage, makeStrip("cello-1"));
  Hash mainV = store.commitVersion(mainId, makeState({s1, s3}));

  // Merge dev onto main — three-way merge
  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  // Result should have all three strips: violin, cello, viola
  auto newVer = store.getVersion(result.newHeadHash);
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

  // Setup: commit with violin on main
  Hash s1 = storeStrip(storage, makeStrip("violin-1", "OrigLib"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  // Branch from v1
  BranchId devBranch = store.createBranch("Dev", v1);

  // Both branches modify the same strip (same UUID, different settings)
  StripBlob mainMod = makeStrip("violin-1", "MainLib");
  Hash mainModHash = storeStrip(storage, mainMod);
  store.commitVersion(mainId, makeState({mainModHash}));

  StripBlob devMod = makeStrip("violin-1", "DevLib");
  Hash devModHash = storeStrip(storage, devMod);
  store.commitVersion(devBranch, makeState({devModHash}));

  // Merge dev (source/MP) onto main — MP wins
  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadHash)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 1);

  // The surviving strip should be DevLib (MP wins)
  auto blob = store.getStripBlob(newState->stripHashes[0]);
  CHECK(blob.has_value());
  CHECK_EQ(blob->library, "DevLib");
}

void testMergeStripAdded() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);

  // Dev adds a new strip
  Hash s2 = storeStrip(storage, makeStrip("new-strip"));
  store.commitVersion(devBranch, makeState({s1, s2}));

  // Main doesn't change
  store.commitVersion(mainId, makeState({s1}));

  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadHash)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 2);
}

void testMergeStripDeleted() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1, s2}));

  BranchId devBranch = store.createBranch("Dev", v1);

  // Dev removes viola
  store.commitVersion(devBranch, makeState({s1}));

  // Main doesn't change
  store.commitVersion(mainId, makeState({s1, s2}));

  auto result = store.merge(devBranch, mainId);
  CHECK_EQ((int)result.kind, (int)MergeResult::ThreeWay);

  auto newState = store.getState(store.getVersion(result.newHeadHash)->stateHash);
  CHECK_EQ((int)newState->stripHashes.size(), 1);

  auto blob = store.getStripBlob(newState->stripHashes[0]);
  CHECK_EQ(blob->uuid, "violin-1");
}

void testMergeAncestorBlocked() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  store.commitVersion(devBranch, makeState({s1, s2}));

  // Try to merge main (ancestor) onto dev — should be blocked
  auto result = store.merge(mainId, devBranch);
  CHECK_EQ((int)result.kind, (int)MergeResult::Error);
}

void testSquash() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash v2 = store.commitVersion(mainId, makeState({s1, s2}));

  Hash s3 = storeStrip(storage, makeStrip("cello-1"));
  Hash v3 = store.commitVersion(mainId, makeState({s1, s2, s3}));

  // Squash v2 (middle node): root → v1 → v2 → v3 becomes root → v1 → v3'
  auto result = store.squash(v2);
  CHECK(result.success);

  // v2 should be gone
  CHECK(!storage.hasVersion(v2));

  // v3 was re-parented — the old v3 hash is gone, but a new version with
  // v1 as parent should exist and be the branch head
  auto head = store.getBranchHead(mainId);
  CHECK(head.has_value());
  auto headVer = store.getVersion(*head);
  CHECK(headVer.has_value());
  CHECK_EQ(headVer->parentHash, v1);

  // The state should still be the same as v3's state
  auto state = store.getState(headVer->stateHash);
  CHECK(state.has_value());
  CHECK_EQ((int)state->stripHashes.size(), 3);
}

void testSquashWithMergeParentBlocked() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);
  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  store.commitVersion(devBranch, makeState({s1, s2}));

  store.commitVersion(mainId, makeState({s1}));

  // Merge creates a node with a merge-parent
  auto mergeResult = store.merge(devBranch, mainId);
  CHECK_EQ((int)mergeResult.kind, (int)MergeResult::ThreeWay);

  // Commit again so the merge node has a child
  Hash s3 = storeStrip(storage, makeStrip("cello-1"));
  store.commitVersion(mainId, makeState({s1, s2, s3}));

  // Try to squash the merge node — should fail
  auto result = store.squash(mergeResult.newHeadHash);
  CHECK(!result.success);
}

void testSquashMultiChildBlocked() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  Hash s2 = storeStrip(storage, makeStrip("viola-1"));
  Hash v2 = store.commitVersion(mainId, makeState({s1, s2}));

  // Create two children of v2
  Hash s3 = storeStrip(storage, makeStrip("cello-1"));
  store.commitVersion(mainId, makeState({s1, s2, s3}));

  BranchId devBranch = store.createBranch("Dev", v2);

  // v2 now has 2 children — squash should fail
  auto result = store.squash(v2);
  CHECK(!result.success);
}

void testAncestorChain() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));
  Hash v2 = store.commitVersion(mainId, makeState({s1}));
  Hash v3 = store.commitVersion(mainId, makeState({s1}));

  auto chain = store.getAncestorChain(v3);
  CHECK_EQ((int)chain.size(), 4);
  CHECK_EQ(chain[0], rootHash);
  CHECK_EQ(chain[1], v1);
  CHECK_EQ(chain[2], v2);
  CHECK_EQ(chain[3], v3);
}

void testCommonAncestor() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  auto branches = storage.listBranches();
  BranchId mainId = std::get<0>(branches[0]);

  Hash s1 = storeStrip(storage, makeStrip("violin-1"));
  Hash v1 = store.commitVersion(mainId, makeState({s1}));

  BranchId devBranch = store.createBranch("Dev", v1);

  Hash devV = store.commitVersion(devBranch, makeState({s1}));
  Hash mainV = store.commitVersion(mainId, makeState({s1}));

  auto lca = store.findCommonAncestor(mainV, devV);
  CHECK(lca.has_value());
  CHECK_EQ(*lca, v1);
}

void testImportFromBlob() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  // Simulate a Dorico load with new data
  StripBlob strip = makeStrip("imported-violin");
  Hash stripHash = strip.computeHash();
  FiddleState importedState = makeState({stripHash});

  Hash importedHash =
      store.importFromBlob(importedState, {strip}, {rootHash}, "Main");
  CHECK(!importedHash.empty());

  // The imported version should exist
  auto ver = store.getVersion(importedHash);
  CHECK(ver.has_value());

  // Its parent should be rootHash (closest existing ancestor)
  CHECK_EQ(ver->parentHash, rootHash);

  // The strip blob should be in storage
  CHECK(storage.hasStripBlob(stripHash));
}

void testImportNoBranchConflict() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  StripBlob strip = makeStrip("imported-violin");
  Hash stripHash = strip.computeHash();
  FiddleState state = makeState({stripHash});

  // Import with a branch name that doesn't exist yet
  Hash h = store.importFromBlob(state, {strip}, {rootHash}, "Baroque");
  CHECK(!h.empty());

  // Should have created a "Baroque" branch
  auto baroqueId = storage.findBranchByName("Baroque");
  CHECK(baroqueId.has_value());
}

void testImportNoAncestor() {
  InMemoryVersionStorage storage;
  VersionStore store(storage);
  Hash rootHash = store.initializeEmpty();

  StripBlob strip = makeStrip("imported-violin");
  Hash stripHash = strip.computeHash();
  FiddleState state = makeState({stripHash});

  // Import with an ancestor chain that doesn't match anything in storage
  Hash h = store.importFromBlob(state, {strip},
                                {"nonexistent-hash-1", "nonexistent-hash-2"},
                                "NewBranch");
  CHECK(!h.empty());

  // Parent should be the root (fallback)
  auto ver = store.getVersion(h);
  CHECK(ver.has_value());
  CHECK_EQ(ver->parentHash, rootHash);
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
  RUN_TEST(testMergeStripAdded);
  RUN_TEST(testMergeStripDeleted);
  RUN_TEST(testMergeAncestorBlocked);
  RUN_TEST(testSquash);
  RUN_TEST(testSquashWithMergeParentBlocked);
  RUN_TEST(testSquashMultiChildBlocked);
  RUN_TEST(testAncestorChain);
  RUN_TEST(testCommonAncestor);
  RUN_TEST(testImportFromBlob);
  RUN_TEST(testImportNoBranchConflict);
  RUN_TEST(testImportNoAncestor);

  std::cout << std::endl;
  std::cout << "Passed: " << gTestsPassed << std::endl;
  std::cout << "Failed: " << gTestsFailed << std::endl;

  return gTestsFailed > 0 ? 1 : 0;
}
