#include "BridgeMapDocument.h"
#include "NoteStreamTracker.h"
#include "NoteInspection.h"
#include "ProjectSettings.h"
#include <iostream>
#include <stdexcept>

using namespace fiddle;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (false)
juce::String fixture(const char *path) {
  return juce::File(FIDDLE_TEST_ROOT).getChildFile(path).loadFileAsString();
}
MidiEvent cc(int number, int value, int port = 0) {
  MidiEvent e; e.set_port(port); e.set_channel(2); e.mutable_cc()->set_controller_number(number); e.mutable_cc()->set_controller_value(value); return e;
}
MidiEvent key(int pitch, int velocity, int port = 0) {
  MidiEvent e; e.set_port(port); e.set_channel(2); e.mutable_note_on()->set_note_number(pitch); e.mutable_note_on()->set_velocity(velocity); return e;
}
MidiEvent at(MidiEvent e, uint64_t sample) { e.set_host_sample_position(sample); return e; }
void initialize(NoteStreamTracker &tracker, const CompiledBridgeMap &profile, int port = 0) {
  for (const auto &d : profile.dimensions) {
    int value = d.number == 102 && !d.key ? 0 : d.reset;
    tracker.processEvent(d.key ? key(d.number, value, port) : cc(d.number, value, port));
  }
  if (profile.schema >= 2)
    for (size_t i = 0; i < profile.beacon.size(); ++i)
      tracker.processEvent(key(CompiledBridgeMap::beaconPitches[i], profile.beacon[i], port));
}

// Wire schema 2: typed Off, debounce, beacon, semantic identity, and the
// relaxed add-on gating. Strings r4 shares r3's allocation (CC105 vibrato).
void schemaTwo() {
  juce::String error;
  const auto source = fixture("experiments/orchestral-strings-bridge/r4/Fiddle-Orchestral-Strings-r4.doricolib");
  const auto doc = BridgeMapDocument::compile(source, error);
  if (!doc) throw std::runtime_error(error.toStdString());
  CHECK(doc->schema == 2 && doc->compiled->schema == 2);
  CHECK(doc->compiled->dimensions.size() == 33);
  CHECK(doc->compiled->beacon == BridgeMapDocument::beaconFor(doc->entityId, doc->revision));
  for (const auto *path : {"experiments/orchestral-winds-bridge/woodwinds-r2/Fiddle-Orchestral-Woodwinds-r2.doricolib",
                           "experiments/orchestral-winds-bridge/brass-r2/Fiddle-Orchestral-Brass-r2.doricolib"}) {
    const auto winds = BridgeMapDocument::compile(fixture(path), error);
    CHECK(winds && winds->schema == 2);
  }
  // A schema-2 map must use typed Offs; a plain reset is rejected.
  CHECK(!BridgeMapDocument::compile(source.replace("<param1>105</param1>\n                <param2>66</param2>",
                                                   "<param1>105</param1>\n                <param2>0</param2>"), error));
  CHECK(error.contains("typed Off"));
  // A stale beacon is rejected; stampBeacon repairs it from the identity.
  auto root = juce::parseXML(source);
  auto *map = root->getChildByName("expressionMapDefinitions")->getChildByName("entities")->getFirstChildElement();
  auto *version = map->getChildByName("version");
  version->deleteAllChildElements(); version->addTextElement("99");
  CHECK(!BridgeMapDocument::compile(root->toString(), error) && error.contains("beacon"));
  CHECK(BridgeMapDocument::stampBeacon(*map));
  const auto restamped = BridgeMapDocument::compile(root->toString(), error);
  CHECK(restamped && restamped->revision == 99 && restamped->semanticHash != doc->semanticHash);
  // Semantic identity ignores formatting, display text and row order.
  auto reordered = juce::parseXML(source);
  auto *def = reordered->getChildByName("expressionMapDefinitions")->getChildByName("entities")->getFirstChildElement();
  def->getChildByName("name")->deleteAllChildElements(); def->getChildByName("name")->addTextElement("Renamed by Dorico");
  auto *addons = def->getChildByName("techniqueAddOns");
  auto *first = addons->getFirstChildElement();
  addons->removeChildElement(first, false); addons->addChildElement(first);
  const auto equivalent = BridgeMapDocument::compile(reordered->toString(), error);
  CHECK(equivalent && equivalent->contentHash != doc->contentHash && equivalent->semanticHash == doc->semanticHash);
  // Schema 1 keeps its persisted fingerprint form, and is still decoded.
  const auto r3 = BridgeMapDocument::compile(fixture("experiments/orchestral-strings-bridge/r3/Fiddle-Orchestral-Strings-r3.doricolib"), error);
  CHECK(r3 && r3->schema == 1 && r3->compiled->beacon[0] == -1);

  BridgeMapProject project;
  project.documents[doc->contentHash] = doc;
  project.assignments[{0, 2}] = doc->contentHash;
  BridgeMapProject restored;
  CHECK(BridgeMapProject::parse(project.serialize(), restored, error));
  CHECK(restored.documents.begin()->second->schema == 2);

  NoteStreamTracker tracker;
  tracker.setBridgeMaps(project);
  std::vector<Note> notes;
  NoteStreamTracker::Callbacks callbacks;
  callbacks.onNoteStarted = [&](const Note &note) { notes.push_back(note); };
  int bridgeCommands = 0, statusChanges = 0;
  callbacks.onBridgeCommand = [&](const MidiEvent &, uint64_t) { ++bridgeCommands; };
  callbacks.onBridgeStatusChanged = [&] { ++statusChanges; };
  tracker.setCallbacks(callbacks);
  const auto vibrato = [&] {
    return collectInputTechniqueIds(notes.back());
  };
  // Without Init: not verified, base unknown.
  tracker.processEvent(key(72, 90)); CHECK(notes.empty());
  CHECK(tracker.bridgeDiagnostics(0, 2)->lastWithheld == BridgeDecoderState::Withheld::Unverified);
  // The beacon alone verifies; never-received add-ons count as unspecified.
  const auto &beacon = doc->compiled->beacon;
  tracker.processEvent(key(126, beacon[0])); tracker.processEvent(key(127, beacon[1]));
  CHECK(tracker.bridgeDiagnostics(0, 2)->beacon == BridgeDecoderState::Beacon::Matched);
  tracker.processEvent(key(72, 90)); CHECK(notes.empty());
  CHECK(tracker.bridgeDiagnostics(0, 2)->lastWithheld == BridgeDecoderState::Withheld::BaseUnknown);
  tracker.processEvent(cc(102, 2));
  tracker.processEvent(key(72, 90)); CHECK(notes.size() == 1);
  CHECK(vibrato() == std::set<std::string>{"pt.natural"});
  CHECK(bridgeCommands == 3 && statusChanges >= 2);

  // Replacement then the replaced technique's late Off: successor survives.
  tracker.processEvent(at(cc(105, 1), 1000));          // Non vibrato On
  tracker.processEvent(at(cc(105, 3), 50000));         // Vibrato On replaces it
  tracker.processEvent(at(cc(105, 1 + 64), 50010));    // Late Off(Non vibrato)
  tracker.processEvent(key(74, 90)); CHECK(notes.size() == 2 && vibrato().count("pt.vibrato"));
  CHECK(tracker.bridgeDiagnostics(0, 2)->ignoredOffs == 1);
  // Restated On followed 16 ms later by its own Off (base change): kept.
  tracker.processEvent(at(cc(102, 4), 131594));
  tracker.processEvent(at(cc(105, 3), 131594));
  tracker.processEvent(at(cc(105, 3 + 64), 132300));
  tracker.processEvent(key(76, 90)); CHECK(vibrato().count("pt.vibrato"));
  CHECK(tracker.bridgeDiagnostics(0, 2)->debouncedOffs == 1);
  // A genuine expiry well after its On clears the dimension.
  tracker.processEvent(at(cc(105, 3 + 64), 200000));
  tracker.processEvent(key(77, 90)); CHECK(!vibrato().count("pt.vibrato"));
  // Unknown values still invalidate rather than fall back.
  tracker.processEvent(cc(105, 40)); tracker.processEvent(key(79, 90));
  CHECK(notes.size() == 4);
  CHECK(tracker.bridgeDiagnostics(0, 2)->lastWithheld == BridgeDecoderState::Withheld::InvalidCommand);
  // Stop clears state and the beacon; a mismatching beacon blocks attestation.
  tracker.resetForTransportStop();
  CHECK(tracker.bridgeDiagnostics(0, 2)->beacon == BridgeDecoderState::Beacon::Absent);
  tracker.verifyBridge(0, 2);
  tracker.processEvent(cc(102, 2));
  tracker.processEvent(key(126, beacon[0])); tracker.processEvent(key(127, beacon[1] % 127 + 1));
  tracker.processEvent(key(72, 90)); CHECK(notes.size() == 4);
  CHECK(tracker.bridgeDiagnostics(0, 2)->lastWithheld == BridgeDecoderState::Withheld::BeaconMismatch);
  // Attestation alone still works when no beacon has arrived.
  tracker.resetForTransportStop();
  tracker.processEvent(cc(102, 2)); tracker.processEvent(key(72, 90)); CHECK(notes.size() == 5);
}
void tests() {
  juce::String error;
  const auto source = fixture("experiments/orchestral-strings-bridge/r3/Fiddle-Orchestral-Strings-r3.doricolib");
  auto strings = BridgeMapDocument::compile(source, error);
  if (!strings) throw std::runtime_error(error.toStdString());
  CHECK(strings->compiled->dimensions.size() == 33);
  CHECK(strings->contentHash.length() == 64);
  for (const auto &entry : {std::pair{"Woodwinds", 36}, std::pair{"Brass", 37}}) {
    const auto xml = fixture(("experiments/orchestral-winds-bridge/" + std::string(entry.first == std::string("Brass") ? "brass" : "woodwinds") + "-r1/Fiddle-Orchestral-" + entry.first + "-r1.doricolib").c_str());
    const auto doc = BridgeMapDocument::compile(xml, error);
    if (!doc) throw std::runtime_error(error.toStdString());
    CHECK(doc->compiled->dimensions.size() == size_t(entry.second));
  }
  // No code arithmetic: the category comes from the actual condition, even
  // when Medium's CC102 value is reassigned to a number not congruent to 2.
  auto root = juce::parseXML(source);
  auto *map = root->getChildByName("expressionMapDefinitions")->getChildByName("entities")->getFirstChildElement();
  auto *row = map->getChildByName("playingTechniqueCombinations")->getChildElement(2);
  auto *value = row->getChildByName("switchOnActions")->getFirstChildElement()->getChildByName("param2");
  value->deleteAllChildElements(); value->addTextElement("100");
  auto remapped = BridgeMapDocument::compile(root->toString(), error);
  CHECK(remapped);
  CHECK(remapped->compiled->dimensions[0].values.at(100).length == 2);
  CHECK(remapped->contentHash != strings->contentHash);
  BridgeMapProject project;
  project.documents[strings->contentHash] = strings;
  project.assignments[{0, 2}] = strings->contentHash;
  project.assignments[{1, 2}] = strings->contentHash;
  BridgeMapProject restored;
  CHECK(BridgeMapProject::parse(project.serialize(), restored, error));
  CHECK(restored.serialize() == project.serialize());
  ProjectSettings settings; settings.bridgeMapsJson = project.serialize().toStdString();
  CHECK(ProjectSettings::deserialize(settings.serialize()) == settings);
  CHECK(ProjectSettings::deserialize(ProjectSettings{}.serialize()) == ProjectSettings{});
  auto truncated = settings.serialize().substr(0, settings.serialize().find('{'));
  CHECK(!BridgeMapProject::parse(juce::String(ProjectSettings::deserialize(truncated).bridgeMapsJson), restored, error));
  auto corrupt = juce::JSON::parse(project.serialize());
  corrupt["documents"][0].getDynamicObject()->setProperty("contentHash", "wrong");
  CHECK(!BridgeMapProject::parse(juce::JSON::toString(corrupt), restored, error));
  CHECK(restored.serialize() == project.serialize()); // Failure is atomic.
  auto duplicateIdentity = project;
  duplicateIdentity.documents[remapped->contentHash] = remapped;
  CHECK(!BridgeMapProject::parse(duplicateIdentity.serialize(), restored, error));
  CHECK(restored.serialize() == project.serialize());
  auto unknown = juce::parseXML(source);
  auto *def = unknown->getChildByName("expressionMapDefinitions")->getChildByName("entities")->getFirstChildElement();
  def->createNewChildElement("futureBehavior")->addTextElement("1");
  CHECK(!BridgeMapDocument::compile(unknown->toString(), error));
  CHECK(error.contains("futureBehavior"));
  CHECK(!BridgeMapDocument::compile(source.replace("<velocityFactor>1.000000</velocityFactor>", "<velocityFactor>1junk</velocityFactor>"), error));
  CHECK(!BridgeMapDocument::compile(source.replace("<baseSwitchID>1</baseSwitchID>", "<baseSwitchID>0</baseSwitchID>"), error));
  CHECK(!BridgeMapDocument::compile(source.replace("<kScoreLibrary>", "<kScoreLibrary futureBehavior=\"true\">"), error));
  auto reserved = juce::JSON::parse(project.serialize());
  reserved["assignments"][0].getDynamicObject()->setProperty("channel", 1);
  CHECK(!BridgeMapProject::parse(juce::JSON::toString(reserved), restored, error));
  CHECK(restored.serialize() == project.serialize());
  CHECK(!BridgeMapDocument::compile(source.replace("<param2>2</param2>", "<param2>1</param2>"), error));
  CHECK(!BridgeMapDocument::compile(source, error, 0, 127));
  auto formatted = juce::parseXML(source)->toString(juce::XmlElement::TextFormat().singleLine());
  auto equivalent = BridgeMapDocument::compile(formatted, error);
  CHECK(equivalent && equivalent->contentHash == strings->contentHash);

  NoteStreamTracker tracker;
  tracker.setBridgeMaps(project);
  std::vector<Note> notes;
  NoteStreamTracker::Callbacks callbacks;
  callbacks.onNoteStarted = [&](const Note &note) { notes.push_back(note); };
  tracker.setCallbacks(callbacks);
  initialize(tracker, *strings->compiled);
  tracker.processEvent(key(72, 90)); CHECK(notes.empty()); // Not verified.
  tracker.verifyBridge(0, 2);
  tracker.processEvent(key(72, 90)); CHECK(notes.empty()); // Verify invalidates old state.
  initialize(tracker, *strings->compiled);
  tracker.processEvent(cc(102, 2)); tracker.processEvent(cc(105, 2));
  tracker.processEvent(key(72, 90)); CHECK(notes.size() == 1);
  CHECK(collectInputTechniqueIds(notes.back()).count("pt.conVibrato"));
  initialize(tracker, *strings->compiled, 1);
  tracker.processEvent(key(74, 90, 1)); CHECK(notes.size() == 1); // Port isolation.
  tracker.processEvent(cc(105, 127)); tracker.processEvent(key(76, 90)); CHECK(notes.size() == 1); // Invalid is not default.
  tracker.resetForTransportStop();
  tracker.processEvent(key(76, 90)); CHECK(notes.size() == 1);
  tracker.setBridgeMaps(project); // Checkout/reopen loses verification.
  initialize(tracker, *strings->compiled);
  tracker.processEvent(key(76, 90)); CHECK(notes.size() == 1);
  schemaTwo();
  std::cout << "Bridge compiler, persistence, corruption, runtime isolation and schema 2 passed\n";
}
} // namespace
int main() { try { tests(); return 0; } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
