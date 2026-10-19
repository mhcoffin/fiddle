#include "NoteStreamTracker.h"
#include "ExpressionMapAnnotator.h"
#include "../../NativePlugin/RealtimeMidiEvent.h"
#include <iostream>
#include <stdexcept>

using namespace fiddle;
namespace {
int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (false)

juce::File root() { return juce::File(FIDDLE_TEST_ROOT).getChildFile("experiments/dorico-strings"); }
std::shared_ptr<const ExperimentalBridgeProfile> profile() {
  juce::String error;
  auto p = ExperimentalBridgeProfile::parse(juce::JSON::parse(root().getChildFile("generated/high-vibrato.decoder.json")), error);
  if (!p) throw std::runtime_error(error.toStdString());
  return p;
}
MidiEvent cc(int number, int value, int port = 0, int channel = 1, uint64_t time = 0) {
  MidiEvent e; e.set_port(port); e.set_channel(channel); e.set_host_sample_position(time);
  e.mutable_cc()->set_controller_number(number); e.mutable_cc()->set_controller_value(value); return e;
}
MidiEvent key(int pitch, int velocity, int port = 0, int channel = 1, uint64_t time = 0) {
  MidiEvent e; e.set_port(port); e.set_channel(channel); e.set_host_sample_position(time);
  e.mutable_note_on()->set_note_number(pitch); e.mutable_note_on()->set_velocity(velocity); return e;
}
MidiEvent off(int pitch, int port = 0, int channel = 1, uint64_t time = 1) {
  MidiEvent e; e.set_port(port); e.set_channel(channel); e.set_host_sample_position(time);
  e.mutable_note_off()->set_note_number(pitch); return e;
}
void initialize(NoteStreamTracker &tracker, int port = 0, int channel = 1) {
  for (int c : {102, 103, 107, 108}) tracker.processEvent(cc(c, c == 102 ? 2 : 0, port, channel));
  for (auto pair : {std::pair{0, 2}, {1, 64}, {2, 127}, {127, 1}})
    tracker.processEvent(key(pair.first, pair.second, port, channel));
  tracker.processEvent(cc(1, 70, port, channel));
}
std::set<std::string> ids(const juce::var &row) {
  std::set<std::string> result;
  for (const auto &id : *row["techniques"].getArray()) result.insert(id.toString().toStdString());
  return result;
}
void unitTests() {
  auto p = profile();
  CHECK(p->dimensions().size() == 8);
  for (const auto &d : p->dimensions()) {
    for (const auto &entry : d.values) {
      const auto value = entry.first;
      const auto decoded = entry.second;
      NoteStreamTracker t;
      CHECK(t.setExperimentalBridge(0, 1, p));
      initialize(t);
      t.processEvent(d.cc >= 0 ? cc(d.cc, value) : key(d.pitch, value));
      bool received = false;
      NoteStreamTracker::Callbacks callbacks;
      callbacks.onNoteStarted = [&](const Note &n) {
        received = true;
        CHECK(collectInputTechniqueIds(n) == decoded.techniques);
        CHECK(n.dynamics_mode() == Note::CC);
        CHECK(int(inputLengthCategory(n, 44100)) == (decoded.length < 0 ? 2 : decoded.length));
      };
      t.setCallbacks(callbacks); t.processEvent(key(72, 100)); CHECK(received);
    }
  }
  NoteStreamTracker tracker;
  CHECK(tracker.setExperimentalBridge(0, 1, p));
  CHECK(tracker.setExperimentalBridge(1, 1, p));
  CHECK(tracker.setExperimentalBridge(0, 16, p));
  CHECK(!tracker.setExperimentalBridge(16, 1, p));
  CHECK(!tracker.setExperimentalBridge(0, 0, p));
  std::vector<Note> notes;
  int raw = 0;
  NoteStreamTracker::Callbacks cb;
  cb.onNoteStarted = [&](const Note &n) { notes.push_back(n); };
  cb.onMidiEvent = [&](const MidiEvent &, uint64_t, int) { ++raw; };
  tracker.setCallbacks(cb);
  tracker.processEvent(key(72, 100)); CHECK(notes.empty());
  CHECK(tracker.experimentalErrors(0, 1).second == 1);
  initialize(tracker); initialize(tracker, 1); initialize(tracker, 0, 16);
  tracker.processEvent(cc(1, 90, 1));
  tracker.processEvent(key(127, 2));
  const int before = raw;
  tracker.processEvent(off(127)); tracker.processEvent(key(127, 0));
  CHECK(raw == before);
  tracker.processEvent(key(72, 100));
  tracker.processEvent(key(72, 100, 1));
  tracker.processEvent(key(72, 100, 0, 16));
  CHECK(notes.size() == 3);
  CHECK(collectInputTechniqueIds(notes[0]) == std::set<std::string>{"pt.nonVibrato"});
  CHECK(collectInputTechniqueIds(notes[1]).empty());
  CHECK(collectInputTechniqueIds(notes[2]).empty());
  CHECK(notes[0].cc_automation().at(1).points(0).value() == 70);
  CHECK(notes[1].cc_automation().at(1).points(0).value() == 90);
  CHECK(!tracker.setExperimentalBridge(0, 1, {}));
  // Subsequent commands must not rewrite already-started experimental notes.
  tracker.processEvent(key(127, 126));
  CHECK(collectInputTechniqueIds(tracker.getActiveNotes()[0]) == std::set<std::string>{"pt.nonVibrato"});
  tracker.processEvent(key(127, 3)); tracker.processEvent(key(72, 100));
  CHECK(notes.size() == 3 && tracker.experimentalErrors(0, 1).first == 1);
  tracker.processEvent(key(127, 126)); tracker.processEvent(key(72, 100));
  CHECK(notes.size() == 4 && collectInputTechniqueIds(notes.back()) == std::set<std::string>{"pt.vibrato"});
  tracker.processEvent(cc(102, 127)); tracker.processEvent(key(72, 100)); CHECK(notes.size() == 4);
  MidiEvent stop; stop.mutable_transport()->set_type(MidiEvent_TransportEvent_Type_STOP);
  tracker.processEvent(stop);
  CHECK(tracker.getActiveNotes().empty() && tracker.getCC(1, 1, 1) == 0);
  tracker.processEvent(key(72, 100)); CHECK(notes.size() == 4);
  initialize(tracker); tracker.processEvent(key(72, 100)); CHECK(notes.size() == 5);
  CHECK(tracker.consumesExperimentalCC(0, 1, 102));
  CHECK(!tracker.consumesExperimentalCC(0, 1, 1));
  CHECK(!tracker.consumesExperimentalCC(2, 1, 102));
  // No profile: low/high musical pitches remain ordinary notes.
  NoteStreamTracker legacy; legacy.setCallbacks(cb);
  legacy.processEvent(key(0, 1)); legacy.processEvent(key(127, 64)); CHECK(notes.size() == 7);
  legacy.processEvent(cc(1, 22)); legacy.processEvent(cc(1, 99, 1));
  CHECK(legacy.getCC(1, 1) == 22 && legacy.getCC(1, 1, 1) == 99);
  // Automation offsets use the host clock, not wall-clock session age.
  legacy.processEvent(key(60, 80, 1, 1, 900000));
  legacy.processEvent(cc(1, 33, 1, 1, 900100));
  CHECK(legacy.getActiveNotes().back().cc_automation().at(1).points(1).offset_samples() == 100);

  const auto json = juce::JSON::parse(root().getChildFile("generated/high-vibrato.decoder.json"));
  for (int mode = 0; mode < 6; ++mode) {
    auto bad = json.clone();
    if (mode == 0) bad.getDynamicObject()->setProperty("schema_version", 3);
    if (mode == 1) bad["key_switches"][0].getDynamicObject()->setProperty("on_velocity", 0);
    if (mode == 2) bad["key_dimensions"][0].getDynamicObject()->setProperty("note", 72);
    if (mode == 3) bad["key_dimensions"][0].getDynamicObject()->setProperty("note", 0);
    if (mode == 4) bad["key_switches"][0].getDynamicObject()->setProperty("technique", "nonVibrato");
    if (mode == 5) bad["tables"].getDynamicObject()->removeProperty("102");
    juce::String error; CHECK(!ExperimentalBridgeProfile::parse(bad, error)); CHECK(error.isNotEmpty());
  }

  // The bridge carries written duration. Shortened playback (e.g. staccato)
  // must not choose a different length-conditioned switch when releasing it.
  auto outputMap = std::make_shared<ExpressionMapData>();
  for (int length : {0, 2}) {
    TechniqueCombination c;
    c.name = std::to_string(length); c.techniqueIDs = {"pt.staccato"};
    c.condition = parseConditionString(length == 0 ? "NoteLength == kVeryShort" : "NoteLength == kMedium");
    c.switchOffActions.push_back({SwitchActionType::CC, 20, length});
    outputMap->combinations.push_back(c);
  }
  ExpressionMapAnnotator annotator(outputMap);
  Note n;
  n.set_note_number(72); n.set_start_velocity(90); n.set_duration_samples(1000);
  (*n.mutable_notation_techniques())["articulation"] = "pt.staccato";
  (*n.mutable_notation_dimensions())["dorico_length_category"] = 2;
  annotator.onNoteStart(n, {}); annotator.onNoteEnd(n, {});
  CHECK(n.post_note_size() == 1 && n.post_note(0).param2() == 2);
  n.clear_notation_dimensions(); n.clear_post_note();
  annotator.onNoteEnd(n, {});
  CHECK(n.post_note_size() == 1 && n.post_note(0).param2() == 0);
}

// Replay raw probe records through protobuf serialization, the production
// tracker and the production per-layer expression-map annotator. The caller
// must first validate completeness/timing with inspect_live.py. No TCP socket
// or vendor plug-in is involved in this deterministic test.
void replay(const juce::File &capture, int fromNote) {
  const auto expected = juce::JSON::parse(root().getChildFile("expected.json"));
  CHECK(fromNote >= 1 && fromNote <= expected.size());
  auto map = std::make_shared<ExpressionMapData>();
  std::map<std::set<std::string>, int> programs;
  for (const auto &row : *expected.getArray()) {
    auto set = ids(row);
    if (programs.count(set)) continue;
    int program = int(programs.size()) + 1;
    programs[set] = program;
    TechniqueCombination c; c.name = std::to_string(program);
    c.techniqueIDs = set.empty() ? std::set<std::string>{"pt.natural"} : set;
    c.volumeType = VolumeType::kCC; c.volumeCC = 1;
    c.switchOnActions.push_back({SwitchActionType::ProgramChange, program, 0});
    map->combinations.push_back(c);
  }
  ExpressionMapAnnotator layer1(map), layer2(map);
  NoteStreamTracker tracker; CHECK(tracker.setExperimentalBridge(0, 1, profile()));
  int index = fromNote - 1;
  NoteStreamTracker::Callbacks cb;
  cb.onNoteStarted = [&](const Note &input) {
    CHECK(index < expected.size()); CHECK(input.note_number() == 72);
    const auto wanted = ids(expected[index++]);
    CHECK(collectInputTechniqueIds(input) == wanted);
    CHECK(inputLengthCategory(input, 44100) == LengthCategory::Medium);
    for (auto *layer : {&layer1, &layer2}) {
      auto n = input;
      layer->onNoteStart(n, {});
      CHECK(layer->lastAnnotationRecord()->baseSwitchName == std::to_string(programs.at(wanted)));
      bool programFound = false;
      for (const auto &instruction : n.pre_note())
        if (instruction.type() == MidiInstruction::PROGRAM_CHANGE) {
          CHECK(instruction.param1() == programs.at(wanted)); programFound = true;
        }
      CHECK(programFound); // adjacent test notes deliberately change PT sets.
    }
  };
  tracker.setCallbacks(cb);
  juce::StringArray lines; lines.addLines(capture.loadFileAsString());
  bool playing = false, ended = false, header = false;
  int notes = 0, blockCount = 0;
  int expectedInputs = 0;
  std::set<int> sequences;
  std::pair<int, int> lastOrder{-1, -1};
  auto checkBlock = [&] {
    CHECK(int(sequences.size()) == expectedInputs);
    for (int i = 0; i < expectedInputs; ++i) CHECK(sequences.count(i) == 1);
  };
  for (const auto &line : lines) {
    if (line.isEmpty()) continue;
    const auto r = juce::JSON::parse(line);
    const auto kind = r["kind"].toString();
    if (kind == "header") {
      CHECK(!header && blockCount == 0 && int(r["schema"]) == 1 && bool(r["capture_only"]));
      header = true; continue;
    }
    CHECK(header && !ended);
    if (kind == "end") {
      checkBlock(); CHECK(int(r["capture_dropped"]) == 0);
      CHECK(int(r["blocks"]) == blockCount); ended = true; continue;
    }
    CHECK(!ended);
    if (kind == "block") {
      checkBlock(); sequences.clear(); expectedInputs = int(r["inputs"]); lastOrder = {-1, -1};
      CHECK(int(r["block"]) == blockCount++);
      CHECK(int(r["input_dropped"]) == 0 && int(r["input_invalid"]) == 0);
      bool next = bool(r["playing"]);
      if (next != playing) {
        MidiEvent e; e.mutable_transport()->set_type(next ? MidiEvent_TransportEvent_Type_START : MidiEvent_TransportEvent_Type_STOP);
        tracker.processEvent(e);
      }
      playing = next; continue;
    }
    CHECK(kind == "input" && bool(r["valid"]));
    CHECK(int(r["block"]) == blockCount - 1 && blockCount > 0);
    CHECK(sequences.insert(int(r["sequence"])).second);
    const std::pair<int, int> order{int(r["offset"]), int(r["sequence"])};
    CHECK(order > lastOrder); lastOrder = order;
    if (int(r["port"]) != 0 || int(r["channel"]) != 1) continue;
    const int type = int(r["type"]), a = int(r["data1"]), b = int(r["data2"]);
    const auto time = uint64_t(static_cast<juce::int64>(r["host_samples"]));
    if (type == 0) {
      if (!playing && a == 72) continue; // audition note, not this playback pass
      if (a == 72 && b) ++notes;
    } else if (type != 1 && type != 3) continue;
    RealtimeMidiEvent wire;
    wire.type = static_cast<RealtimeMidiEventType>(type);
    wire.timestampSamples = int(r["offset"]); wire.hostSamplePosition = time;
    wire.port = 0; wire.channel = 1; wire.data1 = a; wire.data2 = b;
    MidiEvent received; CHECK(received.ParseFromString(toMidiEvent(wire).SerializeAsString()));
    tracker.processEvent(received);
  }
  CHECK(ended && notes == expected.size() - fromNote + 1 && index == expected.size());
  CHECK(tracker.experimentalErrors(0, 1).first == 0 && tracker.experimentalErrors(0, 1).second == 0);
  std::cout << "Replayed " << notes << " notes through decoder and two layer annotators\n";
}
} // namespace
int main(int argc, char **argv) {
  try {
    unitTests();
    if (argc == 3) replay(juce::File(argv[1]), std::stoi(argv[2]));
    else CHECK(argc == 1);
    std::cout << "PASS " << checks << " experimental bridge checks\n"; return 0;
  } catch (const std::exception &e) { std::cerr << "FAIL " << e.what() << '\n'; return 1; }
}
