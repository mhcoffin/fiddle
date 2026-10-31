#include "BridgeMapDocument.h"
#include "BridgeSections.h"
#include "NoteStreamTracker.h"
#include "ScoreTest.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

// Score tests: recorder and comparison. Each generated test is "played" the
// way Dorico plays it through the built-in bridge maps (switch commands,
// CC1 dynamics a few ms after the note-on, written lengths), decoded by the
// real NoteStreamTracker, recorded and compared: every test must pass, and
// deliberate faults must be reported on the right note and check.

using namespace fiddle;
using namespace fiddle::scoretest;
namespace {
#define CHECK(x) do { if (!(x)) throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x); } while (false)

constexpr double kRate = 48000;
constexpr uint64_t kStart = 10 * 48000; // the score starts 10 s into host time

juce::File root() { return juce::File(FIDDLE_TEST_ROOT); }
juce::var loadExpected(const juce::String &id) {
  return juce::JSON::parse(root().getChildFile("tests/score/generated/" + id + ".expected.json"));
}

std::shared_ptr<const BridgeMapDocument> bridgeFor(const juce::String &instrument) {
  static std::map<BridgeSection, std::shared_ptr<const BridgeMapDocument>> cache;
  static const std::map<BridgeSection, const char *> paths{
      {BridgeSection::Strings, "experiments/orchestral-strings-bridge/r5/Fiddle-Orchestral-Strings-r5.doricolib"},
      {BridgeSection::Woodwinds, "experiments/orchestral-winds-bridge/woodwinds-r3/Fiddle-Orchestral-Woodwinds-r3.doricolib"},
      {BridgeSection::Brass, "experiments/orchestral-winds-bridge/brass-r3/Fiddle-Orchestral-Brass-r3.doricolib"},
      {BridgeSection::PitchedPercussion,
       "experiments/orchestral-percussion-bridge/pitched-percussion-r2/Fiddle-Orchestral-PitchedPercussion-r2.doricolib"},
      {BridgeSection::UnpitchedPercussion,
       "experiments/unpitched-percussion-bridge/unpitched-percussion-r1/Fiddle-Orchestral-UnpitchedPercussion-r1.doricolib"}};
  const auto section = bridgeSectionForInstrument(instrument);
  CHECK(paths.count(section));
  auto &doc = cache[section];
  if (!doc) {
    juce::String error;
    doc = BridgeMapDocument::compile(root().getChildFile(paths.at(section)).loadFileAsString(), error);
    if (!doc) throw std::runtime_error(error.toStdString());
  }
  return doc;
}

MidiEvent event(Endpoint ep, uint64_t sample) {
  MidiEvent e; e.set_port(uint32_t(ep.first)); e.set_channel(uint32_t(ep.second)); e.set_host_sample_position(sample); return e;
}
MidiEvent cc(Endpoint ep, uint64_t sample, int number, int value) {
  auto e = event(ep, sample); e.mutable_cc()->set_controller_number(number); e.mutable_cc()->set_controller_value(value); return e;
}
MidiEvent noteOn(Endpoint ep, uint64_t sample, int pitch, int velocity) {
  auto e = event(ep, sample); e.mutable_note_on()->set_note_number(pitch); e.mutable_note_on()->set_velocity(velocity); return e;
}
MidiEvent noteOff(Endpoint ep, uint64_t sample, int pitch) {
  auto e = event(ep, sample); e.mutable_note_off()->set_note_number(pitch); return e;
}
MidiEvent transport(bool start, uint64_t sample) {
  auto e = event({0, 0}, sample);
  e.mutable_transport()->set_type(start ? MidiEvent_TransportEvent_Type_START : MidiEvent_TransportEvent_Type_STOP);
  return e;
}
uint64_t at(double ms) { return kStart + uint64_t(std::llround(ms * kRate / 1000.0)); }
// The CC1 value whose pinned linear level is `level` (inverse of bridgeDynamicLevel).
int valueFor(double level) {
  return int(std::lround(1 + 126 * (level - kBridgeDynamicsMinLevel) / (kBridgeDynamicsMaxLevel - kBridgeDynamicsMinLevel)));
}

// Per dimension, the value carrying the most wanted techniques and none of
// the unwanted ones, as Dorico chooses switches.
struct Choice { std::vector<int> values; std::set<std::string> techniques; bool ccDynamics = true; };
Choice choose(const CompiledBridgeMap &map, const juce::var &techniques) {
  std::set<std::string> want, avoid;
  for (const auto &t : *techniques["includes"].getArray()) want.insert(t.toString().toStdString());
  for (const auto &t : *techniques["excludes"].getArray()) avoid.insert(t.toString().toStdString());
  Choice c;
  for (const auto &d : map.dimensions) {
    int best = d.reset, bestScore = -1;
    size_t bestSize = 0;
    bool bestCC = false;
    for (const auto &[value, v] : d.values) {
      if (std::any_of(v.techniques.begin(), v.techniques.end(), [&](const auto &t) { return avoid.count(t); })) continue;
      const int score = int(std::count_if(v.techniques.begin(), v.techniques.end(), [&](const auto &t) { return want.count(t); }));
      // Ties: sustained (CC1) dynamics, then the fewest techniques.
      const bool cc = v.length < 0 || v.ccDynamics;
      if (score > bestScore || (score == bestScore && (cc > bestCC || (cc == bestCC && v.techniques.size() < bestSize)))) {
        best = value; bestScore = score; bestSize = v.techniques.size(); bestCC = cc;
      }
    }
    if (bestScore < 0) throw std::runtime_error("no bridge value avoids the unwanted techniques");
    c.values.push_back(best);
    const auto &v = d.values.at(best);
    c.techniques.insert(v.techniques.begin(), v.techniques.end());
    if (v.length >= 0) c.ccDynamics = v.ccDynamics;
  }
  for (const auto &t : want)
    if (!c.techniques.count(t)) throw std::runtime_error("the bridge cannot express " + t);
  return c;
}

// What Dorico would send for one test, in time order. `adjust` lets a test
// break one note on purpose (onset shift in ms, techniques, level).
struct Fault { int index = -1; double onsetShiftMs = 0; bool dropIncludes = false; double levelShift = 0; bool drop = false; };

struct Played {
  std::vector<std::pair<uint64_t, MidiEvent>> events;
  std::map<Endpoint, juce::String> instruments;
  BridgeMapProject project;
};

Played play(const juce::var &expected, const Fault &fault = {}) {
  Played p;
  std::map<juce::String, Endpoint> endpoints;
  int channel = 2;
  for (const auto &part : *expected["parts"].getArray()) {
    const Endpoint ep{0, channel++};
    endpoints[part["id"].toString()] = ep;
    p.instruments[ep] = part["instrument"].toString();
    const auto doc = bridgeFor(part["instrument"].toString());
    p.project.documents[doc->contentHash] = doc;
    p.project.assignments[ep] = doc->contentHash;
  }
  // Init and beacon, as Dorico sends at playback start.
  for (const auto &[ep, instrument] : p.instruments) {
    const auto &map = *bridgeFor(instrument)->compiled;
    for (const auto &d : map.dimensions) {
      const int value = d.number == 102 && !d.key ? 0 : d.reset;
      p.events.emplace_back(0, d.key ? noteOn(ep, kStart - 4800, d.number, value) : cc(ep, kStart - 4800, d.number, value));
    }
    for (size_t i = 0; i < map.beacon.size(); ++i)
      p.events.emplace_back(0, noteOn(ep, kStart - 4800, CompiledBridgeMap::beaconPitches[i], map.beacon[i]));
  }
  p.events.emplace_back(1, transport(true, 0));
  // Hairpin notes take levels evenly spaced between the hairpin's ends.
  std::map<std::pair<juce::String, int>, std::vector<int>> hairpins;
  const auto &notes = *expected["notes"].getArray();
  for (int i = 0; i < notes.size(); ++i)
    if (notes[i]["dynamic"].hasProperty("hairpin"))
      hairpins[{notes[i]["part"].toString(), int(notes[i]["dynamic"]["hairpin"])}].push_back(i);
  std::vector<std::pair<uint64_t, std::pair<int, MidiEvent>>> timed; // sample, (order, event)
  for (int i = 0; i < notes.size(); ++i) {
    const auto &n = notes[i];
    if (fault.drop && fault.index == i) continue;
    const auto ep = endpoints.at(n["part"].toString());
    const auto &map = *bridgeFor(p.instruments.at(ep))->compiled;
    auto techniques = n["techniques"];
    if (fault.dropIncludes && fault.index == i) {
      techniques = juce::JSON::parse(juce::JSON::toString(techniques));
      techniques.getDynamicObject()->setProperty("includes", juce::Array<juce::var>{});
    }
    const auto choice = choose(map, techniques);
    const double onset = double(n["onset_ms"]) + (fault.index == i ? fault.onsetShiftMs : 0);
    const double end = double(n["onset_ms"]) + double(n["duration_ms"]);
    const auto &dynamic = n["dynamic"];
    double level;
    if (dynamic.hasProperty("level")) level = dynamic["level"];
    else {
      const auto &group = hairpins.at({n["part"].toString(), int(dynamic["hairpin"])});
      const double from = dynamic["from"], to = dynamic["to"].isVoid() ? from : double(dynamic["to"]);
      const auto k = std::find(group.begin(), group.end(), i) - group.begin() + 1;
      level = from + (to - from) * double(k) / double(group.size() + 1);
    }
    if (fault.index == i) level += fault.levelShift;
    // Switches 10 ticks ahead (about 5 ms here); CC1 3 ms after the note-on.
    for (size_t d = 0; d < map.dimensions.size(); ++d) {
      const auto &dim = map.dimensions[d];
      const int v = choice.values[d];
      timed.push_back({at(onset - 5), {1, dim.key ? noteOn(ep, at(onset - 5), dim.number, v) : cc(ep, at(onset - 5), dim.number, v)}});
    }
    const int value = valueFor(level);
    timed.push_back({at(onset), {2, noteOn(ep, at(onset), int(n["pitch"]), choice.ccDynamics ? 100 : value)}});
    timed.push_back({at(onset + 3), {1, cc(ep, at(onset + 3), 1, value)}});
    if (const auto &during = dynamic["during"]; during.isObject()) {
      // Like Dorico, ramp toward where the hairpin's line is at the note's end.
      const double target = during.hasProperty("end_level") ? double(during["end_level"])
                            : level + (during["direction"].toString() == "up" ? 0.5 : -0.5);
      for (int step = 1; step <= 4; ++step) {
        const double t = onset + (end - onset) * step / 4.0 - (step == 4 ? 1.0 : 0.0);
        timed.push_back({at(t), {1, cc(ep, at(t), 1, valueFor(level + (target - level) * step / 4.0))}});
      }
    }
    timed.push_back({at(end), {0, noteOff(ep, at(end), int(n["pitch"]))}});
  }
  std::stable_sort(timed.begin(), timed.end(), [](const auto &a, const auto &b) {
    return a.first != b.first ? a.first < b.first : a.second.first < b.second.first;
  });
  for (auto &[sample, item] : timed) p.events.emplace_back(sample, item.second);
  p.events.emplace_back(at(double(expected["length_ms"]) + 100), transport(false, at(double(expected["length_ms"]) + 100)));
  return p;
}

struct Rig {
  NoteStreamTracker tracker;
  Recorder recorder;
  explicit Rig(const BridgeMapProject &project) {
    tracker.setBridgeMaps(project);
    tracker.setHostSampleRate(kRate);
    recorder.setSampleRate(kRate);
    NoteStreamTracker::Callbacks callbacks;
    callbacks.onNoteStarted = [this](const Note &n) { recorder.noteStarted(n); };
    callbacks.onNoteEnded = [this](const Note &n) { recorder.noteEnded(n); };
    callbacks.onMidiEvent = [this](const MidiEvent &e, uint64_t s, int) { recorder.midiEvent(e, s, false); };
    callbacks.onBridgeCommand = [this](const MidiEvent &e, uint64_t s) { recorder.midiEvent(e, s, true); };
    tracker.setCallbacks(std::move(callbacks));
  }
  void send(const Played &p) { for (const auto &[sample, e] : p.events) tracker.processEvent(e); }
};

juce::var runTest(const juce::String &id, const Fault &fault = {}) {
  const auto expected = loadExpected(id);
  CHECK(expected.isObject());
  const auto played = play(expected, fault);
  Rig rig(played.project);
  rig.recorder.arm(id, expected, played.instruments);
  rig.send(played);
  const auto take = rig.recorder.finish();
  CHECK(take && !rig.recorder.armed());
  return runRecord(runId(id, juce::Time(2026, 9, 21, 14, 32, 0)), *take);
}

juce::var noteResult(const juce::var &record, int index) { return record["comparison"]["notes"][index]; }
bool checkFailed(const juce::var &note, const char *name) {
  for (const auto &c : *note["checks"].getArray())
    if (c["check"].toString() == name) return !bool(c["ok"]);
  return false;
}

void everyTestPassesWhenPlayedAsWritten() {
  const auto index = juce::JSON::parse(root().getChildFile("tests/score/generated/index.json"));
  int tests = 0;
  for (const auto &test : *index["tests"].getArray()) {
    const auto id = test["id"].toString();
    const auto record = runTest(id);
    if (!bool(record["pass"])) {
      std::cerr << id << " failed:\n";
        }
    CHECK(bool(record["pass"]));
    CHECK(int(record["summary"]["passed"]) == int(test["notes"]));
    CHECK(std::abs(double(record["comparison"]["offset_ms"]) - kStart * 1000.0 / kRate) < 0.01);
    CHECK(record["run_id"].toString() == id + "-20261021-143200");
    CHECK(record["midi"].getArray()->size() > 0 && record["received"].getArray()->size() == int(test["notes"]));
    ++tests;
  }
  CHECK(tests >= 12);
}

void faultsAreReportedOnTheirNote() {
  // A note 12 ms late fails its onset (and its duration, 12 ms shorter).
  auto record = runTest("basic-rhythm", {3, 12.0});
  CHECK(!bool(record["pass"]) && int(record["summary"]["failed"]) == 1);
  CHECK(noteResult(record, 3)["status"].toString() == "fail");
  CHECK(checkFailed(noteResult(record, 3), "onset") && checkFailed(noteResult(record, 3), "duration"));
  CHECK(noteResult(record, 2)["status"].toString() == "pass");
  // Within tolerance passes.
  CHECK(bool(runTest("basic-rhythm", {3, 4.0})["pass"]));
  // A dropped note is missing; the rest still match.
  record = runTest("repeated-notes", {.index = 5, .drop = true});
  CHECK(int(record["summary"]["missing"]) == 1 && int(record["summary"]["passed"]) == int(record["summary"]["expected"]) - 1);
  CHECK(noteResult(record, 5)["status"].toString() == "missing");
  // A staccato that arrives without its technique.
  record = runTest("articulations", {.index = 1, .dropIncludes = true});
  CHECK(checkFailed(noteResult(record, 1), "techniques") && int(record["summary"]["failed"]) == 1);
  CHECK(record["failures"][0].toString().contains("missing pt.staccato"));
  // A wrong dynamic.
  record = runTest("dynamics-ladder", {.index = 4, .levelShift = 0.5});
  CHECK(checkFailed(noteResult(record, 4), "level") && int(record["summary"]["failed"]) == 1);
  // A hairpin note louder than the next one in a crescendo.
  const auto hairpins = loadExpected("hairpins");
  const auto &list = *hairpins["notes"].getArray();
  int first = -1;
  for (int i = 0; i + 1 < list.size(); ++i)
    if (list[i]["dynamic"].hasProperty("hairpin") && list[i]["dynamic"]["direction"] == "up"
        && list[i + 1]["dynamic"].hasProperty("hairpin")) { first = i; break; }
  CHECK(first >= 0);
  const auto clean = runTest("hairpins");
  const double step = double(noteResult(clean, first + 1)["received"]["level"])
                      - double(noteResult(clean, first)["received"]["level"]);
  CHECK(step > 0);
  record = runTest("hairpins", {.index = first, .levelShift = step + 0.3});
  CHECK(checkFailed(noteResult(record, first + 1), "hairpin_order"));
  CHECK(noteResult(record, first + 1)["status"].toString() == "fail");
}

void bindingsAndExtras() {
  // Two parts bound by their chairs' instruments, whatever the endpoints.
  const auto record = runTest("two-instruments");
  const auto &bindings = *record["comparison"]["bindings"].getArray();
  CHECK(bindings.size() == 2 && bindings[0]["bound_by"].toString() == "chair instrument");
  CHECK(bindings[1]["chair_instrument"].toString() == "instrument.wind.flute");
  // Horn plays through the brass bridge and arrives at sounding pitch (F3, written C4).
  const auto horn = runTest("horn");
  CHECK(bool(horn["pass"]) && horn["comparison"]["bindings"][0]["chair_instrument"].toString() == "instrument.brass.horn.f");
  CHECK(int(horn["received"][0]["pitch"]) == 53);
  // Timpani and marimba share the pitched percussion bridge; each binds to its own chair.
  const auto percussion = runTest("pitched-percussion");
  const auto &percussionBindings = *percussion["comparison"]["bindings"].getArray();
  CHECK(bool(percussion["pass"]) && percussionBindings.size() == 2);
  CHECK(percussionBindings[0]["chair_instrument"].toString() == "instrument.pitchedpercussion.timpani");
  CHECK(percussionBindings[1]["chair_instrument"].toString() == "instrument.pitchedpercussion.marimba");
  // Unpitched parts play on the percussion map's one pitch (36): hits by
  // velocity, tremolo rolls with CC1 dynamics on the tremolo base.
  const auto snare = runTest("snare-drum");
  CHECK(bool(snare["pass"]) && snare["comparison"]["bindings"][0]["chair_instrument"].toString() == "instrument.orchestral.unpitched.snare.drum");
  CHECK(int(snare["received"][0]["pitch"]) == 36);
  const auto cymbals = runTest("cymbals-and-triangle");
  CHECK(bool(cymbals["pass"]) && cymbals["comparison"]["bindings"].size() == 3);
  CHECK(cymbals["comparison"]["bindings"][0]["chair_instrument"].toString() == "instrument.orchestral.unpitched.suspended.cymbal");
  std::set<int> codes;
  for (const auto &n : *cymbals["received"].getArray()) codes.insert(int(n["pitch"]));
  CHECK(codes == std::set<int>({36}));

  // A note no test expects, and a note that never ends.
  const auto expected = loadExpected("basic-rhythm");
  auto played = play(expected);
  const Endpoint ep{0, 2};
  played.events.insert(played.events.end() - 1, {at(500), noteOn(ep, at(500), 90, 100)});
  played.events.insert(played.events.end() - 1, {at(600), noteOff(ep, at(600), 90)});
  played.events.insert(played.events.end() - 1, {at(19250), noteOn(ep, at(19250), 91, 100)}); // stop ends it
  Rig rig(played.project);
  rig.recorder.arm("basic-rhythm", expected, played.instruments);
  rig.send(played);
  const auto take = rig.recorder.finish();
  const auto withExtras = runRecord("x", *take);
  CHECK(int(withExtras["summary"]["extra"]) == 2 && int(withExtras["summary"]["passed"]) == int(expected["notes"].size()));
  const auto &extras = *withExtras["comparison"]["extras"].getArray();
  CHECK(extras[0]["pitch"] == juce::var(90) && extras[1]["duration_ms"].isVoid());

  // Chair instruments that match no part: a single part takes the only endpoint.
  auto unnamed = *take;
  unnamed.instruments = {{ep, "instrument.keyboard.piano"}};
  const auto byElimination = runRecord("y", unnamed);
  CHECK(byElimination["comparison"]["bindings"][0]["bound_by"].toString() == "only remaining endpoint");
}

void takes() {
  // A partial playthrough, then a full one: Done compares the latest.
  const auto expected = loadExpected("basic-rhythm");
  const auto full = play(expected);
  Rig rig(full.project);
  rig.recorder.arm("basic-rhythm", expected, full.instruments);
  for (size_t i = 0; i < full.events.size() / 3; ++i) rig.tracker.processEvent(full.events[i].second);
  rig.tracker.processEvent(transport(false, at(5000)));
  rig.send(full);
  const auto take = rig.recorder.finish();
  CHECK(take->discardedTakes == 1);
  CHECK(bool(runRecord("z", *take)["pass"]));
  // Not armed: nothing is recorded, and finish returns nothing.
  rig.send(full);
  CHECK(!rig.recorder.finish());
  rig.recorder.arm("basic-rhythm", expected, full.instruments);
  rig.recorder.cancel();
  CHECK(!rig.recorder.armed() && !rig.recorder.finish());
}

// The sample run record tests/score/run_report.py and the MCP adapter are
// tested against (tests/score/fixtures/sample-run.json): basic-rhythm with
// note 3 played 12 ms late and an extra note.
juce::var sampleRun() {
  const auto expected = loadExpected("basic-rhythm");
  auto played = play(expected, {3, 12.0});
  const Endpoint ep{0, 2};
  played.events.insert(played.events.end() - 1, {at(500), noteOn(ep, at(500), 90, 100)});
  played.events.insert(played.events.end() - 1, {at(600), noteOff(ep, at(600), 90)});
  Rig rig(played.project);
  rig.recorder.arm("basic-rhythm", expected, played.instruments);
  rig.send(played);
  const auto take = rig.recorder.finish();
  juce::DynamicObject::Ptr bridge = new juce::DynamicObject();
  bridge->setProperty("port", 1);
  bridge->setProperty("channel", 2);
  bridge->setProperty("instrument", "instrument.strings.violin");
  bridge->setProperty("bridge", true);
  bridge->setProperty("beacon", "matched");
  bridge->setProperty("attested", false);
  bridge->setProperty("invalid_commands", 0);
  bridge->setProperty("withheld_total", new juce::DynamicObject());
  juce::DynamicObject::Ptr context = new juce::DynamicObject();
  context->setProperty("bridges", juce::Array<juce::var>{bridge.get()});
  return runRecord("basic-rhythm-20261021-143200", *take, context.get());
}

// The committed sample keeps the record's shape, so the readers built on it
// notice when the record changes.
void sampleRunMatchesRecords() {
  const auto fixture = juce::JSON::parse(root().getChildFile("tests/score/fixtures/sample-run.json"));
  const auto fresh = sampleRun();
  CHECK(fixture.isObject());
  const auto keys = [](const juce::var &v) {
    juce::StringArray names;
    if (auto *o = v.getDynamicObject()) for (const auto &p : o->getProperties()) names.add(p.name.toString());
    names.sort(false);
    return names;
  };
  CHECK(keys(fixture) == keys(fresh));
  for (const auto *k : {"comparison", "summary"}) CHECK(keys(fixture[k]) == keys(fresh[k]));
  CHECK(keys(fixture["comparison"]["bindings"][0]) == keys(fresh["comparison"]["bindings"][0]));
  CHECK(keys(fixture["comparison"]["notes"][3]) == keys(fresh["comparison"]["notes"][3]));
  CHECK(keys(fixture["comparison"]["notes"][3]["checks"][0]) == keys(fresh["comparison"]["notes"][3]["checks"][0]));
  CHECK(keys(fixture["received"][0]) == keys(fresh["received"][0]));
  CHECK(fresh["failures"].size() == 2 && fixture["failures"] == fresh["failures"]);
}

void receivedNotes() {
  Note n;
  n.set_id(7); n.set_port(1); n.set_channel(4); n.set_note_number(61); n.set_start_velocity(80);
  n.set_start_sample(48000); n.set_duration_samples(24000); n.set_dynamics_mode(Note::CC);
  (*n.mutable_notation_techniques())["CC102:pt.staccato"] = "pt.staccato";
  (*n.mutable_notation_techniques())["CC103:pt.accent"] = "pt.accent";
  auto &lane = (*n.mutable_cc_automation())[1];
  for (auto [offset, value] : {std::pair<int, int>{0, 40}, {240, 64}, {4800, 70}}) {
    auto *p = lane.add_points(); p->set_offset_samples(offset); p->set_value(value);
  }
  const auto r = ReceivedNote::from(n, kRate, true);
  CHECK(r.endpoint == Endpoint(1, 4) && r.onsetMs == 1000 && *r.durationMs == 500);
  // CC1 within 10 ms of the note-on is its starting dynamic.
  CHECK(r.startValue() == 64 && std::abs(*r.startLevel() - bridgeDynamicLevel(64)) < 1e-9);
  CHECK(r.laterValues() == std::vector<int>{70});
  CHECK((r.techniques == std::vector<std::string>{"pt.accent", "pt.staccato"}));
  n.set_dynamics_mode(Note::VELOCITY);
  CHECK(ReceivedNote::from(n, kRate, false).startValue() == 80 && !ReceivedNote::from(n, kRate, false).durationMs);
  CHECK(pitchName(60) == "C4" && pitchName(73) == "C#5");
  CHECK(valueFor(0.5) == 68 && std::abs(bridgeDynamicLevel(valueFor(-1)) + 1) < 0.07);
}
} // namespace

int main(int argc, char **argv) {
  try {
    if (argc == 3 && juce::String(argv[1]) == "--write-fixture") {
      const juce::File out(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
      out.getParentDirectory().createDirectory();
      return out.replaceWithText(juce::JSON::toString(sampleRun(), false) + "\n") ? 0 : 1;
    }
    receivedNotes();
    everyTestPassesWhenPlayedAsWritten();
    faultsAreReportedOnTheirNote();
    bindingsAndExtras();
    takes();
    sampleRunMatchesRecords();
    std::cout << "ScoreTestTest passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
