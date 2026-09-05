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
  testReferencedPatchDeletionIsBlocked();
  testDeletingChairExplicitlyDeletesItsLayersOnly();
  testChairDestinationsAndChannelsAreUnique();
  testChairAllocationIsStableAndReclaimsMatchingDestination();
  testHistoricalTopologyRestoresMissingCatalogReference();
  testLegacyLayerSchemaDropsCatalogForeignKeyWithoutLosingRows();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
