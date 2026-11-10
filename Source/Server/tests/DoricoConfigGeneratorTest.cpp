#include "../DoricoConfigGenerator.h"
#include "../BridgeMapDocument.h"
#include "../DoricoKitDefinitions.h"

#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {

int passed = 0;
int failed = 0;

#define CHECK(condition)                                                       \
  do {                                                                         \
    if (condition)                                                             \
      ++passed;                                                                \
    else {                                                                     \
      ++failed;                                                                \
      std::cerr << "FAIL [" << __FILE__ << ':' << __LINE__                    \
                << "]: " << #condition << std::endl;                          \
    }                                                                          \
  } while (false)

void testSparseStableDestinationsAreUsedAsEndpointNumbers() {
  std::vector<fiddle::InstrumentAssignment> assignments;

  fiddle::InstrumentAssignment violin;
  violin.entityID = "instrument.strings.violin";
  violin.name = "Violin 1";
  violin.isSolo = false;
  violin.flatIndex = 1; // Port 1, channel 2; channel 1 is the metronome.
  assignments.push_back(violin);

  fiddle::InstrumentAssignment flute;
  flute.entityID = "instrument.wind.flute";
  flute.name = "Flute 1";
  flute.isSolo = true;
  flute.flatIndex = 18; // Port 2, channel 3, with a deliberate gap.
  assignments.push_back(flute);

  auto xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml(
      assignments, {});
  CHECK(xml != nullptr);

  auto *slots = xml ? xml->getChildByName("slots") : nullptr;
  auto *slot = slots ? slots->getChildByName("slotData") : nullptr;
  auto *instance = slot ? slot->getChildByName("instanceData") : nullptr;
  auto *contents =
      instance ? instance->getChildByName("programContents") : nullptr;
  auto *entries = contents ? contents->getChildByName("entries") : nullptr;
  auto *firstEntry = entries ? entries->getFirstChildElement() : nullptr;
  auto *secondEntry = firstEntry ? firstEntry->getNextElement() : nullptr;
  CHECK(firstEntry != nullptr);
  CHECK(firstEntry &&
        firstEntry->getChildElementAllSubText("portIndex", "") == "0");
  CHECK(firstEntry && firstEntry->getChildElementAllSubText(
                            "channelNumberRel0", "") == "1");
  CHECK(secondEntry &&
        secondEntry->getChildElementAllSubText("portIndex", "") == "1");
  CHECK(secondEntry && secondEntry->getChildElementAllSubText(
                             "channelNumberRel0", "") == "2");

  auto *instruments = xml ? xml->getChildByName("instruments") : nullptr;
  auto *firstInstrument =
      instruments ? instruments->getFirstChildElement() : nullptr;
  auto *secondInstrument =
      firstInstrument ? firstInstrument->getNextElement() : nullptr;
  CHECK(firstInstrument != nullptr);
  CHECK(firstInstrument && firstInstrument->getChildElementAllSubText(
                               "endpoints", "") == "1");
  CHECK(secondInstrument && secondInstrument->getChildElementAllSubText(
                                "endpoints", "") == "18");
}

// A chair re-added later lists after its siblings (display order), but Dorico's
// first solo flute must still reach the chair named Flute 1.
void testPlayerIndexFollowsOrdinalNotListOrder() {
  std::vector<fiddle::InstrumentAssignment> assignments;
  const auto add = [&](const char *name, int ordinal, int flat, bool solo = true) {
    fiddle::InstrumentAssignment a;
    a.entityID = "instrument.wind.flute"; a.name = name; a.isSolo = solo;
    a.ordinal = ordinal; a.flatIndex = flat;
    assignments.push_back(a);
  };
  add("Flute 2", 2, 10);
  add("Flute 1", 1, 8);
  add("Flute section 1", 1, 12, false);
  const auto indices = fiddle::DoricoConfigGenerator::playerIndices(assignments);
  CHECK(indices == std::vector<int>({1, 0, 0}));
  auto xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml(assignments, {});
  std::map<std::pair<juce::String, juce::String>, juce::String> endpointFor;
  if (auto *instruments = xml ? xml->getChildByName("instruments") : nullptr)
    for (auto *data : instruments->getChildIterator())
      endpointFor[{data->getChildElementAllSubText("playerType", ""), data->getChildElementAllSubText("index", "")}] =
          data->getChildElementAllSubText("endpoints", "");
  CHECK((endpointFor[{"kSoloPlayer", "0"}] == "8"));  // Flute 1
  CHECK((endpointFor[{"kSoloPlayer", "1"}] == "10")); // Flute 2
  CHECK((endpointFor[{"kSectionPlayer", "0"}] == "12"));
  // Without ordinals (older callers), list order still decides.
  for (auto &a : assignments) a.ordinal = -1;
  CHECK(fiddle::DoricoConfigGenerator::playerIndices(assignments) == std::vector<int>({0, 1, 0}));
}

// Kit instruments share sound IDs (every wood block is wood.wood-block), and
// kits share an empty one with many other instruments. Each instrument with a
// chair must map only to its own chair, and a variant at most once per player
// index (docs/percussion-kits.md).
std::vector<fiddle::BrowsableInstrument> blockCatalogue() {
  std::vector<fiddle::BrowsableInstrument> result;
  for (const char *suffix : {"high", "medium.high", "medium", "medium.low", "low"}) {
    fiddle::BrowsableInstrument block;
    block.entityID = juce::String("instrument.percussion.other.wood.block.") + suffix;
    block.name = block.entityID;
    block.musicXMLSoundID = "wood.wood-block";
    result.push_back(block);
  }
  for (const char *id : {"instrument.kit.wood.blocks", "instrument.body.finger.snaps"}) {
    fiddle::BrowsableInstrument empty;
    empty.entityID = id;
    empty.name = id;
    result.push_back(empty); // no sound ID
  }
  return result;
}

std::multimap<juce::String, std::pair<juce::String, juce::String>> mappings(const juce::XmlElement *xml) {
  std::multimap<juce::String, std::pair<juce::String, juce::String>> result; // entity -> (index, endpoint)
  if (auto *instruments = xml ? xml->getChildByName("instruments") : nullptr)
    for (auto *data : instruments->getChildIterator())
      result.insert({data->getChildElementAllSubText("entityID", ""),
                     {data->getChildElementAllSubText("index", ""), data->getChildElementAllSubText("endpoints", "")}});
  return result;
}

fiddle::InstrumentAssignment soloChair(const juce::String &entity, int ordinal, int flat) {
  fiddle::InstrumentAssignment a;
  a.entityID = entity; a.name = entity; a.isSolo = true; a.ordinal = ordinal; a.flatIndex = flat;
  return a;
}

void testKitChairsMapOnlyTheirOwnInstruments() {
  const auto catalogue = blockCatalogue();
  std::vector<fiddle::InstrumentAssignment> kit;
  int flat = 20;
  for (int i = 0; i < 5; ++i) kit.push_back(soloChair(catalogue[size_t(i)].entityID, 1, flat++));
  auto xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml(kit, catalogue);
  const auto map = mappings(xml.get());
  CHECK(map.size() == 5);
  for (int i = 0; i < 5; ++i) {
    const auto range = map.equal_range(catalogue[size_t(i)].entityID);
    CHECK(std::distance(range.first, range.second) == 1);
    CHECK(range.first != range.second && range.first->second == std::make_pair(juce::String("0"), juce::String(20 + i)));
  }
  CHECK(map.count("instrument.kit.wood.blocks") == 0);

  // One wood-block chair still receives the other blocks as variants.
  xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml({soloChair(catalogue[0].entityID, 1, 30)}, catalogue);
  const auto single = mappings(xml.get());
  CHECK(single.size() == 5);
  for (const auto &[entity, where] : single) CHECK(where.second == "30");

  // An instrument with no sound ID groups nothing: no kits, no strangers.
  xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml({soloChair("instrument.body.finger.snaps", 1, 31)}, catalogue);
  const auto snaps = mappings(xml.get());
  CHECK(snaps.size() == 1 && snaps.count("instrument.body.finger.snaps") == 1);
}

void testKitBlockAndStandaloneBlockTakeSuccessiveIndices() {
  const auto catalogue = blockCatalogue();
  // A kit's high block (ordinal 1), a standalone high block (ordinal 2) and
  // the kit's low block.
  std::vector<fiddle::InstrumentAssignment> chairs{
      soloChair(catalogue[0].entityID, 1, 40), soloChair(catalogue[0].entityID, 2, 42),
      soloChair(catalogue[4].entityID, 1, 41)};
  auto xml = fiddle::DoricoConfigGenerator::buildEndpointConfigXml(chairs, catalogue);
  const auto map = mappings(xml.get());
  std::set<std::pair<juce::String, juce::String>> high;
  for (auto [it, end] = map.equal_range(catalogue[0].entityID); it != end; ++it) high.insert(it->second);
  CHECK((high == std::set<std::pair<juce::String, juce::String>>{{"0", "40"}, {"1", "42"}}));
  // Each (instrument, index) is mapped once, though both high chairs could
  // claim the medium block as a variant at their own index.
  std::set<std::pair<juce::String, juce::String>> seen;
  bool unique = true;
  for (const auto &[entity, where] : map) unique = seen.insert({entity, where.first}).second && unique;
  CHECK(unique);
  for (auto [it, end] = map.equal_range(catalogue[4].entityID); it != end; ++it)
    if (it->second.first == "0") CHECK(it->second.second == "41");
}

void testKitDefinitionsParseInKitOrder() {
  const auto xml = juce::parseXML(R"(<kScoreLibrary><percussionKitDefinitionCollectionDefinition><entities array="true">
    <PercussionKitDefinitionDefinition><name/><entityID>kit-definition.kit.wood.blocks</entityID>
      <kitDefinition><percussionKitComponents array="true">
        <percussionKitComponent><instrumentEntityID>instrument.percussion.other.wood.block.low</instrumentEntityID><instrumentOrder>2</instrumentOrder></percussionKitComponent>
        <percussionKitComponent><instrumentEntityID>instrument.percussion.other.wood.block.high</instrumentEntityID><instrumentOrder>1</instrumentOrder></percussionKitComponent>
      </percussionKitComponents></kitDefinition></PercussionKitDefinitionDefinition>
    <PercussionKitDefinitionDefinition><entityID>kit-definition.empty</entityID><kitDefinition/></PercussionKitDefinitionDefinition>
  </entities></percussionKitDefinitionCollectionDefinition></kScoreLibrary>)");
  CHECK(xml != nullptr);
  const auto kits = xml ? fiddle::parseKitDefinitions(*xml) : fiddle::KitDefinitions{};
  CHECK(kits.size() == 1);
  CHECK((kits.count("kit-definition.kit.wood.blocks") &&
         kits.at("kit-definition.kit.wood.blocks") == std::vector<juce::String>{
             "instrument.percussion.other.wood.block.high", "instrument.percussion.other.wood.block.low"}));
  const auto instrument = juce::parseXML(
      "<InstrumentEntityDefinition><entityID>instrument.kit.wood.blocks</entityID>"
      "<percussionKitDefinitionID>kit-definition.kit.wood.blocks</percussionKitDefinitionID></InstrumentEntityDefinition>");
  CHECK(instrument && fiddle::kitDefinitionIdOf(*instrument) == "kit-definition.kit.wood.blocks");
}

struct Sandbox {
  juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
      .getChildFile("fiddle-template-test-" + juce::Uuid().toString());
  ~Sandbox() { dir.deleteRecursively(); }
};

std::shared_ptr<const fiddle::BridgeMapDocument> bridge(const char *relative) {
  juce::String error;
  const auto doc = fiddle::BridgeMapDocument::compile(juce::File(FIDDLE_TEST_ROOT).getChildFile(relative).loadFileAsString(), error);
  if (!doc) throw std::runtime_error(error.toStdString());
  return doc;
}

void testPinnedMapsInstallWithMatchingEndpointsAndPresets() {
  Sandbox sandbox;
  fiddle::DoricoConfigGenerator generator(sandbox.dir);
  const std::vector<std::shared_ptr<const fiddle::BridgeMapDocument>> docs{
      bridge("experiments/orchestral-strings-bridge/r4/Fiddle-Orchestral-Strings-r4.doricolib"),
      bridge("experiments/orchestral-winds-bridge/woodwinds-r2/Fiddle-Orchestral-Woodwinds-r2.doricolib"),
      bridge("experiments/orchestral-winds-bridge/brass-r2/Fiddle-Orchestral-Brass-r2.doricolib")};
  const std::vector<int> destinations{1, 18, 35};
  std::vector<fiddle::InstrumentAssignment> assignments;
  fiddle::BridgeMapProject project;
  for (size_t i = 0; i < docs.size(); ++i) {
    fiddle::InstrumentAssignment a;
    a.entityID = "instrument.fixture." + juce::String(int(i));
    a.name = "Chair " + juce::String(int(i)); a.program = int(i) + 1; a.isSolo = true;
    a.flatIndex = destinations[i]; assignments.push_back(a);
    project.documents[docs[i]->contentHash] = docs[i];
    project.assignments[{a.flatIndex / 16, a.flatIndex % 16 + 1}] = docs[i]->contentHash;
  }
  // A second chair reuses the same immutable strings revision on another input.
  auto secondViolin = assignments.front(); secondViolin.name = "Violin 2";
  secondViolin.flatIndex = 3; secondViolin.program = 4; assignments.push_back(secondViolin);
  project.assignments[{0, 4}] = docs.front()->contentHash;
  auto result = generator.generateAndInstallFiles(assignments, 36, {}, project.serialize());
  if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
  const auto endpoint = sandbox.dir.getChildFile("EndpointConfigs/Fiddle/endpointconfig.xml");
  const auto presetsFile = sandbox.dir.getChildFile("PluginPresetLibraries/Fiddle/presets.xml");
  auto xml = juce::parseXML(endpoint), presets = juce::parseXML(presetsFile);
  CHECK(xml && presets);
  if (!xml || !presets) return;
  auto *entries = xml->getChildByName("slots")->getFirstChildElement()->getChildByName("instanceData")
      ->getChildByName("programContents")->getChildByName("entries");
  auto *instruments = xml->getChildByName("instruments");
  for (size_t i = 0; i < assignments.size(); ++i) {
    const auto doc = docs[i < docs.size() ? i : 0];
    const auto *entry = entries->getChildElement(int(i));
    CHECK(entry->getChildElementAllSubText("expressionMapID", {}) == doc->entityId);
    CHECK(entry->getChildElementAllSubText("portIndex", {}) == juce::String(assignments[i].flatIndex / 16));
    CHECK(entry->getChildElementAllSubText("channelNumberRel0", {}) == juce::String(assignments[i].flatIndex % 16));
    CHECK(instruments->getChildElement(int(i))->getChildElementAllSubText("endpoints", {}) == juce::String(assignments[i].flatIndex));
    CHECK(presets->getChildElement(int(i))->getChildElementAllSubText("ExpressionMap", {}) == doc->entityId);
  }
  const auto library = sandbox.dir.getChildFile("DefaultLibraryAdditions");
  CHECK(library.findChildFiles(juce::File::findFiles, false, "Fiddle-Bridge-*.doricolib").size() == 3);
  for (const auto &doc : docs)
    CHECK(library.getChildFile("Fiddle-Bridge-" + doc->contentHash + ".doricolib").loadFileAsString() == doc->sourceXml);
  CHECK(!library.getChildFile("Fiddle_Universal.doricolib").exists());
  const auto expectedEndpoint = endpoint.loadFileAsString();
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, project.serialize()).wasOk());
  CHECK(endpoint.loadFileAsString() == expectedEndpoint);

  // Metadata edits under an existing Dorico identity must not shadow that map.
  juce::String error;
  auto conflicting = fiddle::BridgeMapDocument::compile(docs.front()->sourceXml.replace("<creator>Fiddle</creator>", "<creator>Changed</creator>"), error);
  CHECK(conflicting);
  if (!conflicting) return;
  auto bad = project; bad.documents.erase(docs.front()->contentHash);
  bad.documents[conflicting->contentHash] = conflicting;
  bad.assignments[{0, 2}] = conflicting->contentHash; bad.assignments[{0, 4}] = conflicting->contentHash;
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, bad.serialize()).failed());
  CHECK(endpoint.loadFileAsString() == expectedEndpoint);
  CHECK(library.findChildFiles(juce::File::findFiles, false, "Fiddle-Bridge-*.doricolib").size() == 3);

  // New identities coexist with older versions; returning to an old pin works.
  // A new identity changes the schema-2 beacon too; restamp it as publish does.
  auto renamed = juce::parseXML(docs.front()->sourceXml.replace(
      "xmap.user.Fiddle.OrchestralStrings.r4", "xmap.user.Fiddle.fixture.r5")
      .replace("ptmg.user.Fiddle.OrchestralStrings.r4.", "ptmg.user.Fiddle.fixture.r5."));
  CHECK(renamed && fiddle::BridgeMapDocument::stampBeacon(*renamed->getChildByName("expressionMapDefinitions")
      ->getChildByName("entities")->getFirstChildElement()));
  const auto updated = fiddle::BridgeMapDocument::compile(renamed->toString(), error);
  CHECK(updated);
  if (!updated) return;
  auto newer = project; newer.documents[updated->contentHash] = updated;
  newer.assignments[{0, 2}] = updated->contentHash;
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, newer.serialize()).wasOk());
  CHECK(library.findChildFiles(juce::File::findFiles, false, "Fiddle-Bridge-*.doricolib").size() == 4);
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, project.serialize()).wasOk());
  CHECK(endpoint.loadFileAsString() == expectedEndpoint);

  auto orphan = project; orphan.assignments[{9, 2}] = docs.front()->contentHash;
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, orphan.serialize()).failed());
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, "invalid").failed());
  auto duplicates = assignments; duplicates.back().flatIndex = assignments.front().flatIndex;
  CHECK(generator.generateAndInstallFiles(duplicates, 36, {}, project.serialize()).failed());
  CHECK(endpoint.loadFileAsString() == expectedEndpoint);

  // An unassigned input uses the legacy map in both XML locations.
  auto mixed = project; mixed.assignments.erase({0, 4});
  CHECK(generator.generateAndInstallFiles(assignments, 36, {}, mixed.serialize()).wasOk());
  xml = juce::parseXML(endpoint); presets = juce::parseXML(presetsFile);
  CHECK(xml && presets);
  if (!xml || !presets) return;
  entries = xml->getChildByName("slots")->getFirstChildElement()->getChildByName("instanceData")
      ->getChildByName("programContents")->getChildByName("entries");
  CHECK(entries->getChildElement(3)->getChildElementAllSubText("expressionMapID", {}) == "xmap.user.Fiddle.1");
  CHECK(presets->getChildElement(3)->getChildElementAllSubText("ExpressionMap", {}) == "xmap.user.Fiddle.1");
  CHECK(library.getChildFile("Fiddle_Universal.doricolib").existsAsFile());
}

void testFailedPublicationRestoresExistingFiles() {
  Sandbox sandbox;
  const auto doc = bridge("experiments/orchestral-strings-bridge/r3/Fiddle-Orchestral-Strings-r3.doricolib");
  fiddle::BridgeMapProject project; project.documents[doc->contentHash] = doc;
  project.assignments[{0, 2}] = doc->contentHash;
  fiddle::InstrumentAssignment a; a.entityID = "violin"; a.name = "Violin";
  a.flatIndex = 1; a.program = 1; a.isSolo = true;
  const auto endpoint = sandbox.dir.getChildFile("EndpointConfigs/Fiddle/endpointconfig.xml");
  CHECK(endpoint.getParentDirectory().createDirectory().wasOk());
  CHECK(endpoint.replaceWithText("previous endpoint"));
  // A directory at a required file path forces failure after publishing maps.
  const auto blocked = sandbox.dir.getChildFile("PluginPresetLibraries/Fiddle/presets.xml");
  CHECK(blocked.createDirectory().wasOk());
  fiddle::DoricoConfigGenerator generator(sandbox.dir);
  CHECK(generator.generateAndInstallFiles({a}, 2, {}, project.serialize()).failed());
  CHECK(endpoint.loadFileAsString() == "previous endpoint");
  CHECK(blocked.isDirectory());
  CHECK(sandbox.dir.getChildFile("DefaultLibraryAdditions").findChildFiles(juce::File::findFiles, false).isEmpty());
}

// An unpitched chair's bridge carries a percussion map: it reaches both the
// endpoint entry and the preset, and an installed file under its ID must
// match, like the expression map's.
void testPercussionMapReachesEndpointAndPreset() {
  Sandbox sandbox;
  fiddle::DoricoConfigGenerator generator(sandbox.dir);
  const auto unpitched = bridge("experiments/unpitched-percussion-bridge/unpitched-percussion-r2/Fiddle-Orchestral-UnpitchedPercussion-r2.doricolib");
  const auto strings = bridge("experiments/orchestral-strings-bridge/r5/Fiddle-Orchestral-Strings-r5.doricolib");
  CHECK(unpitched->percussionMapId.isNotEmpty() && strings->percussionMapId.isEmpty());
  fiddle::BridgeMapProject project;
  project.documents[unpitched->contentHash] = unpitched;
  project.documents[strings->contentHash] = strings;
  project.assignments[{0, 2}] = unpitched->contentHash;
  project.assignments[{0, 3}] = strings->contentHash;
  std::vector<fiddle::InstrumentAssignment> assignments(2);
  assignments[0].entityID = "instrument.orchestral.unpitched.snare.drum"; assignments[0].name = "Snare Drum";
  assignments[0].flatIndex = 1; assignments[0].program = 1; assignments[0].isSolo = true;
  assignments[1].entityID = "instrument.strings.violin"; assignments[1].name = "Violin";
  assignments[1].flatIndex = 2; assignments[1].program = 2; assignments[1].isSolo = true;
  auto result = generator.generateAndInstallFiles(assignments, 3, {}, project.serialize());
  if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
  const auto endpoint = sandbox.dir.getChildFile("EndpointConfigs/Fiddle/endpointconfig.xml");
  const auto presetsFile = sandbox.dir.getChildFile("PluginPresetLibraries/Fiddle/presets.xml");
  const auto xml = juce::parseXML(endpoint), presets = juce::parseXML(presetsFile);
  CHECK(xml && presets);
  if (!xml || !presets) return;
  const auto *entries = xml->getChildByName("slots")->getFirstChildElement()->getChildByName("instanceData")
      ->getChildByName("programContents")->getChildByName("entries");
  CHECK(entries->getChildElement(0)->getChildElementAllSubText("expressionMapID", {}) == unpitched->entityId);
  CHECK(entries->getChildElement(0)->getChildElementAllSubText("drumkitNoteMapID", {}) == unpitched->percussionMapId);
  CHECK(entries->getChildElement(1)->getChildElementAllSubText("drumkitNoteMapID", {}).isEmpty());
  CHECK(presets->getChildElement(0)->getChildElementAllSubText("DrumKitNoteMap", {}) == unpitched->percussionMapId);
  CHECK(presets->getChildElement(1)->getChildByName("DrumKitNoteMap") == nullptr);
  const auto library = sandbox.dir.getChildFile("DefaultLibraryAdditions");
  CHECK(library.getChildFile("Fiddle-Bridge-" + unpitched->contentHash + ".doricolib").loadFileAsString() == unpitched->sourceXml);
  // Another revision that keeps the percussion map's ID but changes its
  // content cannot install beside it, even under a new expression-map ID.
  auto renamed = juce::parseXML(unpitched->sourceXml.replace("xmap.user.Fiddle.OrchestralUnpitchedPercussion.r2", "xmap.user.Fiddle.fixture.r2")
      .replace("ptmg.user.Fiddle.OrchestralUnpitchedPercussion.r2.", "ptmg.user.Fiddle.fixture.r2."));
  CHECK(renamed && fiddle::BridgeMapDocument::stampBeacon(*renamed->getChildByName("expressionMapDefinitions")
      ->getChildByName("entities")->getFirstChildElement()));
  auto *entry = renamed->getChildByName("drumKitNoteMapDefinitions")->getChildByName("entities")->getFirstChildElement()
      ->getChildByName("entries")->getFirstChildElement()->getChildByName("name");
  entry->deleteAllChildElements(); entry->addTextElement("Renamed entry");
  juce::String error;
  const auto clashing = fiddle::BridgeMapDocument::compile(renamed->toString(), error);
  if (!clashing) throw std::runtime_error(error.toStdString());
  auto clash = project; clash.documents.erase(unpitched->contentHash);
  clash.documents[clashing->contentHash] = clashing; clash.assignments[{0, 2}] = clashing->contentHash;
  result = generator.generateAndInstallFiles(assignments, 3, {}, clash.serialize());
  CHECK(result.failed() && result.getErrorMessage().contains(unpitched->percussionMapId));
}

} // namespace

int main() {
  std::cout << "===== Dorico Config Generator Tests =====" << std::endl;
  const auto previousDirectory = juce::File::getCurrentWorkingDirectory();
  juce::File(FIDDLE_TEST_ROOT).setAsCurrentWorkingDirectory();
  try {
    testSparseStableDestinationsAreUsedAsEndpointNumbers();
    testPlayerIndexFollowsOrdinalNotListOrder();
    testKitChairsMapOnlyTheirOwnInstruments();
    testKitBlockAndStandaloneBlockTakeSuccessiveIndices();
    testKitDefinitionsParseInKitOrder();
    testPinnedMapsInstallWithMatchingEndpointsAndPresets();
    testFailedPublicationRestoresExistingFiles();
    testPercussionMapReachesEndpointAndPreset();
  } catch (const std::exception &error) { ++failed; std::cerr << error.what() << '\n'; }
  previousDirectory.setAsCurrentWorkingDirectory();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
