// Per-library expression and percussion maps (docs/library-map-import.md):
// reading maps from .doricolib files, fingerprints, library-scoped storage,
// snapshots, the migration from the shared percussion map list, and the
// conversion of pre-chair project versions (LegacyStripConversion.h).
#include "../ExpressionMapParser.h"
#include "../LegacyStripConversion.h"
#include "../LibraryMaps.h"
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

using fiddle::LibraryMapKind;
using fiddle::LibraryMapRow;

bool execute(sqlite3 *database, const char *sql) {
  return sqlite3_exec(database, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
}

juce::File fixture(const char *relative) {
  return juce::File::getCurrentWorkingDirectory().getChildFile(relative);
}

struct DatabaseFixture {
  explicit DatabaseFixture(bool initialize = true) {
    CHECK(sqlite3_open(":memory:", &database) == SQLITE_OK);
    CHECK(execute(database, "PRAGMA foreign_keys=ON"));
    CHECK(execute(database, "CREATE TABLE libraries (id TEXT PRIMARY KEY, name TEXT, "
                            "vendor TEXT DEFAULT '', variant TEXT DEFAULT '')"));
    CHECK(execute(database, "INSERT INTO libraries (id, name) VALUES ('vsl', 'Vienna'), "
                            "('ews', 'EastWest'), ('spare', 'Spare')"));
    if (initialize)
      CHECK(fiddle::LibraryRoutingRepository::ensureSchema(database));
  }
  ~DatabaseFixture() {
    if (database)
      sqlite3_close(database);
  }
  sqlite3 *database = nullptr;
  std::mutex mutex;
};

fiddle::LibraryPatchRow patch(std::string id, std::string libraryId) {
  fiddle::LibraryPatchRow row;
  row.id = std::move(id);
  row.libraryId = std::move(libraryId);
  row.name = row.id;
  row.instrumentEntityId = "instrument.percussion.snare";
  return row;
}

LibraryMapRow map(std::string libraryId, LibraryMapKind kind, std::string entityId, std::string xml) {
  LibraryMapRow row;
  row.libraryId = std::move(libraryId);
  row.kind = kind;
  row.entityId = std::move(entityId);
  row.name = row.entityId;
  row.xml = std::move(xml);
  return row;
}

int countRows(sqlite3 *database, const char *sql) {
  sqlite3_stmt *statement = nullptr;
  int count = -1;
  if (sqlite3_prepare_v2(database, sql, -1, &statement, nullptr) == SQLITE_OK &&
      sqlite3_step(statement) == SQLITE_ROW)
    count = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return count;
}

void testReadsEveryExpressionMapInAFile() {
  const auto file = fixture("example-expression-maps/EWQL - Hollywood Brass - Expression Maps.doricolib");
  juce::String error;
  const auto candidates = fiddle::readLibraryMapFile(LibraryMapKind::expression, file, error);
  const auto parsed = fiddle::parseAllExpressionMaps(file);
  CHECK(error.isEmpty());
  CHECK(candidates.size() > 1);
  CHECK(candidates.size() == parsed.size());
  for (std::size_t i = 0; i < candidates.size() && i < parsed.size(); ++i) {
    const auto &candidate = candidates[i];
    CHECK(candidate.entityId == parsed[i].entityID);
    CHECK(candidate.file == file);
    CHECK(candidate.hash == fiddle::libraryMapHash(candidate.definitionXml));
    CHECK(candidate.hash.length() == 64);
    // A stored definition alone compiles to the same map as the file.
    const auto compiled = fiddle::compileExpressionMap(candidate.definitionXml);
    CHECK(compiled != nullptr);
    if (compiled) {
      CHECK(compiled->entityID == parsed[i].entityID);
      CHECK(compiled->combinations.size() == parsed[i].combinations.size());
    }
    CHECK(fiddle::libraryMapDefinitionVersion(LibraryMapKind::expression, candidate.definitionXml) ==
          candidate.version);
  }
  // Reading again gives identical fingerprints.
  const auto again = fiddle::readLibraryMapFile(LibraryMapKind::expression, file, error);
  CHECK(again.size() == candidates.size());
  if (!again.empty() && !candidates.empty())
    CHECK(again.front().hash == candidates.front().hash);
  // An expression map file has no percussion maps.
  CHECK(fiddle::readLibraryMapFile(LibraryMapKind::percussion, file, error).empty());
}

void testReadsPercussionMapsAndExportsExpressionMaps() {
  const auto file = fixture("example-expression-maps/percussion/VSL SY Percussion - Snare Drum (PM).doricolib");
  juce::String error;
  const auto candidates = fiddle::readLibraryMapFile(LibraryMapKind::percussion, file, error);
  CHECK(error.isEmpty());
  CHECK(candidates.size() == 1);
  if (!candidates.empty()) {
    CHECK(!candidates.front().entityId.empty());
    CHECK(candidates.front().definitionXml.contains("DrumKitNoteMapDefinition"));
    CHECK(fiddle::libraryMapDefinitionVersion(LibraryMapKind::percussion, candidates.front().definitionXml) ==
          candidates.front().version);
  }
  CHECK(fiddle::readLibraryMapFile(LibraryMapKind::percussion, fixture("missing.doricolib"), error).empty());
  CHECK(error == "file not found");

  // Exported expression maps read back as the same definition.
  const auto source = fixture("example-expression-maps/BWWA Eb Clarinet.doricolib");
  const auto maps = fiddle::readLibraryMapFile(LibraryMapKind::expression, source, error);
  CHECK(maps.size() == 1);
  if (!maps.empty()) {
    const auto exported = fiddle::expressionMapLibraryXml(maps.front().definitionXml);
    juce::String readError;
    const auto reread = fiddle::readLibraryMaps(LibraryMapKind::expression, exported, {}, readError);
    CHECK(reread.size() == 1);
    if (!reread.empty())
      CHECK(reread.front().hash == maps.front().hash);
  }
}

// Dorico's own built-in map files share the .doricolib structure. Skipped
// where Dorico isn't installed.
void testReadsDoricosBuiltInMaps() {
  for (const auto &file : fiddle::doricoMapSources()) {
    if (file.isDirectory())
      continue;
    juce::String error;
    const auto kind = file.getFileName().startsWith("drumKit") ? LibraryMapKind::percussion
                                                               : LibraryMapKind::expression;
    const auto maps = fiddle::readLibraryMapFile(kind, file, error);
    if (maps.empty())
      std::cerr << file.getFullPathName() << ": " << error << std::endl;
    CHECK(!maps.empty());
    for (const auto &map : maps)
      CHECK(!map.entityId.empty() && !map.definitionXml.isEmpty());
  }
}

void testMapsAreScopedByLibraryAndKind() {
  DatabaseFixture db;
  fiddle::LibraryRoutingRepository repository(db.database, db.mutex);
  CHECK(repository.upsertLibraryMap(map("vsl", LibraryMapKind::percussion, "drumkit.snare", "<A/>")));
  CHECK(repository.upsertLibraryMap(map("ews", LibraryMapKind::percussion, "drumkit.snare", "<B/>")));
  CHECK(repository.upsertLibraryMap(map("vsl", LibraryMapKind::expression, "drumkit.snare", "<C/>")));
  CHECK(repository.getLibraryMap("vsl", LibraryMapKind::percussion, "drumkit.snare")->xml == "<A/>");
  CHECK(repository.getLibraryMap("ews", LibraryMapKind::percussion, "drumkit.snare")->xml == "<B/>");
  CHECK(repository.getLibraryMap("vsl", LibraryMapKind::expression, "drumkit.snare")->xml == "<C/>");
  CHECK(!repository.getLibraryMap("spare", LibraryMapKind::percussion, "drumkit.snare"));
  CHECK(repository.listLibraryMaps("vsl").size() == 2);
  CHECK(repository.listLibraryMaps("vsl", LibraryMapKind::percussion).size() == 1);
  CHECK(repository.listLibraryMaps().size() == 3);

  // Revisions advance only on a content change; source fields are stored.
  auto row = map("vsl", LibraryMapKind::percussion, "drumkit.snare", "<A/>");
  row.sourcePath = "/maps/snare.doricolib";
  row.sourceVersion = 3;
  row.sourceHash = "abc";
  CHECK(repository.upsertLibraryMap(row));
  auto stored = repository.getLibraryMap("vsl", LibraryMapKind::percussion, "drumkit.snare");
  CHECK(stored->revision == 1);
  CHECK(stored->sourcePath == "/maps/snare.doricolib");
  CHECK(stored->sourceVersion == 3);
  CHECK(stored->sourceHash == "abc");
  CHECK(!stored->edited);
  row.xml = "<A2/>";
  row.edited = true;
  CHECK(repository.upsertLibraryMap(row));
  stored = repository.getLibraryMap("vsl", LibraryMapKind::percussion, "drumkit.snare");
  CHECK(stored->revision == 2);
  CHECK(stored->edited);

  // References and deletion are per library: the EastWest copy is unused.
  auto vslPatch = patch("vsl-snare", "vsl");
  vslPatch.percussionMapId = "drumkit.snare";
  CHECK(repository.upsertPatch(vslPatch));
  CHECK(repository.libraryMapReferences("vsl", LibraryMapKind::percussion, "drumkit.snare").size() == 1);
  CHECK(repository.libraryMapReferences("ews", LibraryMapKind::percussion, "drumkit.snare").empty());
  CHECK(repository.libraryMapReferences("vsl", LibraryMapKind::expression, "drumkit.snare").empty());
  CHECK(repository.deleteLibraryMap("vsl", LibraryMapKind::percussion, "drumkit.snare") ==
        fiddle::PatchDeleteResult::referenced);
  CHECK(repository.deleteLibraryMap("ews", LibraryMapKind::percussion, "drumkit.snare") ==
        fiddle::PatchDeleteResult::deleted);
  CHECK(repository.deleteLibraryMap("ews", LibraryMapKind::percussion, "drumkit.snare") ==
        fiddle::PatchDeleteResult::notFound);

  // Patches naming maps their library doesn't hold are reported.
  auto unmapped = patch("vsl-violin", "vsl");
  unmapped.expressionMapId = "xmap.violin";
  CHECK(repository.upsertPatch(unmapped));
  const auto missing = repository.missingLibraryMapReferences();
  CHECK(missing.size() == 1);
  if (!missing.empty()) {
    CHECK(missing.front().libraryId == "vsl");
    CHECK(missing.front().kind == LibraryMapKind::expression);
    CHECK(missing.front().entityId == "xmap.violin");
  }
}

void testSnapshotsCarryMapsOnlyWhenCaptured() {
  DatabaseFixture db;
  fiddle::LibraryRoutingRepository repository(db.database, db.mutex);
  CHECK(repository.upsertLibraryMap(map("vsl", LibraryMapKind::expression, "xmap.a", "<A/>")));
  CHECK(repository.upsertLibraryMap(map("vsl", LibraryMapKind::percussion, "drumkit.b", "<B/>")));
  const auto captured = repository.captureLibrary("vsl");
  CHECK(captured && captured->includesMaps && captured->maps.size() == 2);

  // An editor's patch-only snapshot leaves the maps alone.
  fiddle::LibraryCatalogSnapshot edited;
  edited.id = "vsl";
  edited.name = "Vienna";
  edited.exists = true;
  edited.patches = {patch("vsl-1", "vsl")};
  CHECK(repository.restoreLibrary(edited, false) == fiddle::PatchReplaceResult::replaced);
  CHECK(repository.listLibraryMaps("vsl").size() == 2);

  // Deleting the library deletes its maps; restoring brings them back exactly.
  CHECK(repository.upsertLibraryMap(map("vsl", LibraryMapKind::expression, "xmap.a", "<A2/>")));
  const auto beforeDelete = repository.captureLibrary("vsl");
  fiddle::LibraryCatalogSnapshot removed;
  removed.id = "vsl";
  CHECK(repository.restoreLibrary(removed) == fiddle::PatchReplaceResult::replaced);
  CHECK(repository.listLibraryMaps("vsl").empty());
  CHECK(repository.restoreLibrary(*beforeDelete) == fiddle::PatchReplaceResult::replaced);
  const auto restored = repository.getLibraryMap("vsl", LibraryMapKind::expression, "xmap.a");
  CHECK(restored && restored->xml == "<A2/>" && restored->revision == 2);
  CHECK(repository.listLibraryMaps("vsl").size() == 2);

  // Restoring the earlier capture rolls the map back, revision included.
  CHECK(repository.restoreLibrary(*captured) == fiddle::PatchReplaceResult::replaced);
  const auto rolledBack = repository.getLibraryMap("vsl", LibraryMapKind::expression, "xmap.a");
  CHECK(rolledBack && rolledBack->xml == "<A/>" && rolledBack->revision == 1);
}

void testSharedPercussionMapsMigrateToTheLibrariesThatUseThem() {
  DatabaseFixture db;
  fiddle::LibraryRoutingRepository repository(db.database, db.mutex);
  // The old schema: one shared list of percussion maps.
  CHECK(execute(db.database, R"(
    CREATE TABLE library_percussion_maps (
      entity_id TEXT PRIMARY KEY, name TEXT NOT NULL, xml TEXT NOT NULL,
      revision INTEGER NOT NULL DEFAULT 1, updated_at INTEGER NOT NULL DEFAULT (unixepoch()));
    INSERT INTO library_percussion_maps (entity_id, name, xml, revision) VALUES
      ('drumkit.used', 'Used', '<Used/>', 4),
      ('drumkit.unused', 'Unused', '<Unused/>', 1);
  )"));
  auto vslPatch = patch("vsl-snare", "vsl");
  vslPatch.percussionMapId = "drumkit.used";
  CHECK(repository.upsertPatch(vslPatch));
  fiddle::ChairRow chair;
  chair.id = "snare-chair";
  chair.instrumentEntityId = "instrument.percussion.snare";
  chair.name = "Snare Drum";
  CHECK(repository.insertChairWithStableAssignment(chair));
  fiddle::LayerRow layer;
  layer.id = "ews-layer";
  layer.chairId = chair.id;
  layer.patchId = "ews-snare";
  layer.libraryId = "ews";
  layer.percussionMapId = "drumkit.used";
  CHECK(repository.upsertLayer(layer));

  CHECK(fiddle::LibraryRoutingRepository::ensureSchema(db.database));
  CHECK(countRows(db.database, "SELECT COUNT(*) FROM sqlite_master WHERE name = 'library_percussion_maps'") == 0);
  const auto vsl = repository.getLibraryMap("vsl", LibraryMapKind::percussion, "drumkit.used");
  const auto ews = repository.getLibraryMap("ews", LibraryMapKind::percussion, "drumkit.used");
  CHECK(vsl && vsl->xml == "<Used/>" && vsl->revision == 4 && vsl->sourcePath.empty());
  CHECK(ews && ews->xml == "<Used/>");
  CHECK(repository.listLibraryMaps().size() == 2); // the unused map is dropped
  CHECK(repository.missingLibraryMapReferences().empty());

  // Rerunning finds nothing to migrate.
  CHECK(fiddle::LibraryRoutingRepository::ensureSchema(db.database));
  CHECK(repository.listLibraryMaps().size() == 2);
}

// Expression maps a library uses are copied from catalog files (highest
// version), then a saved project version; percussion maps get the Dorico file
// they came from. A rerun copies nothing.
void testMigrationCopiesExpressionMapsIntoLibraries() {
  const auto temp = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("LibraryMapsTest-" + juce::String(juce::Random::getSystemRandom().nextInt64()));
  CHECK(temp.createDirectory());
  // Real definitions with only the identity changed.
  juce::String baseError;
  const auto base = fiddle::readLibraryMapFile(
      LibraryMapKind::expression, fixture("example-expression-maps/BWWA Eb Clarinet.doricolib"), baseError);
  CHECK(base.size() == 1);
  if (base.empty())
    return;
  const auto replaceChild = [](juce::String xml, const char *tag, const juce::String &value) {
    const auto open = juce::String("<") + tag + ">";
    const auto close = juce::String("</") + tag + ">";
    const int start = xml.indexOf(open);
    const int end = xml.indexOf(start, close);
    return start < 0 || end < 0 ? xml : xml.substring(0, start + open.length()) + value + xml.substring(end);
  };
  const auto definition = [&](const char *id, int version, const char *name) {
    auto xml = replaceChild(base.front().definitionXml, "entityID", id);
    xml = replaceChild(xml, "version", juce::String(version));
    return replaceChild(xml, "name", name);
  };
  const auto older = temp.getChildFile("older.doricolib");
  const auto newer = temp.getChildFile("newer.doricolib");
  CHECK(older.replaceWithText(fiddle::expressionMapLibraryXml(definition("xmap.violin", 1, "Violin v1"))));
  CHECK(newer.replaceWithText(fiddle::expressionMapLibraryXml(definition("xmap.violin", 2, "Violin v2"))));
  const auto percussionSource = fixture("example-expression-maps/percussion/VSL SY Percussion - Snare Drum (PM).doricolib");
  juce::String error;
  const auto snare = fiddle::readLibraryMapFile(LibraryMapKind::percussion, percussionSource, error);
  CHECK(snare.size() == 1);

  DatabaseFixture db;
  fiddle::LibraryRoutingRepository repository(db.database, db.mutex);
  auto violin = patch("vsl-violin", "vsl");
  violin.expressionMapId = "xmap.violin";
  CHECK(repository.upsertPatch(violin));
  auto viola = patch("ews-viola", "ews");
  viola.expressionMapId = "xmap.viola"; // only in a saved project version
  CHECK(repository.upsertPatch(viola));
  auto cello = patch("ews-cello", "ews");
  cello.expressionMapId = "xmap.cello"; // nowhere
  CHECK(repository.upsertPatch(cello));
  if (!snare.empty())
    CHECK(repository.upsertLibraryMap(map("vsl", LibraryMapKind::percussion, snare.front().entityId,
                                          snare.front().definitionXml.toStdString())));

  const auto saved = [&](const std::string &entityId) {
    return entityId == "xmap.viola" ? fiddle::expressionMapLibraryXml(definition("xmap.viola", 3, "Viola"))
                                    : juce::String();
  };
  juce::Array<juce::File> doricoSources{percussionSource.getParentDirectory()};
  const auto report = fiddle::migrateLibraryMaps(repository, {older, newer}, doricoSources, saved, true);
  CHECK(report.expressionCopied == 2);
  CHECK(report.percussionSourced == 1);
  CHECK(report.missing.size() == 1 && report.missing.front() == "ews / xmap.cello");

  const auto copiedViolin = repository.getLibraryMap("vsl", LibraryMapKind::expression, "xmap.violin");
  CHECK(copiedViolin && copiedViolin->name == "Violin v2");
  CHECK(copiedViolin && copiedViolin->sourcePath == newer.getFullPathName().toStdString());
  CHECK(copiedViolin && copiedViolin->sourceVersion == 2 && copiedViolin->sourceHash.size() == 64);
  CHECK(copiedViolin && fiddle::compileExpressionMap(juce::String(copiedViolin->xml)) != nullptr);
  const auto copiedViola = repository.getLibraryMap("ews", LibraryMapKind::expression, "xmap.viola");
  CHECK(copiedViola && copiedViola->sourcePath.empty() && copiedViola->sourceHash.empty());
  if (!snare.empty()) {
    const auto sourced = repository.getLibraryMap("vsl", LibraryMapKind::percussion, snare.front().entityId);
    CHECK(sourced && sourced->sourcePath == percussionSource.getFullPathName().toStdString());
    CHECK(sourced && sourced->sourceHash == snare.front().hash.toStdString());
  }

  const auto rerun = fiddle::migrateLibraryMaps(repository, {older, newer}, doricoSources, saved, true);
  CHECK(rerun.expressionCopied == 0 && rerun.percussionSourced == 0 && rerun.missing.size() == 1);
  temp.deleteRecursively();
}


fiddle::versioning::StripBlob legacyStrip(std::string library, std::string family, int port, int channel,
                                          int pluginUid, std::vector<std::uint8_t> state = {}) {
  fiddle::versioning::StripBlob blob;
  blob.library = std::move(library);
  blob.family = std::move(family);
  blob.isSolo = false;
  blob.inputPort = port;
  blob.inputChannel = channel;
  blob.pluginUid = pluginUid;
  blob.pluginState = std::move(state);
  return blob;
}

// A version saved before chairs: strips sharing an input share a chair, strips
// sharing a library name share a generated library, identical setups share a
// patch, and empty strips leave only their chair.
void testLegacyStripsBecomeChairsLayersAndLibraries() {
  juce::String error;
  const auto clarinet = fiddle::readLibraryMapFile(
      LibraryMapKind::expression, fixture("example-expression-maps/BWWA Eb Clarinet.doricolib"), error);
  const auto snare = fiddle::readLibraryMapFile(
      LibraryMapKind::percussion,
      fixture("example-expression-maps/percussion/VSL SY Percussion - Snare Drum (PM).doricolib"), error);
  CHECK(clarinet.size() == 1 && snare.size() == 1);
  if (clarinet.empty() || snare.empty())
    return;

  auto first = legacyStrip("Elite", "strings", 0, 1, 5, {1, 2, 3});
  first.expressionMapEntityId = clarinet.front().entityId;
  first.expressionMapSourceXml = fiddle::expressionMapLibraryXml(clarinet.front().definitionXml).toStdString();
  first.gainDb = -6.0f;
  first.muted = true;
  const auto second = legacyStrip(" Elite ", "strings", 0, 1, 6, {9});    // same input, same library
  auto drum = legacyStrip("", "percussion", 0, 2, 7);                      // unnamed library
  drum.percussionMapSourceXml = snare.front().definitionXml.toStdString();
  const auto empty = legacyStrip("Elite", "wind", 0, 3, 0);                // only a chair
  auto noInput = legacyStrip("Elite", "strings", 0, 0, 5, {1, 2, 3});      // same setup as first
  noInput.expressionMapEntityId = first.expressionMapEntityId;
  noInput.expressionMapSourceXml = first.expressionMapSourceXml;
  const std::vector<std::pair<std::string, fiddle::versioning::StripBlob>> strips{
      {"h-first", first}, {"h-second", second}, {"h-drum", drum}, {"h-empty", empty}, {"h-noinput", noInput}};

  fiddle::ChairRow live;
  live.id = "live-violin";
  live.instrumentEntityId = "instrument.strings.violin";
  live.name = "Violin 1";
  live.family = "strings";
  live.role = fiddle::DoricoRole::section;
  live.ordinal = 1;
  live.flatIndex = 1;
  int nextId = 0;
  const auto newId = [&nextId] { return "id-" + std::to_string(++nextId); };
  const auto conversion = fiddle::convertLegacyStrips(strips, {live}, newId);

  CHECK(conversion.libraries.size() == 2);
  CHECK(fiddle::restoredLibraryId("Elite") == fiddle::restoredLibraryId(" Elite "));
  CHECK(fiddle::restoredLibraryId("") == fiddle::restoredLibraryId("  "));
  CHECK(fiddle::restoredLibraryId("Elite") != fiddle::restoredLibraryId(""));
  const fiddle::LibraryCatalogSnapshot *elite = nullptr, *unfiled = nullptr;
  for (const auto &library : conversion.libraries) {
    if (library.name == "Elite (restored)") elite = &library;
    if (library.name == "Unfiled (restored)") unfiled = &library;
  }
  CHECK(elite && unfiled);
  if (!elite || !unfiled)
    return;
  CHECK(elite->id == fiddle::restoredLibraryId("Elite") && elite->exists && elite->includesMaps);
  CHECK(elite->patches.size() == 2); // first and noInput share a patch
  CHECK(elite->maps.size() == 1 && elite->maps.front().kind == LibraryMapKind::expression &&
        elite->maps.front().entityId == clarinet.front().entityId &&
        elite->maps.front().xml == clarinet.front().definitionXml.toStdString());
  if (!elite->patches.empty()) {
    const auto &patch = elite->patches.front();
    CHECK(patch.name == clarinet.front().name);
    CHECK(patch.pluginUid == 5 && patch.pluginState == std::vector<std::uint8_t>({1, 2, 3}));
    CHECK(patch.expressionMapId == clarinet.front().entityId);
    CHECK(patch.instrumentEntityId == "instrument.strings.violin");
  }
  CHECK(unfiled->patches.size() == 1 && unfiled->maps.size() == 1);
  if (!unfiled->patches.empty())
    CHECK(unfiled->patches.front().percussionMapId == snare.front().entityId);

  // Inputs 1, 2 and 3 keep their chairs; the strip on input 0 gets input 4.
  CHECK(conversion.chairs.size() == 4);
  if (conversion.chairs.size() == 4) {
    const auto &violin = conversion.chairs[0];
    CHECK(violin.flatIndex == 1 && violin.instrumentEntityId == "instrument.strings.violin" &&
          violin.name == "Violin 1" && violin.role == fiddle::DoricoRole::section && violin.ordinal == 1);
    CHECK(violin.id != live.id);
    CHECK(conversion.chairs[1].flatIndex == 2 &&
          conversion.chairs[1].instrumentEntityId == fiddle::standInInstrument("percussion") &&
          conversion.chairs[1].name == "Percussion 1/3");
    CHECK(conversion.chairs[2].flatIndex == 3 && conversion.chairs[2].instrumentEntityId == "instrument.wind.flute");
    // A stand-in on the violin's instrument and role takes the next ordinal.
    CHECK(conversion.chairs[3].flatIndex == 4 && conversion.chairs[3].instrumentEntityId == "instrument.strings.violin" &&
          conversion.chairs[3].role == fiddle::DoricoRole::section && conversion.chairs[3].ordinal == 2);
    for (int i = 0; i < 4; ++i)
      CHECK(conversion.chairs[static_cast<size_t>(i)].displayOrder == i);
  }

  CHECK(conversion.layers.size() == 4); // the empty strip has none
  CHECK(conversion.layerHashes.size() == 4);
  if (conversion.layers.size() == 4 && conversion.chairs.size() == 4) {
    const auto &layer = conversion.layers[0];
    CHECK(layer.chairId == conversion.chairs[0].id && layer.position == 0);
    CHECK(layer.libraryId == elite->id && layer.libraryName == "Elite (restored)");
    CHECK(layer.muted && layer.gainDb == -6.0f && layer.pluginUid == 5);
    CHECK(conversion.layerHashes.at(layer.id) == "h-first");
    CHECK(conversion.layers[1].chairId == conversion.chairs[0].id && conversion.layers[1].position == 1);
    CHECK(conversion.layers[2].chairId == conversion.chairs[1].id &&
          conversion.layers[2].percussionMapId == snare.front().entityId);
    CHECK(conversion.layers[3].chairId == conversion.chairs[3].id &&
          conversion.layers[3].patchId == conversion.layers[0].patchId);
  }

  // Converting again yields the same library and patch identities.
  const auto again = fiddle::convertLegacyStrips(strips, {live}, newId);
  CHECK(again.libraries.size() == 2 && again.layers.size() == 4);
  if (again.layers.size() == 4 && conversion.layers.size() == 4)
    CHECK(again.layers[0].patchId == conversion.layers[0].patchId);

  // Written to the database: the libraries and topology are accepted, and a
  // second restore keeps the user's edits to the generated library.
  DatabaseFixture db;
  fiddle::LibraryRoutingRepository repository(db.database, db.mutex);
  for (const auto &library : conversion.libraries)
    CHECK(repository.restoreLibrary(fiddle::mergeRestoredLibrary(repository.captureLibrary(library.id), library)) ==
          fiddle::PatchReplaceResult::replaced);
  CHECK(repository.replaceTopology(conversion.chairs, conversion.layers));
  CHECK(repository.listChairs().size() == 4);
  CHECK(repository.listPatches(elite->id).size() == 2);
  CHECK(repository.getLibraryMap(unfiled->id, LibraryMapKind::percussion, snare.front().entityId).has_value());

  auto renamed = repository.getPatch(elite->patches.front().id);
  CHECK(renamed.has_value());
  if (renamed) {
    renamed->name = "My clarinet";
    CHECK(repository.upsertPatch(*renamed));
  }
  auto extra = legacyStrip("Elite", "brass", 0, 5, 11, {4});
  const auto third = fiddle::convertLegacyStrips({{"h-extra", extra}}, repository.listChairs(), newId);
  CHECK(third.libraries.size() == 1);
  if (third.libraries.size() == 1) {
    const auto merged = fiddle::mergeRestoredLibrary(repository.captureLibrary(elite->id), third.libraries.front());
    CHECK(merged.patches.size() == 3 && merged.maps.size() == 1);
    CHECK(repository.restoreLibrary(merged) == fiddle::PatchReplaceResult::replaced);
  }
  CHECK(repository.listPatches(elite->id).size() == 3);
  const auto kept = repository.getPatch(elite->patches.front().id);
  CHECK(kept && kept->name == "My clarinet");
  CHECK(repository.getLibraryMap(elite->id, LibraryMapKind::expression, clarinet.front().entityId).has_value());
}

} // namespace

int main() {
  testReadsEveryExpressionMapInAFile();
  testReadsPercussionMapsAndExportsExpressionMaps();
  testReadsDoricosBuiltInMaps();
  testMapsAreScopedByLibraryAndKind();
  testSnapshotsCarryMapsOnlyWhenCaptured();
  testSharedPercussionMapsMigrateToTheLibrariesThatUseThem();
  testMigrationCopiesExpressionMapsIntoLibraries();
  testLegacyStripsBecomeChairsLayersAndLibraries();
  std::cout << "LibraryMapsTest: " << passed << " passed, " << failed << " failed" << std::endl;
  return failed == 0 ? 0 : 1;
}
