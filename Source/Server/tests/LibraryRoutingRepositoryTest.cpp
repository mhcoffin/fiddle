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

} // namespace

int main() {
  std::cout << "===== Library Routing Repository Tests =====\n";
  testPatchCanCreateIndependentLayersOnDifferentChairs();
  testOneChairAcceptsSeveralPatchesFromOneLibrary();
  testReferencedPatchDeletionIsBlocked();
  testDeletingChairExplicitlyDeletesItsLayersOnly();
  testChairDestinationsAndChannelsAreUnique();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
