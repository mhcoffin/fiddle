#include "../SqliteVersionStorage.h"

#include <iostream>
#include <mutex>
#include <sqlite3.h>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                     \
                << "]: " << #condition << std::endl;                           \
    }                                                                          \
  } while (false)

bool execute(sqlite3 *database, const char *sql) {
  return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

void createSchema(sqlite3 *database) {
  // Deliberately use the legacy strip_blobs shape. Constructing the storage
  // adapter must migrate it before preparing its statements.
  CHECK(execute(database,
                "CREATE TABLE strip_blobs (hash TEXT PRIMARY KEY, library_id "
                "TEXT, library TEXT, family TEXT, is_solo INTEGER, input_port "
                "INTEGER, input_channel INTEGER, plugin_uid INTEGER, gain_db "
                "REAL, expression_map TEXT, plugin_state BLOB)"));
  CHECK(execute(database,
                "CREATE TABLE fiddle_states (hash TEXT PRIMARY KEY, "
                "master_gain REAL, strip_hashes TEXT, audio_schema INTEGER, "
                "master_state BLOB)"));
  CHECK(execute(database,
                "CREATE TABLE versions (id TEXT PRIMARY KEY, state_hash TEXT, "
                "branch_id TEXT, parent_id TEXT, merge_parent_id TEXT, "
                "created_at TEXT DEFAULT CURRENT_TIMESTAMP)"));
  CHECK(execute(database,
                "CREATE TABLE branches (id TEXT PRIMARY KEY, name TEXT, "
                "head_id TEXT)"));
  CHECK(execute(database,
                "CREATE TABLE libraries (id TEXT PRIMARY KEY, name TEXT, "
                "vendor TEXT, variant TEXT)"));
}

void testStripStateRoundTripsThroughMigratedSqlite() {
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
  createSchema(database);

  // A legacy snapshot should retain the behaviour it had before these fields
  // existed: active, unmuted, unsoloed, with no Lua plug-ins.
  CHECK(execute(database,
                "INSERT INTO strip_blobs "
                "(hash, library_id, library, family, is_solo, input_port, "
                "input_channel, plugin_uid, gain_db, expression_map) VALUES "
                "('legacy', 'library-1', 'Test', 'Strings', 1, 0, 1, 42, "
                "-3.0, 'xmap-1')"));

  std::mutex mutex;
  {
    fiddle::versioning::SqliteVersionStorage storage(database, mutex);

    const auto legacy = storage.getStripBlob("legacy");
    CHECK(legacy.has_value());
    CHECK(legacy && legacy->active);
    CHECK(legacy && !legacy->muted);
    CHECK(legacy && !legacy->soloed);
    CHECK(legacy && legacy->luaPluginFileNames.empty());

    fiddle::versioning::StripBlob strip;
    strip.libraryId = "library-2";
    strip.library = "Orchestral Strings";
    strip.family = "Violins";
    strip.isSolo = false;
    strip.active = false;
    strip.muted = true;
    strip.soloed = true;
    strip.inputPort = 2;
    strip.inputChannel = 7;
    strip.pluginUid = 876;
    strip.gainDb = -6.5f;
    strip.expressionMapEntityId = "xmap-2";
    strip.pluginState = {0, 1, 2, 255};
    strip.luaPluginFileNames = {"first.lua", "second.lua"};

    const auto hash = strip.computeHash();
    storage.putStripBlob(hash, strip);
    const auto restored = storage.getStripBlob(hash);
    CHECK(restored.has_value());
    CHECK(restored && !restored->active);
    CHECK(restored && restored->muted);
    CHECK(restored && restored->soloed);
    CHECK(restored &&
          restored->luaPluginFileNames == strip.luaPluginFileNames);
    CHECK(restored && restored->pluginState == strip.pluginState);
    CHECK(restored && restored->computeHash() == hash);
  }

  CHECK(sqlite3_close(database) == SQLITE_OK);
}

void testMuteAndSoloParticipateInStripIdentity() {
  fiddle::versioning::StripBlob base;
  base.libraryId = "library-1";
  base.inputPort = 0;
  base.inputChannel = 1;

  auto muted = base;
  muted.muted = true;
  auto soloed = base;
  soloed.soloed = true;

  CHECK(base.computeHash() != muted.computeHash());
  CHECK(base.computeHash() != soloed.computeHash());
  CHECK(muted.computeHash() != soloed.computeHash());
}

void testMasterAudioRoundTripsThroughSqlite() {
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
  createSchema(database);

  std::mutex mutex;
  {
    fiddle::versioning::SqliteVersionStorage storage(database, mutex);
    fiddle::versioning::FiddleState state;
    state.globalState.audioSchemaVersion = 1;
    state.globalState.masterGainDb = -4.5f;
    state.stripHashes = {"strip-a", "strip-b"};

    fiddle::versioning::PluginSlotBlob effect;
    effect.slotId = "master-fx-1";
    effect.formatName = "VST3";
    effect.uniqueId = 876;
    effect.fileOrIdentifier = "/test/eq.vst3";
    effect.manufacturer = "Test Maker";
    effect.name = "Test EQ";
    effect.category = "EQ";
    effect.pluginVersion = "2.0";
    effect.numInputChannels = 2;
    effect.numOutputChannels = 2;
    effect.bypassed = true;
    effect.pluginState = {0, 1, 2, 255};
    state.globalState.masterInserts.push_back(effect);

    state.routingState.schemaVersion = 1;
    fiddle::versioning::ChairSnapshot chair;
    chair.id = "chair-1";
    chair.instrumentEntityId = "instrument.strings.violin";
    chair.name = "Violin I";
    chair.family = "Strings";
    chair.isSolo = false;
    chair.ordinal = 1;
    chair.displayOrder = 3;
    chair.flatIndex = 17;
    state.routingState.chairs.push_back(chair);

    fiddle::versioning::LayerSnapshot layer;
    layer.id = "layer-1";
    layer.chairId = chair.id;
    layer.patchId = "removed-patch";
    layer.patchName = "Elite Violins I";
    layer.libraryId = "elite";
    layer.libraryName = "Elite Strings";
    layer.position = 2;
    layer.stripHash = "strip-a";
    state.routingState.layers.push_back(layer);

    const auto hash = state.computeHash();
    storage.putFiddleState(hash, state);
    const auto restored = storage.getFiddleState(hash);
    CHECK(restored.has_value());
    CHECK(restored && restored->stripHashes == state.stripHashes);
    CHECK(restored && restored->globalState.masterGainDb == -4.5f);
    CHECK(restored && restored->globalState.audioSchemaVersion == 1);
    CHECK(restored && restored->globalState.masterInserts.size() == 1);
    CHECK(restored &&
          restored->globalState.masterInserts.front().slotId == "master-fx-1");
    CHECK(restored &&
          restored->globalState.masterInserts.front().name == "Test EQ");
    CHECK(restored && restored->globalState.masterInserts.front().bypassed);
    CHECK(restored && restored->globalState.masterInserts.front().pluginState ==
                          effect.pluginState);
    CHECK(restored && restored->routingState.schemaVersion == 1);
    CHECK(restored && restored->routingState.chairs.size() == 1);
    CHECK(restored && !restored->routingState.chairs.front().isSolo);
    CHECK(restored && restored->routingState.chairs.front().flatIndex == 17);
    CHECK(restored && restored->routingState.layers.size() == 1);
    CHECK(restored &&
          restored->routingState.layers.front().patchName ==
              "Elite Violins I");
    CHECK(restored &&
          restored->routingState.layers.front().stripHash == "strip-a");
  }

  CHECK(sqlite3_close(database) == SQLITE_OK);
}

void testGarbageCollectionRetainsEverythingReferencedByVersions() {
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
  createSchema(database);

  std::mutex mutex;
  {
    fiddle::versioning::SqliteVersionStorage storage(database, mutex);

    fiddle::versioning::StripBlob retainedStrip;
    retainedStrip.libraryId = "retained-library";
    retainedStrip.library = "Retained";
    retainedStrip.inputPort = 0;
    retainedStrip.inputChannel = 1;
    retainedStrip.pluginState = {1, 2, 3};
    const auto retainedStripHash = retainedStrip.computeHash();
    storage.putStripBlob(retainedStripHash, retainedStrip);

    fiddle::versioning::StripBlob orphanStrip;
    orphanStrip.libraryId = "orphan-library";
    orphanStrip.library = "Orphan";
    orphanStrip.inputPort = 0;
    orphanStrip.inputChannel = 2;
    orphanStrip.pluginState = {4, 5, 6};
    const auto orphanStripHash = orphanStrip.computeHash();
    storage.putStripBlob(orphanStripHash, orphanStrip);

    fiddle::versioning::FiddleState retainedState;
    retainedState.stripHashes = {retainedStripHash};
    const auto retainedStateHash = retainedState.computeHash();
    storage.putFiddleState(retainedStateHash, retainedState);

    fiddle::versioning::FiddleState orphanState;
    orphanState.stripHashes = {orphanStripHash};
    const auto orphanStateHash = orphanState.computeHash();
    storage.putFiddleState(orphanStateHash, orphanState);

    fiddle::versioning::Version version;
    version.id = "version-1";
    version.stateHash = retainedStateHash;
    version.branchId = "branch-1";
    storage.putVersion(version.id, version);
    storage.putBranch(version.branchId, "Main", version.id);

    const auto result = storage.garbageCollectUnreferencedObjects(true);
    CHECK(result.succeeded());
    CHECK(result.fiddleStatesRemoved == 1);
    CHECK(result.stripBlobsRemoved == 1);
    CHECK(result.compacted);

    CHECK(storage.hasVersion(version.id));
    CHECK(storage.getBranch(version.branchId).has_value());
    CHECK(storage.hasFiddleState(retainedStateHash));
    CHECK(storage.hasStripBlob(retainedStripHash));
    CHECK(!storage.hasFiddleState(orphanStateHash));
    CHECK(!storage.hasStripBlob(orphanStripHash));
  }

  CHECK(sqlite3_close(database) == SQLITE_OK);
}

} // namespace

int main() {
  std::cout << "===== SQLite Version Storage Tests =====\n";
  testStripStateRoundTripsThroughMigratedSqlite();
  testMuteAndSoloParticipateInStripIdentity();
  testMasterAudioRoundTripsThroughSqlite();
  testGarbageCollectionRetainsEverythingReferencedByVersions();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
