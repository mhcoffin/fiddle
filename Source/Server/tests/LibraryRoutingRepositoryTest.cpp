#include "../LibraryRoutingRepository.h"

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

struct DatabaseFixture {
  DatabaseFixture() {
    CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
    CHECK(execute(database, "PRAGMA foreign_keys=ON"));
    CHECK(execute(database,
                  "CREATE TABLE libraries (id TEXT PRIMARY KEY, name TEXT)"));
    CHECK(execute(database,
                  "INSERT INTO libraries VALUES ('vsl', 'Vienna'), "
                  "('ews', 'EastWest')"));
    CHECK(fiddle::LibraryRoutingRepository::ensureSchema(database));
    CHECK(fiddle::LibraryRoutingRepository::ensureSchema(database));
  }

  ~DatabaseFixture() {
    if (database)
      CHECK(sqlite3_close(database) == SQLITE_OK);
  }

  sqlite3 *database = nullptr;
  std::mutex mutex;
};

fiddle::LibraryPatchRow soloViolinPatch() {
  fiddle::LibraryPatchRow patch;
  patch.id = "synchron-solo-violin-1";
  patch.libraryId = "vsl";
  patch.name = "Synchron Solo Violin 1";
  patch.instrumentEntityId = "instrument.strings.violin";
  patch.family = "Strings";
  patch.character = "solo";
  patch.pluginUid = 410;
  patch.pluginState = {1, 2, 3, 4};
  patch.expressionMapId = "xmap.synchron-solo-violin";
  return patch;
}

fiddle::ChairRow makeChair(std::string id, fiddle::DoricoRole role,
                           int ordinal, int flatIndex) {
  fiddle::ChairRow chair;
  chair.id = std::move(id);
  chair.instrumentEntityId = "instrument.strings.violin";
  chair.name = role == fiddle::DoricoRole::solo ? "Solo Violin 1" : "Violin I";
  chair.family = "Strings";
  chair.role = role;
  chair.ordinal = ordinal;
  chair.flatIndex = flatIndex;
  chair.displayOrder = flatIndex;
  return chair;
}

void testPatchCanCreateIndependentLayersOnDifferentChairs() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  const auto patch = soloViolinPatch();
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.upsertChair(
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
  CHECK(repository.upsertChair(
      makeChair("violin-solo-1", fiddle::DoricoRole::solo, 1, 2)));

  // Patch character is descriptive: a solo patch may feed a section chair.
  CHECK(repository.createLayerFromPatch(
      "section-overlay", "violin-section-1", patch.id, 0));
  CHECK(repository.createLayerFromPatch(
      "solo-primary", "violin-solo-1", patch.id, 0));

  auto sectionLayer = repository.getLayer("section-overlay");
  auto soloLayer = repository.getLayer("solo-primary");
  CHECK(sectionLayer.has_value());
  CHECK(soloLayer.has_value());
  CHECK(sectionLayer && soloLayer && sectionLayer->id != soloLayer->id);
  CHECK(sectionLayer && sectionLayer->pluginState == patch.pluginState);
  CHECK(sectionLayer &&
        sectionLayer->expressionMapId == patch.expressionMapId);
  CHECK(sectionLayer && sectionLayer->patchName == patch.name);
  CHECK(sectionLayer && sectionLayer->libraryId == patch.libraryId);
  CHECK(sectionLayer && sectionLayer->libraryName == "Vienna");

  sectionLayer->gainDb = -8.0f;
  sectionLayer->pluginState = {9, 9};
  CHECK(repository.upsertLayer(*sectionLayer));
  soloLayer = repository.getLayer("solo-primary");
  CHECK(soloLayer && soloLayer->gainDb == 0.0f);
  CHECK(soloLayer && soloLayer->pluginState == patch.pluginState);
}

void testOneChairAcceptsSeveralPatchesFromOneLibrary() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  auto first = soloViolinPatch();
  auto second = first;
  second.id = "elite-violin-1";
  second.name = "Elite Violin 1";
  second.character = "section";
  second.pluginState = {5, 6, 7};
  CHECK(repository.upsertPatch(first));
  CHECK(repository.upsertPatch(second));
  CHECK(repository.upsertChair(
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
  CHECK(repository.createLayerFromPatch(
      "layer-a", "violin-section-1", first.id, 0));
  CHECK(repository.createLayerFromPatch(
      "layer-b", "violin-section-1", second.id, 1));

  const auto layers = repository.listLayers("violin-section-1");
  CHECK(layers.size() == 2);
  CHECK(layers.size() == 2 && layers[0].patchId == first.id);
  CHECK(layers.size() == 2 && layers[1].patchId == second.id);
}

void testCatalogPatchCanUpdateEveryLinkedLayerWithoutChangingItsMix() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  auto patch = soloViolinPatch();
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.upsertChair(
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
  CHECK(repository.upsertChair(
      makeChair("violin-solo-1", fiddle::DoricoRole::solo, 1, 2)));
  CHECK(repository.createLayerFromPatch("layer-a", "violin-section-1",
                                        patch.id, 0));
  CHECK(repository.createLayerFromPatch("layer-b", "violin-solo-1",
                                        patch.id, 3));
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);

  auto customized = repository.getLayer("layer-a");
  CHECK(customized.has_value());
  customized->active = false;
  customized->muted = true;
  customized->soloed = true;
  customized->gainDb = -7.5f;
  customized->pluginState = {9, 9};
  customized->expressionMapId = "xmap.local";
  CHECK(repository.upsertLayer(*customized));
  CHECK(repository.outOfDateLayerCount(patch.id) == 1);

  patch.name = "Updated Synchron Solo Violin";
  patch.pluginUid = 911;
  patch.pluginState = {8, 7, 6};
  patch.expressionMapId = "xmap.updated";
  CHECK(repository.upsertPatch(patch));
  const auto currentPatch = repository.getPatch(patch.id);
  CHECK(currentPatch.has_value());
  CHECK(currentPatch && currentPatch->revision == 2);
  CHECK(repository.outOfDateLayerCount(patch.id) == 2);
  const auto staleIds = repository.outOfDateLayerIds();
  CHECK(staleIds.size() == 2);
  CHECK(staleIds.size() == 2 && staleIds[0] == "layer-a");
  CHECK(staleIds.size() == 2 && staleIds[1] == "layer-b");

  auto oneLayerUpdate = repository.getLayer("layer-a");
  CHECK(oneLayerUpdate.has_value());
  oneLayerUpdate->patchName = patch.name;
  oneLayerUpdate->libraryId = patch.libraryId;
  oneLayerUpdate->libraryName = "Vienna";
  oneLayerUpdate->pluginUid = patch.pluginUid;
  oneLayerUpdate->pluginState = patch.pluginState;
  oneLayerUpdate->expressionMapId = patch.expressionMapId;
  oneLayerUpdate->sourcePatchRevision = currentPatch->revision;
  oneLayerUpdate->pluginStateEdited = false;
  CHECK(repository.upsertLayer(*oneLayerUpdate));
  const auto refreshedOne = repository.getLayer("layer-a");
  CHECK(refreshedOne.has_value());
  CHECK(refreshedOne && refreshedOne->pluginUid == patch.pluginUid);
  CHECK(refreshedOne && refreshedOne->pluginState == patch.pluginState);
  CHECK(refreshedOne && refreshedOne->expressionMapId == patch.expressionMapId);
  CHECK(refreshedOne && !refreshedOne->active);
  CHECK(refreshedOne && refreshedOne->muted);
  CHECK(refreshedOne && refreshedOne->soloed);
  CHECK(refreshedOne && refreshedOne->gainDb == -7.5f);
  CHECK(refreshedOne && refreshedOne->position == 0);
  const auto untouched = repository.getLayer("layer-b");
  CHECK(untouched && untouched->pluginUid == 410);
  CHECK(untouched && untouched->pluginState ==
                         std::vector<std::uint8_t>({1, 2, 3, 4}));
  CHECK(repository.outOfDateLayerCount(patch.id) == 1);
  CHECK(repository.outOfDateLayerIds() ==
        std::vector<std::string>({"layer-b"}));
  const auto updateCount = repository.updateLayersFromPatch(patch.id);
  CHECK(updateCount && *updateCount == 2);
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);
  const auto updated = repository.getLayer("layer-a");
  CHECK(updated.has_value());
  CHECK(updated && updated->patchName == patch.name);
  CHECK(updated && updated->libraryId == patch.libraryId);
  CHECK(updated && updated->libraryName == "Vienna");
  CHECK(updated && updated->pluginUid == patch.pluginUid);
  CHECK(updated && updated->pluginState == patch.pluginState);
  CHECK(updated && updated->expressionMapId == patch.expressionMapId);
  CHECK(updated && !updated->active);
  CHECK(updated && updated->muted);
  CHECK(updated && updated->soloed);
  CHECK(updated && updated->gainDb == -7.5f);
  CHECK(updated && updated->position == 0);

  // A plug-in may rewrite an equivalent state blob merely by being loaded.
  // That must not make a synchronized layer look stale.
  auto reserialized = *updated;
  reserialized.pluginState = {42, 43, 44, 45};
  CHECK(repository.upsertLayer(reserialized));
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);

  // A genuine player edit is tracked explicitly, without relying on opaque
  // serialized bytes being stable.
  CHECK(repository.markLayerPluginStateEdited("layer-a"));
  CHECK(repository.outOfDateLayerCount(patch.id) == 1);
  CHECK(repository.updateLayersFromPatch(patch.id).has_value());
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);
  CHECK(!repository.updateLayersFromPatch("missing-patch").has_value());
}

void testReferencedPatchDeletionIsBlocked() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  const auto patch = soloViolinPatch();
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.upsertChair(
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
  CHECK(repository.createLayerFromPatch(
      "layer-a", "violin-section-1", patch.id, 0));
  CHECK(repository.patchUsageCount(patch.id) == 1);
  CHECK(repository.deletePatch(patch.id) ==
        fiddle::PatchDeleteResult::referenced);
  CHECK(repository.getPatch(patch.id).has_value());

  CHECK(repository.deleteLayer("layer-a"));
  CHECK(repository.deletePatch(patch.id) ==
        fiddle::PatchDeleteResult::deleted);
  CHECK(!repository.getPatch(patch.id).has_value());
  CHECK(repository.deletePatch(patch.id) ==
        fiddle::PatchDeleteResult::notFound);
}

void testLibraryPatchReplacementIsAtomicAndReferenceSafe() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  auto referenced = soloViolinPatch();
  auto retained = referenced;
  retained.id = "retained";
  retained.name = "Retained";
  CHECK(repository.upsertPatch(referenced));
  CHECK(repository.upsertPatch(retained));
  CHECK(repository.upsertChair(
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
  CHECK(repository.createLayerFromPatch(
      "layer-a", "violin-section-1", referenced.id, 0));

  retained.name = "Edited but not partially saved";
  CHECK(repository.replaceLibraryPatches("vsl", {retained}) ==
        fiddle::PatchReplaceResult::referenced);
  CHECK(repository.getPatch(referenced.id).has_value());
  const auto unchanged = repository.getPatch(retained.id);
  CHECK(unchanged && unchanged->name == "Retained");

  CHECK(repository.deleteLayer("layer-a"));
  CHECK(repository.replaceLibraryPatches("vsl", {retained}) ==
        fiddle::PatchReplaceResult::replaced);
  CHECK(!repository.getPatch(referenced.id).has_value());
  const auto edited = repository.getPatch(retained.id);
  CHECK(edited && edited->name == retained.name);

  auto foreign = retained;
  foreign.id = "foreign";
  foreign.libraryId = "ews";
  CHECK(repository.upsertPatch(foreign));
  foreign.libraryId = "vsl";
  CHECK(repository.replaceLibraryPatches("vsl", {retained, foreign}) ==
        fiddle::PatchReplaceResult::error);
  CHECK(repository.getPatch("foreign") &&
        repository.getPatch("foreign")->libraryId == "ews");
}

void testDeletingChairExplicitlyDeletesItsLayersOnly() {
  DatabaseFixture fixture;
  const auto patch = soloViolinPatch();
  {
    fiddle::LibraryRoutingRepository repository(fixture.database,
                                                 fixture.mutex);
    CHECK(repository.upsertPatch(patch));
    CHECK(repository.upsertChair(
        makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
    CHECK(repository.createLayerFromPatch(
        "layer-a", "violin-section-1", patch.id, 0));
    CHECK(repository.deleteChairAndLayers("violin-section-1"));
    CHECK(!repository.getChair("violin-section-1").has_value());
    CHECK(!repository.getLayer("layer-a").has_value());
    CHECK(repository.getPatch(patch.id).has_value());
  }

  // Stable IDs and rows survive repository reconstruction on the same DB.
  fiddle::LibraryRoutingRepository reopened(fixture.database, fixture.mutex);
  CHECK(reopened.getPatch(patch.id).has_value());
}

void testChairDestinationsAndChannelsAreUnique() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  CHECK(repository.upsertChair(
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1)));
  CHECK(!repository.upsertChair(
      makeChair("duplicate-destination", fiddle::DoricoRole::section, 1, 2)));
  CHECK(!repository.upsertChair(
      makeChair("duplicate-channel", fiddle::DoricoRole::solo, 1, 1)));
}

void testChairAllocationIsStableAndReclaimsMatchingDestination() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);

  auto first = makeChair("violin-section-1", fiddle::DoricoRole::section,
                         99, 99);
  CHECK(repository.insertChairWithStableAssignment(first));
  CHECK(first.ordinal == 1);
  CHECK(first.displayOrder == 0);
  CHECK(first.flatIndex == 1);

  auto second = makeChair("violin-section-2", fiddle::DoricoRole::section,
                          99, 99);
  second.name = "Violin II";
  CHECK(repository.insertChairWithStableAssignment(second));
  CHECK(second.ordinal == 2);
  CHECK(second.flatIndex == 2);

  CHECK(repository.deleteChairAndLayers(first.id));

  auto cello = first;
  cello.id = "cello-section-1";
  cello.instrumentEntityId = "instrument.strings.cello";
  cello.name = "Cello";
  CHECK(repository.insertChairWithStableAssignment(cello));
  CHECK(cello.flatIndex == 3);

  auto replacement = first;
  replacement.id = "violin-section-replacement";
  CHECK(repository.insertChairWithStableAssignment(replacement));
  CHECK(replacement.ordinal == 1);
  CHECK(replacement.flatIndex == 1);
  const auto persistedSecond = repository.getChair(second.id);
  CHECK(persistedSecond.has_value());
  CHECK(persistedSecond && persistedSecond->ordinal == 2);
  CHECK(persistedSecond && persistedSecond->flatIndex == 2);
}

void testChairRoleCanChangeWithoutChangingItsMidiDestination() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);

  auto section = makeChair("violin-section-1", fiddle::DoricoRole::section,
                           1, 8);
  auto solo = makeChair("violin-solo-1", fiddle::DoricoRole::solo, 1, 9);
  CHECK(repository.upsertChair(section));
  CHECK(repository.upsertChair(solo));
  CHECK(repository.changeChairRole(section.id, fiddle::DoricoRole::solo));

  const auto changed = repository.getChair(section.id);
  CHECK(changed.has_value());
  CHECK(changed && changed->role == fiddle::DoricoRole::solo);
  CHECK(changed && changed->ordinal == 2);
  CHECK(changed && changed->flatIndex == 8);
  CHECK(repository.changeChairRole(section.id, fiddle::DoricoRole::solo));
}

void testHistoricalTopologyRestoresMissingCatalogReference() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  const auto patch = soloViolinPatch();
  const auto chair =
      makeChair("violin-section-1", fiddle::DoricoRole::section, 1, 1);
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.upsertChair(chair));
  CHECK(repository.createLayerFromPatch("layer-a", chair.id, patch.id, 0));
  const auto savedLayer = repository.getLayer("layer-a");
  CHECK(savedLayer.has_value());

  CHECK(repository.deleteLayer("layer-a"));
  CHECK(repository.deletePatch(patch.id) ==
        fiddle::PatchDeleteResult::deleted);
  CHECK(!repository.getPatch(patch.id).has_value());

  CHECK(savedLayer && repository.replaceTopology({chair}, {*savedLayer}));
  const auto restored = repository.getLayer("layer-a");
  CHECK(restored.has_value());
  CHECK(restored && restored->patchId == patch.id);
  CHECK(restored && restored->patchName == patch.name);
  CHECK(restored && restored->libraryName == "Vienna");
  CHECK(!repository.getPatch(patch.id).has_value());

  CHECK(repository.replaceTopology({}, {}));
  CHECK(repository.listChairs().empty());
  CHECK(repository.listLayers().empty());
}

void testLegacyLayerSchemaDropsCatalogForeignKeyWithoutLosingRows() {
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
  CHECK(execute(database, "PRAGMA foreign_keys=ON"));
  CHECK(execute(database,
                "CREATE TABLE libraries (id TEXT PRIMARY KEY, name TEXT)"));
  CHECK(execute(database, R"(
    CREATE TABLE library_patches (
      id TEXT PRIMARY KEY, library_id TEXT NOT NULL, position INTEGER,
      name TEXT NOT NULL, instrument_entity_id TEXT, family TEXT,
      character TEXT, plugin_uid INTEGER, plugin_state BLOB,
      expression_map_id TEXT,
      FOREIGN KEY (library_id) REFERENCES libraries(id) ON DELETE RESTRICT)
  )"));
  CHECK(execute(database, R"(
    CREATE TABLE chairs (
      id TEXT PRIMARY KEY, instrument_entity_id TEXT NOT NULL, name TEXT,
      family TEXT, dorico_role INTEGER, ordinal INTEGER, display_order INTEGER,
      flat_index INTEGER UNIQUE,
      UNIQUE (instrument_entity_id, dorico_role, ordinal))
  )"));
  CHECK(execute(database, R"(
    CREATE TABLE layers (
      id TEXT PRIMARY KEY, chair_id TEXT NOT NULL, patch_id TEXT NOT NULL,
      position INTEGER, active INTEGER, muted INTEGER, soloed INTEGER,
      gain_db REAL, plugin_uid INTEGER, plugin_state BLOB,
      expression_map_id TEXT,
      FOREIGN KEY (chair_id) REFERENCES chairs(id) ON DELETE RESTRICT,
      FOREIGN KEY (patch_id) REFERENCES library_patches(id) ON DELETE RESTRICT)
  )"));
  CHECK(execute(database, "INSERT INTO libraries VALUES ('vsl', 'Vienna')"));
  CHECK(execute(database,
                "INSERT INTO library_patches VALUES "
                "('patch', 'vsl', 0, 'Violin', 'violin', 'Strings', '', 42, "
                "NULL, 'xmap')"));
  CHECK(execute(database,
                "INSERT INTO chairs VALUES "
                "('chair', 'violin', 'Violin I', 'Strings', 1, 1, 0, 1)"));
  CHECK(execute(database,
                "INSERT INTO layers VALUES "
                "('layer', 'chair', 'patch', 0, 1, 0, 0, 0.0, 42, NULL, "
                "'xmap')"));

  CHECK(fiddle::LibraryRoutingRepository::ensureSchema(database));
  std::mutex mutex;
  fiddle::LibraryRoutingRepository repository(database, mutex);
  const auto migrated = repository.getLayer("layer");
  CHECK(migrated.has_value());
  CHECK(migrated && migrated->patchId == "patch");
  CHECK(repository.deleteLayer("layer"));
  CHECK(repository.deletePatch("patch") == fiddle::PatchDeleteResult::deleted);
  CHECK(migrated && repository.replaceTopology(
                        {makeChair("chair", fiddle::DoricoRole::section, 1, 1)},
                        {*migrated}));
  CHECK(repository.getLayer("layer").has_value());
  CHECK(sqlite3_close(database) == SQLITE_OK);
}

} // namespace

int main() {
  std::cout << "===== Library Routing Repository Tests =====\n";
  testPatchCanCreateIndependentLayersOnDifferentChairs();
  testOneChairAcceptsSeveralPatchesFromOneLibrary();
  testCatalogPatchCanUpdateEveryLinkedLayerWithoutChangingItsMix();
  testReferencedPatchDeletionIsBlocked();
  testLibraryPatchReplacementIsAtomicAndReferenceSafe();
  testDeletingChairExplicitlyDeletesItsLayersOnly();
  testChairDestinationsAndChannelsAreUnique();
  testChairAllocationIsStableAndReclaimsMatchingDestination();
  testChairRoleCanChangeWithoutChangingItsMidiDestination();
  testHistoricalTopologyRestoresMissingCatalogReference();
  testLegacyLayerSchemaDropsCatalogForeignKeyWithoutLosingRows();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
