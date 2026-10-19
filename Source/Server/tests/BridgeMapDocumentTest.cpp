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
void initialize(NoteStreamTracker &tracker, const CompiledBridgeMap &profile, int port = 0) {
  for (const auto &d : profile.dimensions) {
    int value = d.number == 102 && !d.key ? 0 : d.reset;
    tracker.processEvent(d.key ? key(d.number, value, port) : cc(d.number, value, port));
  }
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
  std::cout << "Bridge compiler, persistence, corruption, and runtime isolation passed\n";
}
} // namespace
int main() { try { tests(); return 0; } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
