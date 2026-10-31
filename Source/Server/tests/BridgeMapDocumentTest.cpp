#include "BridgeMapDocument.h"
#include "BridgeSections.h"
#include "NoteStreamTracker.h"
#include "NoteInspection.h"
#include "ProjectSettings.h"
#include <functional>
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
// The supported playback-override policy: all six note-duration options at
// 100%, so notes arrive at written length and carry that fact downstream.
void writtenDurations() {
  juce::String error;
  const auto source = fixture("experiments/orchestral-strings-bridge/r5/Fiddle-Orchestral-Strings-r5.doricolib");
  const auto doc = BridgeMapDocument::compile(source, error);
  if (!doc) throw std::runtime_error(error.toStdString());
  CHECK(doc->schema == 2 && doc->compiled->pinnedPlayback);
  const auto r4 = BridgeMapDocument::compile(
      fixture("experiments/orchestral-strings-bridge/r4/Fiddle-Orchestral-Strings-r4.doricolib"), error);
  CHECK(r4 && !r4->compiled->pinnedPlayback);

  const auto edited = [&](const std::function<void(juce::XmlElement &)> &change) {
    auto root = juce::parseXML(source);
    auto *map = root->getChildByName("expressionMapDefinitions")->getChildByName("entities")->getFirstChildElement();
    change(*map->getChildByName("playbackOptionsOverrides"));
    return BridgeMapDocument::compile(root->toString(), error);
  };
  const auto setValue = [](juce::XmlElement &row, const char *tag, const char *value) {
    auto *field = row.getChildByName(tag);
    field->deleteAllChildElements(); field->addTextElement(value);
  };
  CHECK(!edited([&](juce::XmlElement &o) { setValue(*o.getFirstChildElement(), "value", "int: 90"); }));
  CHECK(error.contains("int:100"));
  CHECK(!edited([&](juce::XmlElement &o) { setValue(*o.getChildElement(6), "value", "bool: true"); }));
  CHECK(error.contains("bool:false"));
  CHECK(!edited([](juce::XmlElement &o) { o.removeChildElement(o.getFirstChildElement(), true); }));
  CHECK(error.contains("all six"));
  CHECK(!edited([](juce::XmlElement &o) { o.removeChildElement(o.getChildElement(6), true); }));
  CHECK(error.contains("truncation off"));
  CHECK(!edited([&](juce::XmlElement &o) { setValue(*o.getChildElement(7), "value", "int: 0"); }));
  CHECK(error.contains("numTicksBeforeForSwitches"));
  CHECK(!edited([&](juce::XmlElement &o) {
    setValue(*o.getFirstChildElement(), "option", "timingOptions.timingHumanisePercent"); }));
  CHECK(error.contains("Unsupported bridge playback override"));
  CHECK(!edited([&](juce::XmlElement &o) {
    setValue(*o.getChildElement(1), "option", "timingOptions.noteDurationPercent"); }));
  // Reordering the rows is not a semantic change.
  const auto reordered = edited([](juce::XmlElement &o) {
    auto *first = o.getFirstChildElement(); o.removeChildElement(first, false); o.addChildElement(first); });
  CHECK(reordered && reordered->semanticHash == doc->semanticHash);

  BridgeMapProject project;
  project.documents[doc->contentHash] = doc;
  project.assignments[{0, 2}] = doc->contentHash;
  NoteStreamTracker tracker;
  tracker.setBridgeMaps(project);
  std::vector<Note> notes;
  NoteStreamTracker::Callbacks callbacks;
  callbacks.onNoteStarted = [&](const Note &note) { notes.push_back(note); };
  tracker.setCallbacks(callbacks);
  initialize(tracker, *doc->compiled);
  tracker.processEvent(cc(102, 2));
  tracker.processEvent(key(72, 90));
  CHECK(notes.size() == 1);
  CHECK(notes[0].notation_dimensions().at(kPinnedPlaybackDimension) == 1.0f);
}

// An unpitched library carries a percussion map beside the expression map:
// every instrument shares one small table (Natural and the roll kinds, all
// on one pitch) that makes Dorico play and sustain rolls, while strokes,
// rolls and dynamics arrive through the expression map as for any section.
void percussion() {
  juce::String error;
  const auto source = fixture("experiments/unpitched-percussion-bridge/unpitched-percussion-r2/Fiddle-Orchestral-UnpitchedPercussion-r2.doricolib");
  const auto doc = BridgeMapDocument::compile(source, error);
  if (!doc) throw std::runtime_error(error.toStdString());
  CHECK(doc->schema == 2 && doc->compiled->pinnedPlayback && doc->compiled->hasPercussionMap);
  CHECK(doc->percussionMapId == "drumkitmap.user.Fiddle.OrchestralUnpitchedPercussion.r2");
  CHECK(doc->percussionMapName.isNotEmpty());
  // Every entry (Natural and the ten roll kinds) sends pitch 36; strokes and
  // surfaces extend the pitched strike_location dimension on CC106.
  CHECK(doc->compiled->percussionPitches == std::set<int>{36});
  const auto strike = std::find_if(doc->compiled->dimensions.begin(), doc->compiled->dimensions.end(),
                                   [](const auto &d) { return !d.key && d.number == 106; });
  CHECK(strike != doc->compiled->dimensions.end() && strike->values.size() == 33);
  CHECK(strike->values.at(3).techniques == std::set<std::string>{"pt.rim"});
  CHECK(strike->values.at(8).techniques == std::set<std::string>{"pt.sideStick"});
  // Hits take velocity; every roll-family base takes CC1.
  const auto &bases = doc->compiled->dimensions[0].values;
  CHECK(bases.size() == 75);
  CHECK(!bases.at(2).ccDynamics && bases.at(2).techniques == std::set<std::string>{"pt.natural"});
  CHECK(bases.at(32).ccDynamics && bases.at(32).techniques == std::set<std::string>{"pt.tremolo"} && bases.at(32).length == 2);
  // A pitched map keeps its fingerprint and has no percussion map.
  const auto pitched = BridgeMapDocument::compile(
      fixture("experiments/orchestral-percussion-bridge/pitched-percussion-r2/Fiddle-Orchestral-PitchedPercussion-r2.doricolib"), error);
  CHECK(pitched && !pitched->compiled->hasPercussionMap && pitched->percussionMapId.isEmpty());

  // The probe library holds two percussion maps, and map A uses an entry
  // keyswitch; this schema rejects both.
  const auto probe = fixture("experiments/unpitched-percussion-bridge/probe/Fiddle-Orchestral-UnpitchedProbe-r1.doricolib");
  CHECK(!BridgeMapDocument::compile(probe, error) && error.contains("one percussion map"));
  auto probeRoot = juce::parseXML(probe);
  auto *probeMaps = probeRoot->getChildByName("drumKitNoteMapDefinitions")->getChildByName("entities");
  probeMaps->removeChildElement(probeMaps->getChildElement(1), true);
  CHECK(!BridgeMapDocument::compile(probeRoot->toString(), error));
  CHECK(error.contains("Unsupported percussion entry field: keyswitches"));
  const auto edited = [&](const std::function<void(juce::XmlElement &)> &change) {
    auto root = juce::parseXML(source);
    change(*root->getChildByName("drumKitNoteMapDefinitions")->getChildByName("entities")->getFirstChildElement());
    return BridgeMapDocument::compile(root->toString(), error);
  };
  const auto setText = [](juce::XmlElement &node, const char *tag, const char *value) {
    auto *field = node.getChildByName(tag);
    field->deleteAllChildElements(); field->addTextElement(value);
  };
  CHECK(!edited([&](juce::XmlElement &m) { setText(m, "version", "3"); }) && error.contains("version"));
  CHECK(!edited([&](juce::XmlElement &m) { setText(m, "applyStageTemplateSettings", "true"); }) && error.contains("Stage Template"));
  CHECK(!edited([&](juce::XmlElement &m) { setText(m, "entityID", doc->entityId.toRawUTF8()); }) && error.contains("own entity ID"));
  CHECK(!edited([&](juce::XmlElement &m) { m.createNewChildElement("futureBehavior"); }) && error.contains("futureBehavior"));
  // One instrument's table differing from the rest, in pitch or technique.
  CHECK(!edited([&](juce::XmlElement &m) { setText(*m.getChildByName("entries")->getFirstChildElement(), "pitch", "35"); })
        && error.contains("identical for every instrument"));
  CHECK(!edited([&](juce::XmlElement &m) { setText(*m.getChildByName("entries")->getFirstChildElement(), "techniqueIDs", "pt.accent"); })
        && error.contains("identical for every instrument"));
  CHECK(!edited([&](juce::XmlElement &m) { setText(*m.getChildByName("entries")->getFirstChildElement(), "pitch", "20"); })
        && error.contains("outside the musical range"));
  CHECK(!edited([&](juce::XmlElement &m) {
    auto *entries = m.getChildByName("entries");
    auto *copy = new juce::XmlElement(*entries->getFirstChildElement());
    setText(*copy, "pitch", "90"); entries->addChildElement(copy);
  }) && error.contains("Duplicate percussion technique set"));
  CHECK(!edited([&](juce::XmlElement &m) { setText(*m.getChildByName("entries")->getFirstChildElement(), "techniqueIDs", "pt.roll"); })
        && error.contains("Duplicate percussion technique set"));
  // Dropping Natural from every instrument.
  CHECK(!edited([&](juce::XmlElement &m) {
    for (auto *row : m.getChildByName("entries")->getChildIterator())
      if (row->getChildByName("techniqueIDs")->getAllSubText().trim() == "pt.natural") setText(*row, "techniqueIDs", "pt.natural+pt.accent");
  }) && error.contains("Natural entry"));
  // Format and display text are not identity; the table is.
  const auto renamed = edited([&](juce::XmlElement &m) { setText(m, "name", "Renamed by Dorico"); });
  CHECK(renamed && renamed->contentHash != doc->contentHash && renamed->semanticHash == doc->semanticHash);
  const auto recoded = edited([&](juce::XmlElement &m) {
    for (auto *row : m.getChildByName("entries")->getChildIterator())
      if (row->getChildByName("techniqueIDs")->getAllSubText().trim() == "pt.tremolo") setText(*row, "pitch", "37");
  });
  CHECK(recoded && recoded->semanticHash != doc->semanticHash && recoded->compiled->percussionPitches == std::set<int>({36, 37}));

  BridgeMapProject project;
  project.documents[doc->contentHash] = doc;
  project.assignments[{0, 2}] = doc->contentHash;
  BridgeMapProject restored;
  CHECK(BridgeMapProject::parse(project.serialize(), restored, error));
  CHECK(restored.documents.begin()->second->percussionMapId == doc->percussionMapId);
  const auto other = BridgeMapDocument::compile(source.replace("xmap.user.Fiddle.OrchestralUnpitchedPercussion.r2", "xmap.user.Fiddle.Other.r1"), error);
  CHECK(!other && error.contains("beacon")); // identity changed: restamp would be needed
  NoteStreamTracker tracker;
  tracker.setBridgeMaps(project);
  std::vector<Note> notes;
  NoteStreamTracker::Callbacks callbacks;
  callbacks.onNoteStarted = [&](const Note &note) { notes.push_back(note); };
  tracker.setCallbacks(callbacks);
  initialize(tracker, *doc->compiled);
  tracker.processEvent(cc(102, 2));
  tracker.processEvent(key(36, 68)); CHECK(notes.size() == 1);
  CHECK(!notes.back().notation_dimensions().count("percussion_code"));
  CHECK(notes.back().dynamics_mode() == Note::VELOCITY);
  CHECK(collectInputTechniqueIds(notes.back()) == std::set<std::string>{"pt.natural"});
  // A tremolo on the same pitch: its base names the roll and selects CC1 dynamics.
  tracker.processEvent(cc(102, 32));
  tracker.processEvent(key(36, 56)); CHECK(notes.size() == 2);
  CHECK(notes.back().dynamics_mode() == Note::CC);
  CHECK(collectInputTechniqueIds(notes.back()) == std::set<std::string>{"pt.tremolo"});
  // The same tremolo on the rim: the surface is an add-on, so one sustained
  // note carries both (in r1's pitch-code design this was strokes on a rim pitch).
  tracker.processEvent(cc(106, 3));
  tracker.processEvent(key(36, 72)); CHECK(notes.size() == 3);
  const std::set<std::string> rimTremolo{"pt.rim", "pt.tremolo"};
  CHECK(collectInputTechniqueIds(notes.back()) == rimTremolo);
  // Pitches the map never sends are withheld, after the range test.
  tracker.processEvent(key(70, 72)); CHECK(notes.size() == 3);
  CHECK(tracker.bridgeDiagnostics(0, 2)->lastWithheld == BridgeDecoderState::Withheld::UnmappedPitch);
  tracker.processEvent(key(20, 72)); CHECK(notes.size() == 3);
  CHECK(tracker.bridgeDiagnostics(0, 2)->lastWithheld == BridgeDecoderState::Withheld::OutsideRange);
  tracker.processEvent(key(36, 72)); CHECK(notes.size() == 4);
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
  // Dorico's default durations keep the V2/V3 form; others round-trip as V4.
  CHECK(settings.serialize().rfind("FIDDLE_SETTINGS_V3\n", 0) == 0);
  for (auto durations : {settings, ProjectSettings{}}) {
    durations.doricoDurationPercents = {80, 40, 20, 110, 100, 70};
    CHECK(durations.serialize().rfind("FIDDLE_SETTINGS_V4\n", 0) == 0);
    CHECK(ProjectSettings::deserialize(durations.serialize()) == durations);
    durations.doricoSwitchLeadTicks = 0;
    CHECK(ProjectSettings::deserialize(durations.serialize()) == durations);
  }
  ProjectSettings lead; lead.doricoSwitchLeadTicks = 30;
  CHECK(lead.serialize().rfind("FIDDLE_SETTINGS_V4\n", 0) == 0);
  CHECK(ProjectSettings::deserialize(lead.serialize()) == lead);
  ProjectSettings dynamics;
  dynamics.doricoDynamics.lowerPower = dynamics.doricoDynamics.upperPower = 1.75;
  dynamics.doricoDynamics.minLevel = -5;
  dynamics.doricoDynamics.accent = 0.6;
  CHECK(dynamics.serialize().rfind("FIDDLE_SETTINGS_V4\n", 0) == 0);
  CHECK(ProjectSettings::deserialize(dynamics.serialize()) == dynamics);
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
  writtenDurations();
  percussion();
  const auto section = [](const char *id) { return bridgeSectionForInstrument(id); };
  CHECK(section("instrument.strings.violin") == BridgeSection::Strings);
  CHECK(section("instrument.strings.violoncello") == BridgeSection::Strings);
  CHECK(section("instrument.strings.viol.bass") == BridgeSection::None); // not viola
  CHECK(section("instrument.strings.contrabass.d") == BridgeSection::Strings);
  CHECK(section("instrument.wind.flute.alto") == BridgeSection::Woodwinds);
  CHECK(section("instrument.wind.flute.pan") == BridgeSection::None);
  CHECK(section("instrument.wind.saxophone.tenor") == BridgeSection::Woodwinds);
  CHECK(section("instrument.wind.saxophone.soprillo") == BridgeSection::None);
  CHECK(section("instrument.wind.recorder.alto") == BridgeSection::None);
  CHECK(section("instrument.brass.horn.f") == BridgeSection::Brass);
  CHECK(section("instrument.brass.horn.tenor") == BridgeSection::None);
  CHECK(section("instrument.brass.tuba.wagner") == BridgeSection::Brass);
  CHECK(section("instrument.brass.sousaphone") == BridgeSection::None);
  CHECK(section("instrument.pitchedpercussion.timpani") == BridgeSection::PitchedPercussion);
  CHECK(section("instrument.pitchedpercussion.bells.tubular.alias.chimes") == BridgeSection::PitchedPercussion);
  CHECK(section("instrument.pitchedpercussion.almglocken") == BridgeSection::None);
  CHECK(section("instrument.keyboard.piano") == BridgeSection::None);
  CHECK(section("instrument.orchestral.unpitched.snare.drum") == BridgeSection::UnpitchedPercussion);
  CHECK(section("instrument.band.unpitched.tomtom.high.alias.tom-high") == BridgeSection::UnpitchedPercussion);
  CHECK(section("instrument.orchestral.unpitched.hihat.pedal") == BridgeSection::UnpitchedPercussion);
  CHECK(section("instrument.orchestral.unpitched.percussion.1line") == BridgeSection::None);
  CHECK(section("instrument.kit.drumset.basic") == BridgeSection::None);
  CHECK(section("instrument.unpitched.kick.drum") == BridgeSection::None); // not a stem
  for (const auto &info : bridgeSections()) {
    const auto dir = std::string(info.section == BridgeSection::Strings ? "experiments/orchestral-strings-bridge/r5/"
        : info.section == BridgeSection::Woodwinds ? "experiments/orchestral-winds-bridge/woodwinds-r3/"
        : info.section == BridgeSection::Brass ? "experiments/orchestral-winds-bridge/brass-r3/"
        : info.section == BridgeSection::PitchedPercussion ? "experiments/orchestral-percussion-bridge/pitched-percussion-r2/"
        : "experiments/unpitched-percussion-bridge/unpitched-percussion-r2/");
    const auto built = BridgeMapDocument::compile(fixture((dir + info.templateFile).c_str()), error);
    if (!built) throw std::runtime_error(std::string(info.templateFile) + ": " + error.toStdString());
    CHECK(built->schema == 2 && built->compiled->pinnedPlayback);
    CHECK((info.section == BridgeSection::UnpitchedPercussion) == built->compiled->hasPercussionMap);
    // The section's stems and the percussion map's instruments stay in lockstep.
    for (const auto &instrument : built->percussionInstruments)
      CHECK(section(instrument.toRawUTF8()) == info.section);
    if (info.section == BridgeSection::UnpitchedPercussion) CHECK(built->percussionInstruments.size() == 75);
  }
  std::cout << "Bridge compiler, persistence, corruption, runtime isolation and schema 2 passed\n";
}
} // namespace
int main() { try { tests(); return 0; } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 1; } }
