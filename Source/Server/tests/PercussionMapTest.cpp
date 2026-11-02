// Library percussion maps: parsing, Dorico's matching rule, and playback
// through PercussionMapAnnotator ahead of a layer's expression map.
#include "../AnnotatorChain.h"
#include "../ExpressionMapAnnotator.h"
#include "../ExpressionMapParser.h"
#include "../PercussionMap.h"
#include "../PercussionMapAnnotator.h"

#include <iostream>

static int gPass = 0, gFail = 0;
#define CHECK(expr)                                                                       \
  do {                                                                                    \
    if (expr) ++gPass;                                                                    \
    else {                                                                                \
      ++gFail;                                                                            \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #expr << std::endl;  \
    }                                                                                     \
  } while (0)
#define CHECK_EQ(a, b)                                                                    \
  do {                                                                                    \
    const auto va = (a);                                                                  \
    const auto vb = (b);                                                                  \
    if (va == vb) ++gPass;                                                                \
    else {                                                                                \
      ++gFail;                                                                            \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #a " == " #b " (got " \
                << va << " vs " << vb << ")" << std::endl;                                \
    }                                                                                     \
  } while (0)

using namespace fiddle;

namespace {

const juce::File kDir = juce::File::getCurrentWorkingDirectory().getChildFile("example-expression-maps/percussion");
const std::string kSnare = "instrument.orchestral.unpitched.snare.drum";
const std::string kHiHat = "instrument.orchestral.unpitched.hihat";

PercussionMap load(const char *file) {
  juce::String error;
  auto maps = PercussionMap::parseLibrary(kDir.getChildFile(file).loadFileAsString(), error);
  if (maps.size() != 1) {
    std::cerr << "cannot load " << file << ": " << error << std::endl;
    return {};
  }
  return maps.front();
}

int pitchFor(const PercussionMap &map, const std::string &instrument, std::set<std::string> techniques) {
  const auto match = map.resolve(instrument, techniques);
  return match.entry ? match.entry->pitch : -1;
}

PercussionMapEntry entry(std::string instrument, std::set<std::string> techniques, int pitch,
                         std::vector<int> keys = {}, std::string variant = {}) {
  PercussionMapEntry e;
  e.instrument = std::move(instrument);
  e.techniques = std::move(techniques);
  e.pitch = pitch;
  e.keyswitches = std::move(keys);
  e.name = joinTechniques(e.techniques);
  e.variant = std::move(variant);
  return e;
}

PercussionMap makeMap(std::vector<PercussionMapEntry> entries) {
  PercussionMap map;
  map.entityId = "drumkitmap.user.test";
  map.name = "Test";
  map.entries = std::move(entries);
  return map;
}

// A bridge note: pitch 36 and the decoded techniques, keyed by dimension.
fiddle::Note bridgeNote(uint64_t id, std::set<std::string> techniques, int velocity = 80) {
  fiddle::Note note;
  note.set_id(id);
  note.set_channel(1);
  note.set_note_number(36);
  note.set_start_velocity(velocity);
  int dim = 0;
  for (const auto &t : techniques)
    (*note.mutable_notation_techniques())["CC" + std::to_string(102 + dim++) + ":" + t] = t;
  return note;
}

void testParseVendorMaps() {
  const auto snare = load("VSL SY Percussion - Snare Drum (PM).doricolib");
  CHECK_EQ(snare.entityId, std::string("drumkitmap.user.vsl_sy_percussion_snare_drum"));
  CHECK_EQ(snare.name, std::string("VSL SY Percussion - Snare Drum"));
  CHECK_EQ(int(snare.entries.size()), 20);
  CHECK(snare.forSingleInstrument);
  CHECK_EQ(snare.entries[0].pitch, 48);
  CHECK_EQ(int(snare.entries[0].keyswitches.size()), 1);
  CHECK_EQ(snare.entries[0].keyswitches[0], 24);
  CHECK(snare.validate().isEmpty());

  const auto toms = load("VSL SY Percussion - Concert toms (PM).doricolib");
  CHECK_EQ(int(toms.instruments().size()), 5);
  CHECK(!toms.forSingleInstrument);

  // An expression-map-only library has no percussion maps.
  juce::String error;
  const auto none = PercussionMap::parseLibrary(
      kDir.getChildFile("VSL SY Percussion - Snare drums, Concert toms.doricolib").loadFileAsString(), error);
  CHECK(none.empty());
  CHECK(error.isNotEmpty());
}

void testRoundTrip() {
  const auto cymbal = load("VSL SY Percussion - Suspended cymbals (PM).doricolib");
  juce::String error;
  auto again = PercussionMap::parseLibrary(cymbal.toLibraryXml(), error);
  CHECK_EQ(int(again.size()), 1);
  CHECK(again.front().toDefinitionXml() == cymbal.toDefinitionXml());
  auto fromDefinition = PercussionMap::parseLibrary(cymbal.toDefinitionXml(), error);
  CHECK_EQ(int(fromDefinition.size()), 1);
  PercussionMap fromJson;
  CHECK(PercussionMap::fromJson(cymbal.toJson(), fromJson, error));
  CHECK(fromJson.toDefinitionXml() == cymbal.toDefinitionXml());
  // Technique order is canonical, as Dorico writes it.
  CHECK(cymbal.toDefinitionXml().contains("pt.bell+pt.right+pt.stickShaft"));
}

void testValidation() {
  juce::String error;
  PercussionMap map;
  CHECK(!PercussionMap::fromJson(juce::JSON::parse(R"({"entityId":"x","name":"X","entries":[]})"), map, error));
  CHECK(!PercussionMap::fromJson(juce::JSON::parse(
      R"({"entityId":"x","name":"X","entries":[{"instrument":"instrument.a","techniques":["pt.natural"],"pitch":200}]})"),
      map, error));
  CHECK(error.contains("pitch"));
  CHECK(!PercussionMap::fromJson(juce::JSON::parse(
      R"({"entityId":"x","name":"X","entries":[
          {"instrument":"instrument.a","techniques":["pt.natural"],"pitch":1},
          {"instrument":"instrument.a","techniques":["pt.natural"],"pitch":2}]})"),
      map, error));
  CHECK(error.contains("duplicate"));
  // The same techniques under a variant are a separate entry.
  CHECK(PercussionMap::fromJson(juce::JSON::parse(
      R"({"entityId":"x","name":"X","entries":[
          {"instrument":"instrument.a","techniques":["pt.natural"],"pitch":1},
          {"instrument":"instrument.a","techniques":["pt.natural"],"pitch":2,"variant":"electric"}]})"),
      map, error));
  CHECK(!PercussionMap::fromJson(juce::JSON::parse(
      R"({"entityId":"x","name":"X","entries":[{"instrument":"instrument.a","techniques":["pt.natural"],"pitch":1,"keyswitches":[128]}]})"),
      map, error));
}

void testMatching() {
  const auto snare = load("VSL SY Percussion - Snare Drum (PM).doricolib");
  // Natural, implied or explicit.
  CHECK_EQ(pitchFor(snare, kSnare, {}), 60);
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.natural"}), 60);
  // A stroke the bridge sends as an add-on beside the natural base.
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.natural", "pt.rim"}), 61);
  // An exact combination beats its parts.
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.natural", "pt.right", "pt.rim"}), 63);
  // Best subset: modifiers the map lacks are ignored.
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.natural", "pt.rimShot", "pt.accent", "pt.f"}), 66);
  // A technique the map lacks falls back to Natural.
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.natural", "pt.deadStroke"}), 60);
  // Rolls.
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.tremolo"}), 84);
  CHECK_EQ(pitchFor(snare, kSnare, {"pt.buzzRoll", "pt.snareOff"}), 65);

  // Ties go to the entry listed first.
  const auto tie = makeMap({entry(kSnare, {"pt.natural"}, 1), entry(kSnare, {"pt.rim"}, 2),
                            entry(kSnare, {"pt.shell"}, 3)});
  CHECK_EQ(pitchFor(tie, kSnare, {"pt.shell", "pt.rim"}), 2);
}

void testInstrumentSelection() {
  const auto toms = load("VSL SY Percussion - Concert toms (PM).doricolib");
  // The map lists alias IDs; a chair may hold the plain one.
  CHECK_EQ(pitchFor(toms, "instrument.band.unpitched.tomtom.high", {}), 96);
  CHECK_EQ(pitchFor(toms, "instrument.band.unpitched.tomtom.high.alias.tom-high", {"pt.rim"}), 97);
  CHECK_EQ(pitchFor(toms, "instrument.band.unpitched.tomtom.low", {"pt.tremolo"}), 41);
  // A multi-instrument map covers only its own instruments.
  CHECK_EQ(pitchFor(toms, kSnare, {}), -1);

  // A single-instrument map plays on whatever chair the layer is on.
  const auto snare = load("VSL SY Percussion - Snare Drum (PM).doricolib");
  const auto match = snare.resolve("instrument.orchestral.unpitched.side.drum", {"pt.rim"});
  CHECK(match.entry != nullptr && match.entry->pitch == 61);
  CHECK(match.instrumentFallback);
  CHECK(!snare.resolve(kSnare, {"pt.rim"}).instrumentFallback);

  // Variant entries stand in only without plain ones (Dorico's GM map).
  const auto gm = makeMap({entry(kSnare, {"pt.natural"}, 38), entry(kSnare, {"pt.natural"}, 40, {}, "electric"),
                           entry("instrument.unpitched.kick.drum.low", {"pt.natural"}, 99, {}, "soft")});
  CHECK_EQ(pitchFor(gm, kSnare, {}), 38);
  CHECK_EQ(pitchFor(gm, "instrument.unpitched.kick.drum.low", {}), 99);
}

void testNaturalEquivalent() {
  // Dorico's GM map has no Natural for the hi-hat: Closed is its natural,
  // and Dorico sends a Closed hi-hat note as Natural.
  const auto gm = makeMap({entry(kHiHat, {"pt.closed"}, 42), entry(kHiHat, {"pt.open2"}, 46),
                           entry("instrument.orchestral.unpitched.hihat.pedal", {"pt.natural"}, 44)});
  CHECK_EQ(pitchFor(gm, kHiHat, {"pt.natural"}), 42);
  CHECK_EQ(pitchFor(gm, kHiHat, {}), 42);
  CHECK_EQ(pitchFor(gm, kHiHat, {"pt.natural", "pt.open2"}), 46);
  CHECK_EQ(pitchFor(gm, kHiHat, {"pt.natural", "pt.halfOpen"}), 42);
  CHECK_EQ(percussionNaturalEquivalent("instrument.orchestral.unpitched.triangle"), std::string("pt.open2"));
  CHECK(percussionNaturalEquivalent(kSnare).empty());

  // No Natural entry and no natural equivalent: nothing plays.
  const auto rimOnly = makeMap({entry(kSnare, {"pt.rim"}, 61)});
  CHECK_EQ(pitchFor(rimOnly, kSnare, {"pt.natural"}), -1);
}

void testAnnotator() {
  auto snare = std::make_shared<const PercussionMap>(load("VSL SY Percussion - Snare Drum (PM).doricolib"));
  PercussionMapAnnotator annotator(snare, kSnare);
  AnnotatorContext ctx;

  auto rim = bridgeNote(1, {"pt.natural", "pt.rim"});
  annotator.onNoteStart(rim, ctx);
  CHECK(!annotator.noteSuppressed());
  CHECK_EQ(int(rim.note_number()), 61);
  CHECK_EQ(rim.pre_note_size(), 0);
  CHECK_EQ(annotator.lastAnnotationRecord()->percussion.entryName, std::string("Rim"));
  CHECK_EQ(annotator.lastAnnotationRecord()->percussion.pitch, 61);

  // Rim shot carries keyswitch 24: note-on with the note, off with it.
  auto shot = bridgeNote(2, {"pt.natural", "pt.rimShot"});
  annotator.onNoteStart(shot, ctx);
  CHECK_EQ(int(shot.note_number()), 66);
  CHECK_EQ(shot.pre_note_size(), 1);
  CHECK(shot.pre_note(0).type() == fiddle::MidiInstruction::NOTE_ON);
  CHECK_EQ(shot.pre_note(0).param1(), 24);
  CHECK_EQ(shot.pre_note(0).param2(), 127);
  CHECK_EQ(shot.pre_note(0).delay_ms(), 0);

  // The note-off arrives as Dorico's note (pitch 36) and leaves on the
  // pitch its note-on used, with the keyswitch released.
  auto shotEnd = bridgeNote(2, {"pt.natural"});
  annotator.onNoteEnd(shotEnd, ctx);
  CHECK_EQ(int(shotEnd.note_number()), 66);
  CHECK_EQ(shotEnd.post_note_size(), 1);
  CHECK(shotEnd.post_note(0).type() == fiddle::MidiInstruction::NOTE_OFF);
  CHECK_EQ(shotEnd.post_note(0).param1(), 24);
  auto rimEnd = bridgeNote(1, {});
  annotator.onNoteEnd(rimEnd, ctx);
  CHECK_EQ(int(rimEnd.note_number()), 61);
  CHECK_EQ(rimEnd.post_note_size(), 0);

  // A roll whose entry carries no roll is flagged.
  auto mapNoRoll = std::make_shared<const PercussionMap>(makeMap({entry(kSnare, {"pt.natural"}, 60)}));
  PercussionMapAnnotator plain(mapNoRoll, kSnare);
  auto roll = bridgeNote(3, {"pt.tremolo"});
  plain.onNoteStart(roll, ctx);
  CHECK_EQ(int(roll.note_number()), 60);
  CHECK(!plain.lastAnnotationRecord()->percussion.warning.empty());
}

void testWithheldNote() {
  auto rimOnly = std::make_shared<const PercussionMap>(makeMap({entry(kSnare, {"pt.rim"}, 61)}));
  AnnotatorChain chain;
  chain.add(std::make_unique<PercussionMapAnnotator>(rimOnly, kSnare));
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "EM";
  TechniqueCombination natural;
  natural.name = "Natural";
  natural.techniqueIDs = {"pt.natural"};
  natural.switchOnActions = {{SwitchActionType::KeySwitch, 21, 127}};
  em->combinations.push_back(natural);
  chain.add(std::make_unique<ExpressionMapAnnotator>(em));
  AnnotatorContext ctx;

  auto note = bridgeNote(7, {"pt.natural"});
  chain.onNoteStart(note, ctx);
  CHECK(chain.noteSuppressed());
  CHECK_EQ(note.pre_note_size(), 0); // the expression map never saw it
  CHECK(!chain.lastAnnotationRecord()->percussion.withheld.empty());
  auto end = bridgeNote(7, {"pt.natural"});
  chain.onNoteEnd(end, ctx);
  CHECK(chain.noteSuppressed());

  // The next covered note plays, and the expression map sees it.
  auto rim = bridgeNote(8, {"pt.natural", "pt.rim"});
  chain.onNoteStart(rim, ctx);
  CHECK(!chain.noteSuppressed());
  CHECK_EQ(int(rim.note_number()), 61);
  CHECK_EQ(rim.pre_note_size(), 1);
  CHECK(rim.pre_note(0).type() == fiddle::MidiInstruction::KEY_SWITCH);
  CHECK_EQ(chain.lastAnnotationRecord()->baseSwitchName, std::string("Natural"));
  CHECK_EQ(chain.lastAnnotationRecord()->percussion.pitch, 61);
}

void testWithVendorExpressionMap() {
  // VSL pairs the snare percussion map with an expression map that switches
  // beaters and snares off by keyswitch. Both see every technique.
  ExpressionMapData parsed;
  CHECK(parseExpressionMap(kDir.getChildFile("VSL SY Percussion - Snare drums, Concert toms.doricolib"), parsed));
  auto em = std::make_shared<ExpressionMapData>(parsed);
  auto pm = std::make_shared<const PercussionMap>(load("VSL SY Percussion - Snare Drum (PM).doricolib"));
  AnnotatorChain chain;
  chain.add(std::make_unique<PercussionMapAnnotator>(pm, kSnare));
  chain.add(std::make_unique<ExpressionMapAnnotator>(em));
  AnnotatorContext ctx;

  auto note = bridgeNote(1, {"pt.natural", "pt.rimShot", "pt.snareOff"});
  chain.onNoteStart(note, ctx);
  CHECK_EQ(int(note.note_number()), 66);
  std::set<int> keyswitches, momentary;
  for (const auto &inst : note.pre_note()) {
    if (inst.type() == fiddle::MidiInstruction::KEY_SWITCH) keyswitches.insert(inst.param1());
    if (inst.type() == fiddle::MidiInstruction::NOTE_ON) momentary.insert(inst.param1());
  }
  CHECK(keyswitches.count(21)); // natural (sticks) base
  CHECK(keyswitches.count(23)); // snares off add-on
  CHECK(momentary.count(24));   // the rim-shot entry's keyswitch
  const auto *record = chain.lastAnnotationRecord();
  CHECK_EQ(record->percussion.entryName, std::string("Rim shot"));
  CHECK_EQ(record->baseSwitchName.empty(), false);

  // A roll keeps CC1 dynamics from the bridge when the base takes CC.
  auto roll = bridgeNote(2, {"pt.tremolo"});
  roll.set_dynamics_mode(fiddle::Note::CC);
  auto *pt = (*roll.mutable_cc_automation())[1].add_points();
  pt->set_offset_samples(0);
  pt->set_value(70);
  chain.onNoteStart(roll, ctx);
  CHECK_EQ(int(roll.note_number()), 84);
}

} // namespace

int main() {
  if (!kDir.isDirectory()) {
    std::cerr << "fixtures not found: " << kDir.getFullPathName() << std::endl;
    return 1;
  }
  testParseVendorMaps();
  testRoundTrip();
  testValidation();
  testMatching();
  testInstrumentSelection();
  testNaturalEquivalent();
  testAnnotator();
  testWithheldNote();
  testWithVendorExpressionMap();
  std::cout << "PASS: " << gPass << "  FAIL: " << gFail << std::endl;
  return gFail > 0 ? 1 : 0;
}
