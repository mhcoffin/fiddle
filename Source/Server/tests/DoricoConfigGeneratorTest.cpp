#include "../DoricoConfigGenerator.h"
#include "../BridgeMapDocument.h"

#include <iostream>
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

} // namespace

int main() {
  std::cout << "===== Dorico Config Generator Tests =====" << std::endl;
  const auto previousDirectory = juce::File::getCurrentWorkingDirectory();
  juce::File(FIDDLE_TEST_ROOT).setAsCurrentWorkingDirectory();
  try {
    testSparseStableDestinationsAreUsedAsEndpointNumbers();
    testPlayerIndexFollowsOrdinalNotListOrder();
    testPinnedMapsInstallWithMatchingEndpointsAndPresets();
    testFailedPublicationRestoresExistingFiles();
  } catch (const std::exception &error) { ++failed; std::cerr << error.what() << '\n'; }
  previousDirectory.setAsCurrentWorkingDirectory();
  std::cout << "Passed: " << passed << std::endl;
  std::cout << "Failed: " << failed << std::endl;
  return failed == 0 ? 0 : 1;
}
