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
    CHECK(execute(database, "ALTER TABLE libraries ADD COLUMN vendor TEXT DEFAULT ''"));
    CHECK(execute(database, "ALTER TABLE libraries ADD COLUMN variant TEXT DEFAULT ''"));
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
  patch.expectedPresetName = "Synchron Solo Violin 1";
  patch.setupComplete = false;
  return patch;
}

// What a layer's Update from Library writes: the patch's library-owned setup,
// leaving routing and mixer controls alone.
bool refreshLayerFromPatch(fiddle::LibraryRoutingRepository &repository,
                           const std::string &layerId) {
  auto layer = repository.getLayer(layerId);
  const auto patch = layer ? repository.getPatch(layer->patchId) : std::nullopt;
  if (!patch) return false;
  layer->patchName = patch->name;
  layer->libraryId = patch->libraryId;
  layer->pluginUid = patch->pluginUid;
  layer->pluginState = patch->pluginState;
  layer->expressionMapId = patch->expressionMapId;
  layer->percussionMapId = patch->percussionMapId;
  layer->sourcePatchRevision = patch->revision;
  layer->pluginStateEdited = false;
  return repository.updateLayerSetups({*layer});
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

void testExpressionMapReferencesAreReadOnlyMetadata() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  auto patch = soloViolinPatch();
  CHECK(repository.upsertPatch(patch));
  auto unmapped = patch;
  unmapped.id = "unmapped";
  unmapped.expressionMapId.clear();
  CHECK(repository.upsertPatch(unmapped));
  const auto changes = sqlite3_total_changes(fixture.database);
  const auto references = repository.listExpressionMapReferences();
  CHECK(references.size() == 1);
  CHECK(references[0].mapId == patch.expressionMapId);
  CHECK(references[0].label == "Vienna / Synchron Solo Violin 1");
  CHECK(sqlite3_total_changes(fixture.database) == changes);
  CHECK(repository.getPatch(patch.id)->pluginState == patch.pluginState);
}

void testCatalogSnapshotsAreAtomicAndRevisionSafe() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  fiddle::LibraryCatalogSnapshot created;
  created.id = "new-library";
  created.name = "New Library";
  created.vendor = "Vendor";
  created.variant = "Variant";
  created.exists = true;
  auto createdPatch = soloViolinPatch();
  createdPatch.id = "new-patch";
  createdPatch.libraryId = created.id;
  created.patches.push_back(createdPatch);
  CHECK(repository.restoreLibrary(created, false) ==
        fiddle::PatchReplaceResult::replaced);
  const auto capturedCreated = repository.captureLibrary(created.id);
  CHECK(capturedCreated && capturedCreated->exists &&
        capturedCreated->patches.size() == 1);

  auto patch = soloViolinPatch();
  CHECK(repository.upsertPatch(patch));
  auto original = repository.captureLibrary("vsl");
  CHECK(original && original->exists && original->patches.size() == 1);
  CHECK(original->patches[0].expectedPresetName ==
        patch.expectedPresetName);
  CHECK(!original->patches[0].setupComplete);
  auto edited = *original;
  edited.name = "Edited header"; edited.vendor = "Vendor"; edited.variant = "Variant";
  edited.patches[0].pluginState = {9, 8, 7};
  CHECK(repository.restoreLibrary(edited, false) == fiddle::PatchReplaceResult::replaced);
  const auto second = repository.captureLibrary("vsl");
  CHECK(second->patches[0].revision > original->patches[0].revision);
  CHECK(repository.restoreLibrary(*original) == fiddle::PatchReplaceResult::replaced);
  CHECK(repository.getPatch(patch.id)->pluginState == patch.pluginState);
  CHECK(repository.getPatch(patch.id)->expectedPresetName ==
        patch.expectedPresetName);
  CHECK(!repository.getPatch(patch.id)->setupComplete);
  CHECK(repository.getPatch(patch.id)->revision == original->patches[0].revision);
  // A new branch must not reuse the identity of the just-undone setup.
  edited.patches[0].pluginState = {6, 5, 4};
  CHECK(repository.restoreLibrary(edited, false) == fiddle::PatchReplaceResult::replaced);
  CHECK(repository.getPatch(patch.id)->revision > second->patches[0].revision);
  CHECK(repository.upsertChair(makeChair("chair", fiddle::DoricoRole::solo, 1, 1)));
  CHECK(repository.createLayerFromPatch("layer", "chair", patch.id, 0));
  const auto before = repository.captureLibrary("vsl");
  auto rejected = *before;
  rejected.name = "Must not leak"; rejected.patches.clear();
  CHECK(repository.restoreLibrary(rejected) == fiddle::PatchReplaceResult::referenced);
  CHECK(repository.captureLibrary("vsl")->name == before->name);
  CHECK(repository.getLayer("layer")->pluginState == edited.patches[0].pluginState);
  fiddle::LibraryCatalogSnapshot removed; removed.id = "vsl";
  CHECK(repository.restoreLibrary(removed) == fiddle::PatchReplaceResult::referenced);
  CHECK(repository.deleteLayer("layer"));
  CHECK(repository.restoreLibrary(removed) == fiddle::PatchReplaceResult::replaced);
  CHECK(!repository.captureLibrary("vsl")->exists);
  CHECK(repository.restoreLibrary(*before) == fiddle::PatchReplaceResult::replaced);
  CHECK(repository.captureLibrary("vsl")->vendor == "Vendor");
  CHECK(repository.getPatch(patch.id)->pluginState == edited.patches[0].pluginState);
  CHECK(repository.getPatch(patch.id)->revision == before->patches[0].revision);
  // Invalid rows roll back header edits and all preceding row writes.
  rejected = *before; rejected.name = "Invalid header";
  rejected.patches.push_back(rejected.patches.front());
  CHECK(repository.restoreLibrary(rejected) == fiddle::PatchReplaceResult::error);
  CHECK(repository.captureLibrary("vsl")->name == before->name);
  CHECK(execute(fixture.database, "CREATE TRIGGER reject_catalog_delete BEFORE DELETE ON libraries BEGIN SELECT RAISE(ABORT, 'test'); END"));
  CHECK(repository.restoreLibrary(removed) == fiddle::PatchReplaceResult::error);
  CHECK(repository.getPatch(patch.id)->pluginState == edited.patches[0].pluginState);
}

void testAtomicChairEditsAndRestoration() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  auto a = makeChair("a", fiddle::DoricoRole::section, 1, 1);
  auto b = makeChair("b", fiddle::DoricoRole::section, 2, 2);
  CHECK(repository.upsertChair(a) && repository.upsertChair(b));
  auto edited = a;
  edited.name = "Edited";
  auto invalid = b;
  invalid.flatIndex = a.flatIndex;
  CHECK(!repository.updateChairs({edited, invalid}));
  CHECK(repository.getChair(a.id)->name == a.name);
  CHECK(repository.getChair(b.id)->flatIndex == b.flatIndex);
  CHECK(repository.updateChairs({edited, b}));
  CHECK(repository.getChair(a.id)->name == "Edited");

  fiddle::LayerRow first, rejected;
  first.id = "first"; first.chairId = a.id; first.patchId = "missing-patch";
  rejected = first; rejected.id = "reject";
  CHECK(repository.upsertLayer(first));
  CHECK(repository.deleteChairAndLayers(a.id));
  CHECK(execute(fixture.database, R"(
    CREATE TRIGGER reject_test_layer BEFORE INSERT ON layers WHEN NEW.id = 'reject'
    BEGIN SELECT RAISE(ABORT, 'test restoration failure'); END
  )"));
  CHECK(!repository.restoreChairAndLayers(a, {first, rejected}));
  CHECK(!repository.getChair(a.id) && !repository.getLayer(first.id));
  // Entire failed restore rolled back, including insertion of the first child.
  CHECK(repository.restoreChairAndLayers(a, {first}));
  CHECK(repository.getLayer(first.id).has_value());
  CHECK(!repository.restoreChairAndLayers(a, {first})); // Never overwrite.
  auto next = a; next.id = "next";
  CHECK(repository.insertChairWithStableAssignment(next));
  CHECK(next.flatIndex == 3 && next.ordinal == 3); // No stale tombstone.
}

void testLayerSetupBatchIsAtomicAndPreservesMix() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  CHECK(repository.upsertChair(makeChair("chair", fiddle::DoricoRole::solo, 1, 1)));
  fiddle::LayerRow a;
  a.id = "a"; a.chairId = "chair"; a.patchId = "historical-patch";
  a.patchName = "Original"; a.pluginState = {1, 2};
  a.position = 4; a.gainDb = -7.5f; a.active = false;
  a.muted = true; a.soloed = true; a.pluginStateEdited = true;
  auto b = a; b.id = "b"; b.position = 5;
  CHECK(repository.upsertLayer(a) && repository.upsertLayer(b));
  auto nextA = a, nextB = b;
  for (auto *row : {&nextA, &nextB}) {
    row->patchName = "Updated"; row->pluginUid = 410;
    row->pluginState = {3, 4}; row->expressionMapId = "new-map";
    row->sourcePatchRevision = 2; row->pluginStateEdited = false;
    // Setup updates must ignore stale mixer fields in the supplied snapshots.
    row->position = 0; row->gainDb = 0; row->active = true;
    row->muted = false; row->soloed = false;
  }
  CHECK(execute(fixture.database, R"(
    CREATE TRIGGER reject_setup BEFORE UPDATE ON layers WHEN NEW.id = 'b'
    BEGIN SELECT RAISE(ABORT, 'test setup failure'); END
  )"));
  CHECK(!repository.updateLayerSetups({nextA, nextB}));
  CHECK(repository.getLayer("a")->patchName == "Original");
  CHECK(repository.getLayer("a")->pluginState == a.pluginState);
  CHECK(repository.getLayer("b")->pluginStateEdited);
  CHECK(execute(fixture.database, "DROP TRIGGER reject_setup"));
  auto invalid = nextB; invalid.id = "missing";
  CHECK(!repository.updateLayerSetups({nextA, invalid}));
  invalid = nextB; invalid.patchId = "reassigned";
  CHECK(!repository.updateLayerSetups({nextA, invalid}));
  invalid = nextB; invalid.chairId = "other-chair";
  CHECK(!repository.updateLayerSetups({nextA, invalid}));
  CHECK(!repository.updateLayerSetups({nextA, nextA}));
  CHECK(repository.getLayer("a")->patchName == "Original");
  CHECK(repository.updateLayerSetups({nextA, nextB}));
  const auto updated = *repository.getLayer("a");
  CHECK(updated.pluginState == nextA.pluginState);
  CHECK(updated.expressionMapId == "new-map" && updated.sourcePatchRevision == 2);
  CHECK(!updated.pluginStateEdited && updated.pluginUid == 410);
  CHECK(updated.position == 4 && updated.gainDb == -7.5f);
  CHECK(!updated.active && updated.muted && updated.soloed);
  CHECK(repository.getLayer("b")->position == 5);
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

void testRefreshingLinkedLayersKeepsTheirMix() {
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
  CHECK(refreshLayerFromPatch(repository, "layer-b"));
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);
  const auto updated = repository.getLayer("layer-a");
  CHECK(updated.has_value());
  CHECK(updated && updated->patchName == patch.name);
  CHECK(updated && updated->libraryId == patch.libraryId);
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
  CHECK(refreshLayerFromPatch(repository, "layer-a"));
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);
  CHECK(!refreshLayerFromPatch(repository, "missing-layer"));
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

void testPercussionMapsAreStoredReferencedAndCopiedToLayers() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  const auto kind = fiddle::LibraryMapKind::percussion;
  fiddle::LibraryMapRow map;
  map.kind = kind;
  map.entityId = "drumkitmap.user.snare";
  map.name = "Snare";
  map.xml = "<DrumKitNoteMapDefinition/>";
  CHECK(repository.upsertMap(map));
  CHECK(repository.getMap(kind, map.entityId)->revision == 1);
  CHECK(repository.upsertMap(map)); // unchanged: same revision
  CHECK(repository.getMap(kind, map.entityId)->revision == 1);
  map.xml = "<DrumKitNoteMapDefinition><name>Snare</name></DrumKitNoteMapDefinition>";
  CHECK(repository.upsertMap(map));
  CHECK(repository.getMap(kind, map.entityId)->revision == 2);
  CHECK(repository.listMaps(kind).size() == 1);
  CHECK(repository.listMaps(fiddle::LibraryMapKind::expression).empty());

  auto patch = soloViolinPatch();
  patch.instrumentEntityId = "instrument.orchestral.unpitched.snare.drum";
  patch.percussionMapId = map.entityId;
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.getPatch(patch.id)->percussionMapId == map.entityId);
  auto chair = makeChair("snare-1", fiddle::DoricoRole::solo, 1, 1);
  chair.instrumentEntityId = patch.instrumentEntityId;
  CHECK(repository.upsertChair(chair));
  CHECK(repository.createLayerFromPatch("snare-layer", chair.id, patch.id, 0));
  CHECK(repository.getLayer("snare-layer")->percussionMapId == map.entityId);
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);

  // Referenced maps cannot be deleted.
  CHECK(repository.mapReferences(kind, map.entityId).size() == 2);
  CHECK(repository.deleteMap(kind, map.entityId) == fiddle::PatchDeleteResult::referenced);

  // Play as is stored on the layer; one the patch doesn't set for this
  // chair differs from the library.
  {
    auto playing = *repository.getLayer("snare-layer");
    CHECK(playing.percussionPlayAs.empty());
    playing.percussionPlayAs = "instrument.orchestral.unpitched.side.drum";
    CHECK(repository.upsertLayer(playing));
    CHECK(repository.getLayer("snare-layer")->percussionPlayAs == playing.percussionPlayAs);
    CHECK(repository.outOfDateLayerCount(patch.id) == 1);
    CHECK(repository.updateLayerSetups({playing}));
    CHECK(repository.getLayer("snare-layer")->percussionPlayAs == playing.percussionPlayAs);
    playing.percussionPlayAs.clear();
    CHECK(repository.upsertLayer(playing));
  }

  // A layer override differs from its patch until refreshed.
  auto layer = *repository.getLayer("snare-layer");
  layer.percussionMapId.clear();
  CHECK(repository.updateLayerSetups({layer}));
  CHECK(repository.getLayer("snare-layer")->percussionMapId.empty());
  CHECK(repository.outOfDateLayerCount(patch.id) == 1);
  CHECK(refreshLayerFromPatch(repository, "snare-layer"));
  CHECK(repository.getLayer("snare-layer")->percussionMapId == map.entityId);

  // Changing the patch's map is a library change.
  const int revision = repository.getPatch(patch.id)->revision;
  patch.percussionMapId.clear();
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.getPatch(patch.id)->revision == revision + 1);
  CHECK(repository.deleteLayer("snare-layer"));
  CHECK(repository.deleteMap(kind, map.entityId) == fiddle::PatchDeleteResult::deleted);
  CHECK(repository.deleteMap(kind, map.entityId) == fiddle::PatchDeleteResult::notFound);
}

fiddle::LibraryMapRow expressionMap(const std::string &entityId, const std::string &xml) {
  fiddle::LibraryMapRow map;
  map.kind = fiddle::LibraryMapKind::expression;
  map.entityId = entityId;
  map.name = entityId;
  map.xml = xml;
  return map;
}

// One collection of maps (docs/maps.md): any library's patches and layers
// use the same row, and the picker learns who uses what.
void testMapsAreSharedAcrossLibraries() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  const auto kind = fiddle::LibraryMapKind::expression;
  CHECK(repository.upsertMap(expressionMap("xmap.violin", "<ExpressionMapDefinition/>")));

  auto vsl = soloViolinPatch();
  vsl.expressionMapId = "xmap.violin";
  CHECK(repository.upsertPatch(vsl));
  auto ews = soloViolinPatch();
  ews.id = "ews-violin";
  ews.libraryId = "ews";
  ews.name = "EW Violin";
  ews.instrumentEntityId = "instrument.strings.violin";
  ews.expressionMapId = "xmap.violin";
  CHECK(repository.upsertPatch(ews));
  auto chair = makeChair("violin-1", fiddle::DoricoRole::section, 1, 1);
  CHECK(repository.upsertChair(chair));
  CHECK(repository.createLayerFromPatch("violin-layer", chair.id, ews.id, 0));

  const auto usage = repository.listMapUsage();
  CHECK(usage.size() == 3);
  // Patches by library and position, then layers.
  CHECK(!usage[0].layer && usage[0].patchId == ews.id && usage[0].libraryId == "ews" &&
        usage[0].label == "EastWest / EW Violin");
  CHECK(!usage[1].layer && usage[1].patchId == vsl.id && usage[1].libraryId == "vsl" &&
        usage[1].instrument == "instrument.strings.violin" && usage[1].label == "Vienna / Synchron Solo Violin 1");
  CHECK(usage[2].layer && usage[2].patchId == ews.id && usage[2].libraryId == "ews" &&
        usage[2].instrument == chair.instrumentEntityId && usage[2].label == "Layer " + chair.name + " / EW Violin");
  CHECK(repository.mapReferences(kind, "xmap.violin").size() == 3);
  CHECK(repository.mapReferences(fiddle::LibraryMapKind::percussion, "xmap.violin").empty());
  CHECK(repository.deleteMap(kind, "xmap.violin") == fiddle::PatchDeleteResult::referenced);

  // Missing maps are reported by ID alone.
  auto other = vsl;
  other.id = "vsl-viola";
  other.expressionMapId = "xmap.viola";
  CHECK(repository.upsertPatch(other));
  const auto missing = repository.missingMapReferences();
  CHECK(missing.size() == 1 && missing[0].kind == kind && missing[0].entityId == "xmap.viola");

  // Deleting a library, or undoing its edits, leaves the maps alone.
  CHECK(repository.deleteLayer("violin-layer"));
  fiddle::LibraryCatalogSnapshot deleted;
  deleted.id = "ews";
  CHECK(repository.restoreLibrary(deleted) == fiddle::PatchReplaceResult::replaced);
  CHECK(repository.getMap(kind, "xmap.violin").has_value());
  CHECK(repository.listMapUsage().size() == 2);
}

// The star is a preference: saving or re-importing a map keeps it.
void testMapFavoritesSurviveEdits() {
  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  const auto kind = fiddle::LibraryMapKind::expression;
  auto map = expressionMap("xmap.violin", "<ExpressionMapDefinition/>");
  CHECK(repository.upsertMap(map));
  CHECK(!repository.getMap(kind, map.entityId)->favorite);
  CHECK(repository.setMapFavorite(kind, map.entityId, true));
  CHECK(repository.getMap(kind, map.entityId)->favorite);
  CHECK(repository.getMap(kind, map.entityId)->revision == 1);
  CHECK(!repository.setMapFavorite(kind, "xmap.missing", true));

  map.xml = "<ExpressionMapDefinition><version>2</version></ExpressionMapDefinition>";
  map.favorite = false; // an import's row knows nothing of the star
  CHECK(repository.upsertMap(map));
  CHECK(repository.getMap(kind, map.entityId)->favorite);
  CHECK(repository.getMap(kind, map.entityId)->revision == 2);

  // Undoing a delete re-inserts the captured row, star included.
  const auto before = *repository.getMap(kind, map.entityId);
  CHECK(repository.deleteMap(kind, map.entityId) == fiddle::PatchDeleteResult::deleted);
  CHECK(repository.upsertMap(before));
  CHECK(repository.getMap(kind, map.entityId)->favorite);

  // insertMapIfAbsent never replaces Fiddle's copy.
  auto other = map;
  other.xml = "<ExpressionMapDefinition><version>9</version></ExpressionMapDefinition>";
  CHECK(!repository.insertMapIfAbsent(other));
  CHECK(repository.getMap(kind, map.entityId)->xml == map.xml);
  CHECK(repository.insertMapIfAbsent(expressionMap("xmap.new", "<ExpressionMapDefinition/>")));
  CHECK(repository.getMap(kind, "xmap.new").has_value());
}

// The per-library maps of 2026-11-04 become one collection. When libraries
// held different copies of a map, the most recently updated copy wins.
void testPerLibraryMapsMigrateToOneCollection() {
  sqlite3 *database = nullptr;
  CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
  CHECK(execute(database, "CREATE TABLE libraries (id TEXT PRIMARY KEY, name TEXT)"));
  CHECK(execute(database, R"(
    CREATE TABLE library_maps (
      library_id TEXT NOT NULL, kind TEXT NOT NULL, entity_id TEXT NOT NULL, name TEXT NOT NULL,
      xml TEXT NOT NULL, revision INTEGER NOT NULL DEFAULT 1, source_path TEXT NOT NULL DEFAULT '',
      source_version INTEGER NOT NULL DEFAULT 0, source_hash TEXT NOT NULL DEFAULT '',
      edited INTEGER NOT NULL DEFAULT 0, updated_at INTEGER NOT NULL DEFAULT 0,
      PRIMARY KEY (library_id, kind, entity_id));
    INSERT INTO library_maps (library_id, kind, entity_id, name, xml, revision, source_path, updated_at) VALUES
      ('vsl', 'expression', 'xmap.shared', 'Shared', '<a/>', 1, '/vsl.doricolib', 100),
      ('ews', 'expression', 'xmap.shared', 'Shared', '<a/>', 1, '/vsl.doricolib', 100),
      ('vsl', 'expression', 'xmap.clash', 'Old', '<old/>', 5, '', 100),
      ('ews', 'expression', 'xmap.clash', 'New', '<new/>', 1, '', 200),
      ('vsl', 'percussion', 'xmap.shared', 'Snare', '<p/>', 1, '', 100);
    INSERT INTO library_maps (library_id, kind, entity_id, name, xml, edited, updated_at) VALUES
      ('ews', 'percussion', 'drum.edited', 'Edited', '<e/>', 1, 50);
  )"));
  CHECK(fiddle::LibraryRoutingRepository::ensureSchema(database));
  CHECK(fiddle::LibraryRoutingRepository::ensureSchema(database));
  std::mutex mutex;
  fiddle::LibraryRoutingRepository repository(database, mutex);
  const auto expression = repository.listMaps(fiddle::LibraryMapKind::expression);
  CHECK(expression.size() == 2);
  const auto clash = repository.getMap(fiddle::LibraryMapKind::expression, "xmap.clash");
  CHECK(clash && clash->name == "New" && clash->xml == "<new/>" && clash->revision == 1);
  const auto shared = repository.getMap(fiddle::LibraryMapKind::expression, "xmap.shared");
  CHECK(shared && shared->sourcePath == "/vsl.doricolib" && !shared->favorite);
  CHECK(repository.listMaps(fiddle::LibraryMapKind::percussion).size() == 2);
  CHECK(repository.getMap(fiddle::LibraryMapKind::percussion, "drum.edited")->edited);
  sqlite3_stmt *old = nullptr;
  CHECK(sqlite3_prepare_v2(database, "SELECT 1 FROM sqlite_master WHERE name = 'library_maps'", -1, &old,
                           nullptr) == SQLITE_OK);
  CHECK(sqlite3_step(old) == SQLITE_DONE);
  sqlite3_finalize(old);
  CHECK(sqlite3_close(database) == SQLITE_OK);
}

void testPatchPlayAsDefaults() {
  // The stored form: sorted, flat JSON that json_extract reads.
  const std::map<std::string, std::string> table{
      {"instrument.band.unpitched.floortom.low", "instrument.band.unpitched.tomtom.medium.alias.tom-medium"},
      {"instrument.band.unpitched.floortom.high", "instrument.band.unpitched.tomtom.low.alias.tom-low"}};
  const auto encoded = fiddle::encodePercussionPlayAs(table);
  CHECK(encoded.rfind("{\"instrument.band.unpitched.floortom.high\":", 0) == 0);
  CHECK(fiddle::decodePercussionPlayAs(encoded) == table);
  CHECK(fiddle::encodePercussionPlayAs({}).empty());
  CHECK(fiddle::decodePercussionPlayAs("").empty());
  CHECK(fiddle::decodePercussionPlayAs("{\"a\":").empty());
  CHECK((fiddle::decodePercussionPlayAs(R"({"a\"b":"c"})") == std::map<std::string, std::string>{{"a\"b", "c"}}));

  DatabaseFixture fixture;
  fiddle::LibraryRoutingRepository repository(fixture.database, fixture.mutex);
  auto patch = soloViolinPatch();
  patch.id = "concert-toms";
  patch.name = "Concert Toms";
  patch.instrumentEntityId = "instrument.band.unpitched.tomtom.low";
  patch.percussionMapId = "vsl.toms";
  patch.percussionPlayAs = table;
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.getPatch(patch.id)->percussionPlayAs == table);
  const int revision = repository.getPatch(patch.id)->revision;

  // A layer takes the default for its chair's instrument, or none.
  auto floor = makeChair("floor", fiddle::DoricoRole::solo, 1, 1);
  floor.instrumentEntityId = "instrument.band.unpitched.floortom.high";
  auto low = makeChair("low", fiddle::DoricoRole::solo, 1, 2);
  low.instrumentEntityId = "instrument.band.unpitched.tomtom.low";
  CHECK(repository.upsertChair(floor));
  CHECK(repository.upsertChair(low));
  CHECK(repository.createLayerFromPatch("floor-layer", floor.id, patch.id, 0));
  CHECK(repository.createLayerFromPatch("low-layer", low.id, patch.id, 0));
  CHECK(repository.getLayer("floor-layer")->percussionPlayAs == table.at(floor.instrumentEntityId));
  CHECK(repository.getLayer("low-layer")->percussionPlayAs.empty());
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);

  // A layer's own choice differs from its patch until refreshed.
  auto layer = *repository.getLayer("low-layer");
  layer.percussionPlayAs = "instrument.band.unpitched.tomtom.medium.alias.tom-medium";
  CHECK(repository.updateLayerSetups({layer}));
  CHECK(repository.outOfDateLayerCount(patch.id) == 1);
  CHECK(repository.outOfDateLayerIds() == std::vector<std::string>{"low-layer"});
  layer.percussionPlayAs.clear();
  CHECK(repository.updateLayerSetups({layer}));
  CHECK(repository.outOfDateLayerCount(patch.id) == 0);

  // Changing the defaults is a library change; saving the same ones isn't.
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.getPatch(patch.id)->revision == revision);
  patch.percussionPlayAs.erase("instrument.band.unpitched.floortom.low");
  CHECK(repository.upsertPatch(patch));
  CHECK(repository.getPatch(patch.id)->revision == revision + 1);
}

int main() {
  std::cout << "===== Library Routing Repository Tests =====\n";
  testExpressionMapReferencesAreReadOnlyMetadata();
  testCatalogSnapshotsAreAtomicAndRevisionSafe();
  testAtomicChairEditsAndRestoration();
  testLayerSetupBatchIsAtomicAndPreservesMix();
  testPatchCanCreateIndependentLayersOnDifferentChairs();
  testOneChairAcceptsSeveralPatchesFromOneLibrary();
  testRefreshingLinkedLayersKeepsTheirMix();
  testReferencedPatchDeletionIsBlocked();
  testLibraryPatchReplacementIsAtomicAndReferenceSafe();
  testDeletingChairExplicitlyDeletesItsLayersOnly();
  testChairDestinationsAndChannelsAreUnique();
  testChairAllocationIsStableAndReclaimsMatchingDestination();
  testChairRoleCanChangeWithoutChangingItsMidiDestination();
  testHistoricalTopologyRestoresMissingCatalogReference();
  testLegacyLayerSchemaDropsCatalogForeignKeyWithoutLosingRows();
  testPercussionMapsAreStoredReferencedAndCopiedToLayers();
  testMapsAreSharedAcrossLibraries();
  testMapFavoritesSurviveEdits();
  testPerLibraryMapsMigrateToOneCollection();
  testPatchPlayAsDefaults();
  std::cout << "Passed: " << passed << '\n';
  std::cout << "Failed: " << failed << '\n';
  return failed == 0 ? 0 : 1;
}
