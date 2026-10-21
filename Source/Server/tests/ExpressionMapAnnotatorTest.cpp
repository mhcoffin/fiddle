#include "../ExpressionMapAnnotator.h"
#include "../ExpressionMapData.h"
#include "../ExpressionMapParser.h"
#include "../NoteInspection.h"
#include "../BridgeReachability.h"
#include "../NoteStreamTracker.h"
#include "midi_event.pb.h"

#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness (same macros as parser test)
// ---------------------------------------------------------------------------
extern int gPass, gFail;

#define A_CHECK(expr)                                                          \
  do {                                                                         \
    if (expr) {                                                                \
      ++gPass;                                                                 \
    } else {                                                                   \
      ++gFail;                                                                 \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__ << "]: " #expr      \
                << std::endl;                                                  \
    }                                                                          \
  } while (0)

#define A_CHECK_EQ(a, b)                                                       \
  do {                                                                         \
    if ((a) == (b)) {                                                          \
      ++gPass;                                                                 \
    } else {                                                                   \
      ++gFail;                                                                 \
      std::cerr << "FAIL [" << __FILE__ << ":" << __LINE__                     \
                << "]: " #a " == " #b " (got " << (a) << " vs " << (b) << ")" \
                << std::endl;                                                  \
    }                                                                          \
  } while (0)

using namespace fiddle;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/// Build an ExpressionMapData with a single base combination.
static std::shared_ptr<ExpressionMapData>
makeSimpleEM(const std::string &name, const std::set<std::string> &techniques,
             std::vector<SwitchAction> switchOnActions = {},
             float velocityFactor = 1.0f, int transpose = 0,
             VolumeType volType = VolumeType::kNoteVelocity, int volCC = 1) {
  auto em = std::make_shared<ExpressionMapData>();
  em->name = name;
  TechniqueCombination tc;
  tc.name = name;
  tc.techniqueIDs = techniques;
  tc.switchOnActions = std::move(switchOnActions);
  tc.velocityFactor = velocityFactor;
  tc.transpose = transpose;
  tc.volumeType = volType;
  tc.volumeCC = volCC;
  em->combinations.push_back(std::move(tc));
  return em;
}

/// Build a Note with a single notation technique (display name).
static fiddle::Note makeNote(const std::string &technique, int velocity = 100,
                             int noteNum = 60) {
  fiddle::Note note;
  note.set_note_number(noteNum);
  note.set_start_velocity(velocity);
  (*note.mutable_notation_techniques())["articulation"] = technique;
  return note;
}

/// Build a Note with a pt.xxx technique ID directly.
static fiddle::Note makeNotePT(const std::string &ptID, int velocity = 100,
                               int noteNum = 60) {
  fiddle::Note note;
  note.set_note_number(noteNum);
  note.set_start_velocity(velocity);
  (*note.mutable_notation_techniques())["articulation"] = ptID;
  return note;
}

/// Build a Note with CC dynamics mode and an automation lane.
static fiddle::Note makeNoteWithCC(const std::string &ptID, int velocity,
                                   int ccNum,
                                   const std::vector<std::pair<int, int>> &points) {
  fiddle::Note note;
  note.set_note_number(60);
  note.set_start_velocity(velocity);
  note.set_dynamics_mode(fiddle::Note::CC);
  (*note.mutable_notation_techniques())["articulation"] = ptID;

  auto &lane = (*note.mutable_cc_automation())[ccNum];
  for (auto &[offset, value] : points) {
    auto *pt = lane.add_points();
    pt->set_offset_samples(offset);
    pt->set_value(value);
  }
  return note;
}

// ---------------------------------------------------------------------------
// Test: reverseHumanize (via onNoteStart with matching EM)
// ---------------------------------------------------------------------------

static void testReverseHumanize() {
  std::cout << "--- testReverseHumanize ---" << std::endl;

  // "Staccato" → match "pt.staccato"
  auto em = makeSimpleEM("Staccato", {"pt.staccato"},
                         {{SwitchActionType::KeySwitch, 24, 100}});
  ExpressionMapAnnotator ann(em);
  AnnotatorContext ctx;

  auto note = makeNote("Staccato");
  ann.onNoteStart(note, ctx);
  A_CHECK(note.pre_note_size() > 0); // should match and emit keyswitch

  // "pt.legato" (already a pt.xxx ID) → should match directly
  auto em2 = makeSimpleEM("Legato", {"pt.legato"},
                          {{SwitchActionType::KeySwitch, 36, 100}});
  ExpressionMapAnnotator ann2(em2);

  auto note2 = makeNotePT("pt.legato");
  ann2.onNoteStart(note2, ctx);
  A_CHECK(note2.pre_note_size() > 0);

  // Empty display string → no match
  auto em3 = makeSimpleEM("Natural", {"pt.natural"});
  ExpressionMapAnnotator ann3(em3);

  fiddle::Note note3;
  note3.set_note_number(60);
  note3.set_start_velocity(100);
  (*note3.mutable_notation_techniques())["articulation"] = "";
  ann3.onNoteStart(note3, ctx);
  A_CHECK_EQ(note3.pre_note_size(), 0); // empty string → no match

  // "Foo Bar-Baz" → "pt.foobarbaz"
  auto em4 = makeSimpleEM("FooBarBaz", {"pt.foobarbaz"},
                          {{SwitchActionType::KeySwitch, 48, 100}});
  ExpressionMapAnnotator ann4(em4);

  auto note4 = makeNote("Foo Bar-Baz");
  ann4.onNoteStart(note4, ctx);
  A_CHECK(note4.pre_note_size() > 0);
}

// ---------------------------------------------------------------------------
// Test: actionToInstruction (all 6 switch cases)
// ---------------------------------------------------------------------------

static void testActionToInstruction() {
  std::cout << "--- testActionToInstruction ---" << std::endl;
  AnnotatorContext ctx;

  // KeySwitch
  {
    auto em =
        makeSimpleEM("KS", {"pt.natural"},
                     {{SwitchActionType::KeySwitch, 24, 127}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).type(), fiddle::MidiInstruction::KEY_SWITCH);
    A_CHECK_EQ(note.pre_note(0).param1(), 24);
    A_CHECK_EQ(note.pre_note(0).param2(), 127);
  }

  // KeySwitch with param2=0 → should default to 100
  {
    auto em =
        makeSimpleEM("KS0", {"pt.staccato"},
                     {{SwitchActionType::KeySwitch, 30, 0}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.staccato");
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).param2(), 100); // default velocity
  }

  // CC
  {
    auto em =
        makeSimpleEM("CC", {"pt.legato"},
                     {{SwitchActionType::CC, 102, 5}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.legato");
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).type(), fiddle::MidiInstruction::CC);
    A_CHECK_EQ(note.pre_note(0).param1(), 102);
    A_CHECK_EQ(note.pre_note(0).param2(), 5);
  }

  // ControlChange (same output as CC)
  {
    auto em = makeSimpleEM("CtrlChange", {"pt.accent"},
                           {{SwitchActionType::ControlChange, 64, 127}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.accent");
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).type(), fiddle::MidiInstruction::CC);
  }

  // ProgramChange
  {
    auto em = makeSimpleEM("PC", {"pt.pizzicato"},
                           {{SwitchActionType::ProgramChange, 5, 0}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.pizzicato");
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).type(),
               fiddle::MidiInstruction::PROGRAM_CHANGE);
    A_CHECK_EQ(note.pre_note(0).param1(), 5);
  }

  // ChannelSwitch
  {
    auto em = makeSimpleEM("ChanSw", {"pt.tremolo"},
                           {{SwitchActionType::ChannelSwitch, 3, 0}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.tremolo");
    note.set_channel(1);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 0); // routes the note; no message
    A_CHECK_EQ(note.channel(), 3u);
  }

  // NoteVelocity (should NOT produce a pre_note instruction)
  {
    auto em = makeSimpleEM("NV", {"pt.marcato"},
                           {{SwitchActionType::NoteVelocity, 0, 0}});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.marcato");
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 0); // NoteVelocity is a no-op
  }
}

// ---------------------------------------------------------------------------
// Test: redundancy tracking (same match → no re-send)
// ---------------------------------------------------------------------------

static void testRedundancyTracking() {
  std::cout << "--- testRedundancyTracking ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM("Natural", {"pt.natural"},
                         {{SwitchActionType::KeySwitch, 24, 100}});
  ExpressionMapAnnotator ann(em);

  // First note: should emit keyswitch
  auto note1 = makeNotePT("pt.natural");
  ann.onNoteStart(note1, ctx);
  A_CHECK_EQ(note1.pre_note_size(), 1);

  // Second note, same technique: should NOT re-send
  auto note2 = makeNotePT("pt.natural");
  ann.onNoteStart(note2, ctx);
  A_CHECK_EQ(note2.pre_note_size(), 0);

  // Third note, different technique on a different annotator instance with
  // two combinations:
  auto em2 = std::make_shared<ExpressionMapData>();
  em2->name = "Multi";
  em2->combinations.push_back(
      TechniqueCombination{.name = "Natural",
                           .techniqueIDs = {"pt.natural"},
                           .switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}}});
  em2->combinations.push_back(
      TechniqueCombination{.name = "Staccato",
                           .techniqueIDs = {"pt.staccato"},
                           .switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}}});

  ExpressionMapAnnotator ann2(em2);

  auto note3a = makeNotePT("pt.natural");
  ann2.onNoteStart(note3a, ctx);
  A_CHECK_EQ(note3a.pre_note_size(), 1);
  A_CHECK_EQ(note3a.pre_note(0).param1(), 24);

  // Same → skip
  auto note3b = makeNotePT("pt.natural");
  ann2.onNoteStart(note3b, ctx);
  A_CHECK_EQ(note3b.pre_note_size(), 0);

  // Different → send
  auto note3c = makeNotePT("pt.staccato");
  ann2.onNoteStart(note3c, ctx);
  A_CHECK_EQ(note3c.pre_note_size(), 1);
  A_CHECK_EQ(note3c.pre_note(0).param1(), 36);

  // Back to first → send again
  auto note3d = makeNotePT("pt.natural");
  ann2.onNoteStart(note3d, ctx);
  A_CHECK_EQ(note3d.pre_note_size(), 1);
  A_CHECK_EQ(note3d.pre_note(0).param1(), 24);
}

// ---------------------------------------------------------------------------
// Test: velocity factor and transpose modifiers
// ---------------------------------------------------------------------------

static void testModifiers() {
  std::cout << "--- testModifiers ---" << std::endl;
  AnnotatorContext ctx;

  // Velocity factor = 0.5
  {
    auto em = makeSimpleEM("Soft", {"pt.natural"}, {}, 0.5f, 0);
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural", 100, 60);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.start_velocity(), 50u);
  }

  // Velocity factor = 2.0 (clamped to 127)
  {
    auto em = makeSimpleEM("Loud", {"pt.accent"}, {}, 2.0f, 0);
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.accent", 100, 60);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.start_velocity(), 127u);
  }

  // Velocity factor clamps to minimum 1
  {
    auto em = makeSimpleEM("Quiet", {"pt.natural"}, {}, 0.001f, 0);
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural", 10, 60);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.start_velocity(), 1u);
  }

  // Transpose = +12
  {
    auto em = makeSimpleEM("OctUp", {"pt.natural"}, {}, 1.0f, 12);
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural", 100, 60);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.note_number(), 72u);
  }

  // Transpose = -24 (clamped to 0)
  {
    auto em = makeSimpleEM("LowClamp", {"pt.natural"}, {}, 1.0f, -24);
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural", 100, 10);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.note_number(), 0u);
  }

  // Transpose = +100 (clamped to 127)
  {
    auto em = makeSimpleEM("HighClamp", {"pt.natural"}, {}, 1.0f, 100);
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural", 100, 80);
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.note_number(), 127u);
  }
}

// ---------------------------------------------------------------------------
// Test: emitDynamics — all 4 conversion cases
// ---------------------------------------------------------------------------

static void testEmitDynamicsVelocityToVelocity() {
  std::cout << "--- testEmitDynamics: velocity → velocity ---" << std::endl;
  AnnotatorContext ctx;

  // Default: velocity input + kNoteVelocity target → no change, no during_note
  auto em = makeSimpleEM("VelVel", {"pt.natural"}, {}, 1.0f, 0,
                         VolumeType::kNoteVelocity);
  ExpressionMapAnnotator ann(em);
  auto note = makeNotePT("pt.natural", 80);
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.start_velocity(), 80u);
  A_CHECK_EQ(note.during_note_size(), 0);
}

static void testEmitDynamicsCCToVelocity() {
  std::cout << "--- testEmitDynamics: CC → velocity ---" << std::endl;
  AnnotatorContext ctx;

  // CC input (dynamics_mode=CC, CC1 automation) + kNoteVelocity target
  // → initial CC1 value → start_velocity
  auto em = makeSimpleEM("CCVel", {"pt.natural"}, {}, 1.0f, 0,
                         VolumeType::kNoteVelocity);
  ExpressionMapAnnotator ann(em);

  auto note = makeNoteWithCC("pt.natural", 100, 1, {{0, 85}, {1000, 110}});
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.start_velocity(), 85u); // initial CC1 value
  A_CHECK_EQ(note.during_note_size(), 0); // no CC output
}

static void testEmitDynamicsVelocityToCC() {
  std::cout << "--- testEmitDynamics: velocity → CC ---" << std::endl;
  AnnotatorContext ctx;

  // Velocity input + kCC target → emit single CC at note start = velocity
  auto em = makeSimpleEM("VelCC", {"pt.natural"}, {}, 1.0f, 0,
                         VolumeType::kCC, 1);
  ExpressionMapAnnotator ann(em);

  auto note = makeNotePT("pt.natural", 95);
  note.set_dynamics_mode(fiddle::Note::VELOCITY);
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.during_note_size(), 1);
  A_CHECK_EQ(note.during_note(0).type(), fiddle::MidiInstruction::CC);
  A_CHECK_EQ(note.during_note(0).param1(), 1);  // CC1
  A_CHECK_EQ(note.during_note(0).param2(), 95); // velocity value
  A_CHECK_EQ(note.during_note(0).delay_ms(), 0);
}

static void testEmitDynamicsCCToCC() {
  std::cout << "--- testEmitDynamics: CC → CC ---" << std::endl;
  AnnotatorContext ctx;

  // CC input + kCC target → copy automation curve to during_note
  auto em = makeSimpleEM("CCCC", {"pt.natural"}, {}, 1.0f, 0,
                         VolumeType::kCC, 11);
  ExpressionMapAnnotator ann(em);

  auto note = makeNoteWithCC("pt.natural", 100, 1,
                             {{0, 80}, {44100, 110}, {88200, 40}});
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.during_note_size(), 3);

  // First point: offset=0 → 0ms
  A_CHECK_EQ(note.during_note(0).param1(), 11);  // target CC
  A_CHECK_EQ(note.during_note(0).param2(), 80);
  A_CHECK_EQ(note.during_note(0).delay_ms(), 0);

  // Second point: offset=44100 → 1000ms
  A_CHECK_EQ(note.during_note(1).param2(), 110);
  A_CHECK_EQ(note.during_note(1).delay_ms(), 1000);

  // Third point: offset=88200 → 2000ms
  A_CHECK_EQ(note.during_note(2).param2(), 40);
  A_CHECK_EQ(note.during_note(2).delay_ms(), 2000);
}

static void testEmitDynamicsCCToCCDifferentTarget() {
  std::cout << "--- testEmitDynamics: CC → CC (custom CC#) ---" << std::endl;
  AnnotatorContext ctx;

  // Use CC7 as the target CC
  auto em = makeSimpleEM("CC7", {"pt.natural"}, {}, 1.0f, 0,
                         VolumeType::kCC, 7);
  ExpressionMapAnnotator ann(em);

  auto note = makeNoteWithCC("pt.natural", 100, 1, {{0, 64}});
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.during_note_size(), 1);
  A_CHECK_EQ(note.during_note(0).param1(), 7); // CC7
  A_CHECK_EQ(note.during_note(0).param2(), 64);
}

static void testLiveCC1Dynamics() {
  std::cout << "--- testLiveCC1Dynamics ---" << std::endl;
  AnnotatorContext ctx;
  auto em = makeSimpleEM("CC11", {"pt.natural"}, {}, 1.0f, 0, VolumeType::kCC, 11);
  ExpressionMapAnnotator ann(em);
  fiddle::MidiEvent cc1;
  cc1.mutable_cc()->set_controller_number(1);
  cc1.mutable_cc()->set_controller_value(90);
  std::vector<fiddle::MidiInstruction> out;
  // Before any note there is no base: CC1 is consumed with no replacement.
  A_CHECK(!ann.onCC(cc1, ctx));
  ann.translateCC(cc1, ctx, out);
  A_CHECK_EQ((int)out.size(), 0);
  auto note = makeNoteWithCC("pt.natural", 100, 1, {{0, 64}});
  ann.onNoteStart(note, ctx);
  // A hairpin during the held note follows on the base's controller.
  A_CHECK(!ann.onCC(cc1, ctx));
  ann.translateCC(cc1, ctx, out);
  A_CHECK_EQ((int)out.size(), 1);
  A_CHECK_EQ(out[0].param1(), 11);
  A_CHECK_EQ(out[0].param2(), 90);
  // Velocity-mode input: CC1 is not a dynamics stream for this note.
  auto shortNote = makeNoteWithCC("pt.natural", 100, 1, {{0, 64}});
  shortNote.set_dynamics_mode(fiddle::Note::VELOCITY);
  ann.onNoteStart(shortNote, ctx);
  out.clear();
  ann.translateCC(cc1, ctx, out);
  A_CHECK_EQ((int)out.size(), 0);

  // Pitch bend: ±2 input passes through; a ±12 instrument is rescaled.
  fiddle::MidiEvent bend;
  bend.mutable_pitch_bend()->set_value(8192 + 4096); // +1 semitone at ±2
  A_CHECK(ann.onCC(bend, ctx));
  em->pitchBendRange = 12;
  A_CHECK(!ann.onCC(bend, ctx));
  out.clear();
  ann.translateCC(bend, ctx, out);
  A_CHECK_EQ((int)out.size(), 1);
  A_CHECK_EQ((out[0].param1() << 7) | out[0].param2(), 8192 + 683); // +1 of 12
}

// ---------------------------------------------------------------------------
// Test: resolveMatch Tier 2 (base + add-ons)
// ---------------------------------------------------------------------------

static void testResolveMatchTier2() {
  std::cout << "--- testResolveMatchTier2 (base + add-ons) ---" << std::endl;

  ExpressionMapData data;
  data.combinations.push_back(
      TechniqueCombination{.name = "Natural",
                           .techniqueIDs = {"pt.natural"},
                           .baseSwitchID = 1,
                           .isAddOn = false});
  data.combinations.push_back(
      TechniqueCombination{.name = "Muted (add-on)",
                           .techniqueIDs = {"pt.muted"},
                           .baseSwitchID = 0,
                           .isAddOn = true,
                           .switchOnActions = {{SwitchActionType::CC, 80, 127}}});

  // Looking for {pt.natural, pt.muted} → base=Natural + addon=Muted
  auto result = data.resolveMatch({"pt.natural", "pt.muted"});
  A_CHECK(result.base != nullptr);
  A_CHECK_EQ(result.base->name, std::string("Natural"));
  A_CHECK_EQ((int)result.addOns.size(), 1);
  A_CHECK_EQ(result.addOns[0]->name, std::string("Muted (add-on)"));

  // allSwitchOnActions should include add-on actions
  auto actions = result.allSwitchOnActions();
  bool foundAddonCC = false;
  for (auto &a : actions) {
    if (a.type == SwitchActionType::CC && a.param1 == 80)
      foundAddonCC = true;
  }
  A_CHECK(foundAddonCC);
}

// ---------------------------------------------------------------------------
// Test: bridge requests carry an implicit pt.natural that must not hide a
// more specific base, and equal-size subset bases are all tried.
// ---------------------------------------------------------------------------

static void testResolveMatchImplicitNatural() {
  std::cout << "--- testResolveMatchImplicitNatural ---" << std::endl;

  ExpressionMapData data;
  data.combinations.push_back(TechniqueCombination{
      .name = "Natural", .techniqueIDs = {"pt.natural"}, .baseSwitchID = 1});
  data.combinations.push_back(TechniqueCombination{
      .name = "Legato", .techniqueIDs = {"pt.legato"}, .baseSwitchID = 2});
  data.combinations.push_back(TechniqueCombination{
      .name = "Con sord", .techniqueIDs = {"pt.muted"}, .isAddOn = true,
      .switchOnActions = {{SwitchActionType::CC, 80, 127}}});

  // Previously chose Natural + Con sord and lost legato.
  auto result = data.resolveMatch({"pt.natural", "pt.legato", "pt.muted"});
  A_CHECK(result.base != nullptr);
  A_CHECK_EQ(result.base->name, std::string("Legato"));
  A_CHECK_EQ((int)result.addOns.size(), 1);
  A_CHECK_EQ(result.addOns[0]->name, std::string("Con sord"));

  // {natural, legato} with no exact base resolves to the Legato base.
  result = data.resolveMatch({"pt.natural", "pt.legato"});
  A_CHECK(result.base && result.base->name == "Legato");
  // A plain Natural note is unaffected.
  result = data.resolveMatch({"pt.natural"});
  A_CHECK(result.base && result.base->name == "Natural");
  // An exact compound base still wins over stripping Natural.
  data.combinations.push_back(TechniqueCombination{
      .name = "Natural legato", .techniqueIDs = {"pt.natural", "pt.legato"}, .baseSwitchID = 3});
  result = data.resolveMatch({"pt.natural", "pt.legato"});
  A_CHECK(result.base && result.base->name == "Natural legato");
}

// ---------------------------------------------------------------------------
// Test: resolveMatch natural fallback
// ---------------------------------------------------------------------------

static void testResolveMatchNaturalFallback() {
  std::cout << "--- testResolveMatchNaturalFallback ---" << std::endl;

  ExpressionMapData data;
  data.combinations.push_back(TechniqueCombination{
      .name = "Natural",
      .techniqueIDs = {"pt.natural"},
      .baseSwitchID = 1,
      .switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}}});
  data.combinations.push_back(
      TechniqueCombination{.name = "Staccato",
                           .techniqueIDs = {"pt.staccato"},
                           .baseSwitchID = 2});

  // Looking for {pt.pizzicato} → no overlap → falls back to Natural
  auto result = data.resolveMatch({"pt.pizzicato"});
  A_CHECK(result.base != nullptr);
  A_CHECK_EQ(result.base->name, std::string("Natural"));
}

// ---------------------------------------------------------------------------
// Test: null emData
// ---------------------------------------------------------------------------

static void testNaturalFallbackAddOnsAndCompoundLengths() {
  std::cout << "--- testNaturalFallbackAddOnsAndCompoundLengths ---" << std::endl;
  // Minimal vendor-shaped fixture: natural is implicit in incoming notes,
  // while the map spells it out and supplies a separate mute add-on.
  auto map = std::make_shared<ExpressionMapData>();
  A_CHECK(parseExpressionMapXml(R"xml(
    <root><expressionMapDefinitions><entities>
    <ExpressionMapDefinition><name>Fallback regression</name>
      <playingTechniqueCombinations>
        <playingTechniqueCombination><name>Natural long</name><techniqueIDs>pt.natural</techniqueIDs><conditionString>NoteLength &gt; kShort</conditionString></playingTechniqueCombination>
        <playingTechniqueCombination><name>Natural very short</name><techniqueIDs>pt.natural</techniqueIDs><conditionString>NoteLength == kVeryShort</conditionString></playingTechniqueCombination>
        <playingTechniqueCombination><name>Natural short</name><techniqueIDs>pt.natural</techniqueIDs><conditionString>NoteLength &lt;= kShort AND NoteLength &gt; kVeryShort</conditionString></playingTechniqueCombination>
        <playingTechniqueCombination><name>Staccato</name><techniqueIDs>pt.staccato</techniqueIDs></playingTechniqueCombination>
      </playingTechniqueCombinations>
      <techniqueAddOns><techniqueAddOn><name>Con Sordino</name><techniqueIDs>pt.muted</techniqueIDs>
        <switchOnActions><switchOnAction><type>kKeySwitch</type><param1>23</param1><param2>127</param2></switchOnAction></switchOnActions>
      </techniqueAddOn></techniqueAddOns>
    </ExpressionMapDefinition></entities></expressionMapDefinitions></root>)xml", *map));

  for (auto category : {LengthCategory::VeryShort, LengthCategory::Short,
                        LengthCategory::Medium, LengthCategory::Long,
                        LengthCategory::VeryLong}) {
    const std::string expected = category == LengthCategory::VeryShort ? "Natural very short"
        : category == LengthCategory::Short ? "Natural short" : "Natural long";
    for (const auto& input : std::vector<std::set<std::string>>{
             {}, {"pt.flautando"}, {"pt.muted"}, {"pt.muted", "pt.flautando"},
             {"pt.natural", "pt.muted"}}) {
      auto match = map->resolveMatch(input, category);
      A_CHECK(match.base != nullptr);
      if (match.base) A_CHECK_EQ(match.base->name, expected);
      A_CHECK_EQ(match.addOns.size(), input.count("pt.muted"));
      if (!match.addOns.empty()) A_CHECK_EQ(match.addOns.front()->name, std::string("Con Sordino"));
    }
  }
  // Existing explicit base + add-on selection remains unchanged.
  auto staccato = map->resolveMatch({"pt.staccato", "pt.muted"});
  A_CHECK(staccato.base && staccato.base->name == "Staccato");
  A_CHECK_EQ(staccato.addOns.size(), size_t(1));

  ExpressionMapAnnotator annotator(map);
  AnnotatorContext ctx;
  auto note = makeNotePT("pt.muted");
  (*note.mutable_notation_techniques())["extra"] = "pt.flautando";
  annotator.onNoteStart(note, ctx);
  auto* record = annotator.lastAnnotationRecord();
  A_CHECK(record != nullptr);
  if (record) {
    A_CHECK_EQ(record->addOnNames.size(), size_t(1));
    A_CHECK(record->unmatchedTechniques == std::set<std::string>{"pt.flautando"});
  }
  bool emittedMute = false;
  for (const auto& action : note.pre_note())
    emittedMute |= action.type() == MidiInstruction::KEY_SWITCH && action.param1() == 23;
  A_CHECK(emittedMute);
}

static void testCompoundConditionParsing() {
  std::cout << "--- testCompoundConditionParsing ---" << std::endl;
  for (const auto* expression : {
           "NoteLength <= kShort AND NoteLength > kVeryShort",
           "NoteLength > kVeryShort AND NoteLength <= kShort"}) {
    const auto condition = parseConditionString(expression);
    A_CHECK(!condition.empty());
    A_CHECK(!condition.evaluate(0.09));
    A_CHECK(!condition.evaluate(0.1875));
    A_CHECK(condition.evaluate(0.1876));
    A_CHECK(condition.evaluate(0.28));
    A_CHECK(condition.evaluate(0.375));
    A_CHECK(!condition.evaluate(0.3751));
    A_CHECK(!condition.evaluate(0.55));
  }
  for (const auto* expression : {
           "NoteLength == kShort OR NoteLength == kMedium",
           "NoteLength == kMedium OR NoteLength == kShort"}) {
    const auto condition = parseConditionString(expression);
    A_CHECK(!condition.evaluate(0.09));
    A_CHECK(condition.evaluate(0.28));
    A_CHECK(condition.evaluate(0.55));
    A_CHECK(!condition.evaluate(1.1));
  }
  // AND binds more tightly than OR. Whitespace is not semantically significant.
  const auto mixed = parseConditionString(
      "  NoteLength==kVeryShort OR\tNoteLength>kShort AND NoteLength<=kMedium  ");
  A_CHECK(mixed.evaluate(0.09));
  A_CHECK(!mixed.evaluate(0.28));
  A_CHECK(mixed.evaluate(0.55));
  A_CHECK(!mixed.evaluate(1.1));
  // Never silently turn a malformed length condition into an unconditional switch.
  for (const auto* expression : {"NoteLength > kUnknown", "NoteLength > kShort AND",
                                "NoteLength > kShort garbage"}) {
    auto condition = parseConditionString(expression);
    A_CHECK(!condition.empty());
    A_CHECK(!condition.evaluate(1.1));
  }
  // An empty-PT natural switch must not bypass its length condition via
  // the last-resort fallback (including a malformed condition).
  for (const auto* expression : {"NoteLength == kVeryShort", "NoteLength > kUnknown"}) {
    ExpressionMapData map;
    TechniqueCombination base;
    base.name = "Empty-PT natural";
    base.condition = parseConditionString(expression);
    map.combinations.push_back(base);
    A_CHECK(map.resolveMatch({"pt.muted"}, LengthCategory::Long).base == nullptr);
  }
}

static void testNullExpressionMap() {
  std::cout << "--- testNullExpressionMap ---" << std::endl;
  AnnotatorContext ctx;

  ExpressionMapAnnotator ann(nullptr);
  A_CHECK_EQ(ann.name(), std::string("ExpressionMap"));

  auto note = makeNotePT("pt.natural");
  ann.onNoteStart(note, ctx); // should not crash
  A_CHECK_EQ(note.pre_note_size(), 0);
}

// ---------------------------------------------------------------------------
// Test: no techniques on note → no match
// ---------------------------------------------------------------------------

static void testEmptyTechniques() {
  std::cout << "--- testEmptyTechniques ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM("Natural", {"pt.natural"},
                         {{SwitchActionType::KeySwitch, 24, 100}});
  ExpressionMapAnnotator ann(em);

  fiddle::Note note;
  note.set_note_number(60);
  note.set_start_velocity(100);
  // No notation_techniques set → collectTechniqueIDs returns empty,
  // expandWithDefaults has no MEGs to apply (test EM has none),
  // but resolveMatch falls back to pt.natural → match.
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.pre_note_size(), 1);
  A_CHECK_EQ(note.pre_note(0).param1(), 24); // Natural keyswitch
}

// ---------------------------------------------------------------------------
// Test: no match for technique → empty result
// ---------------------------------------------------------------------------

static void testNoMatch() {
  std::cout << "--- testNoMatch ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM("Staccato", {"pt.staccato"},
                         {{SwitchActionType::KeySwitch, 24, 100}});
  // Remove the natural fallback — only staccato exists
  ExpressionMapAnnotator ann(em);

  auto note = makeNotePT("pt.pizzicato");
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.pre_note_size(), 0); // no match, no keyswitch
}

// ---------------------------------------------------------------------------
// Test: MatchResult equality
// ---------------------------------------------------------------------------

static void testMatchResultEquality() {
  std::cout << "--- testMatchResultEquality ---" << std::endl;

  TechniqueCombination tc1{.name = "A", .techniqueIDs = {"pt.a"}};
  TechniqueCombination tc2{.name = "B", .techniqueIDs = {"pt.b"}};

  MatchResult r1{&tc1, {}};
  MatchResult r2{&tc1, {}};
  MatchResult r3{&tc2, {}};

  A_CHECK(r1 == r2);
  A_CHECK(r1 != r3);
  A_CHECK(r1 != MatchResult{}); // empty
}

// ---------------------------------------------------------------------------
// Test: cache behavior (verify second call uses cache)
// ---------------------------------------------------------------------------

static void testCacheBehavior() {
  std::cout << "--- testCacheBehavior ---" << std::endl;
  AnnotatorContext ctx;

  auto em = std::make_shared<ExpressionMapData>();
  em->name = "CacheTest";
  em->combinations.push_back(
      TechniqueCombination{.name = "Legato",
                           .techniqueIDs = {"pt.legato"},
                           .switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}}});
  em->combinations.push_back(
      TechniqueCombination{.name = "Staccato",
                           .techniqueIDs = {"pt.staccato"},
                           .switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}}});

  ExpressionMapAnnotator ann(em);

  // First use: legato
  auto n1 = makeNotePT("pt.legato");
  ann.onNoteStart(n1, ctx);
  A_CHECK_EQ(n1.pre_note_size(), 1);

  // Switch to staccato
  auto n2 = makeNotePT("pt.staccato");
  ann.onNoteStart(n2, ctx);
  A_CHECK_EQ(n2.pre_note_size(), 1);

  // Back to legato — uses cache, but still emits because match changed
  auto n3 = makeNotePT("pt.legato");
  ann.onNoteStart(n3, ctx);
  A_CHECK_EQ(n3.pre_note_size(), 1);
  A_CHECK_EQ(n3.pre_note(0).param1(), 24);
}

// ---------------------------------------------------------------------------
// Test: multiple switch-on actions
// ---------------------------------------------------------------------------

static void testMultipleSwitchOnActions() {
  std::cout << "--- testMultipleSwitchOnActions ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM(
      "MultiAction", {"pt.natural"},
      {{SwitchActionType::KeySwitch, 24, 100},
       {SwitchActionType::CC, 102, 5},
       {SwitchActionType::ProgramChange, 3, 0}});
  ExpressionMapAnnotator ann(em);

  auto note = makeNotePT("pt.natural");
  ann.onNoteStart(note, ctx);
  A_CHECK_EQ(note.pre_note_size(), 3);
  A_CHECK_EQ(note.pre_note(0).type(), fiddle::MidiInstruction::KEY_SWITCH);
  A_CHECK_EQ(note.pre_note(1).type(), fiddle::MidiInstruction::CC);
  A_CHECK_EQ(note.pre_note(2).type(), fiddle::MidiInstruction::PROGRAM_CHANGE);
}

// ---------------------------------------------------------------------------
// Test: onNoteEnd is a no-op
// ---------------------------------------------------------------------------

static void testOnNoteEnd() {
  std::cout << "--- testOnNoteEnd ---" << std::endl;
  AnnotatorContext ctx;
  ctx.sampleRate = 44100.0;

  // A note ending never emits switch MIDI: Off actions belong to the
  // switch change (testSwitchOffActionsOnChange).
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "OffTest";
  em->combinations.push_back(TechniqueCombination{
      .name = "Natural",
      .techniqueIDs = {"pt.natural"},
      .switchOffActions = {{SwitchActionType::CC, 1, 0}}});
  ExpressionMapAnnotator ann(em);
  auto note = makeNotePT("pt.natural");
  ann.onNoteStart(note, ctx);
  ann.onNoteEnd(note, ctx);
  A_CHECK_EQ(note.post_note_size(), 0);
}

// ---------------------------------------------------------------------------
// Test: parseConditionString
// ---------------------------------------------------------------------------

static void testConditionParsing() {
  std::cout << "--- testConditionParsing ---" << std::endl;

  // Empty string → no condition
  {
    auto nc = parseConditionString("");
    A_CHECK(nc.empty());
  }

  // No NoteLength keyword → no condition
  {
    auto nc = parseConditionString("Velocity > 100");
    A_CHECK(nc.empty());
  }

  // NoteLength > kShort
  {
    auto nc = parseConditionString("NoteLength > kShort");
    A_CHECK(!nc.empty());
    A_CHECK_EQ((int)nc.op, (int)NoteCondition::Operator::GT);
    A_CHECK_EQ((int)nc.threshold, (int)NoteCondition::Threshold::Short);
  }

  // NoteLength <= kMedium
  {
    auto nc = parseConditionString("NoteLength <= kMedium");
    A_CHECK_EQ((int)nc.op, (int)NoteCondition::Operator::LE);
    A_CHECK_EQ((int)nc.threshold, (int)NoteCondition::Threshold::Medium);
  }

  // NoteLength >= kLong
  {
    auto nc = parseConditionString("NoteLength >= kLong");
    A_CHECK_EQ((int)nc.op, (int)NoteCondition::Operator::GE);
    A_CHECK_EQ((int)nc.threshold, (int)NoteCondition::Threshold::Long);
  }

  // NoteLength < kVeryShort
  {
    auto nc = parseConditionString("NoteLength < kVeryShort");
    A_CHECK_EQ((int)nc.op, (int)NoteCondition::Operator::LT);
    A_CHECK_EQ((int)nc.threshold, (int)NoteCondition::Threshold::VeryShort);
  }

  // NoteLength == kVeryLong
  {
    auto nc = parseConditionString("NoteLength == kVeryLong");
    A_CHECK_EQ((int)nc.op, (int)NoteCondition::Operator::EQ);
    A_CHECK_EQ((int)nc.threshold, (int)NoteCondition::Threshold::VeryLong);
  }
}

// ---------------------------------------------------------------------------
// Test: NoteCondition::evaluate
// ---------------------------------------------------------------------------

static void testConditionEvaluation() {
  std::cout << "--- testConditionEvaluation ---" << std::endl;

  // No condition always passes
  {
    NoteCondition nc;
    A_CHECK(nc.evaluate(0.0));
    A_CHECK(nc.evaluate(999.0));
  }

  // NoteLength > kShort (threshold = 0.375)
  {
    NoteCondition nc;
    nc.op = NoteCondition::Operator::GT;
    nc.threshold = NoteCondition::Threshold::Short;
    A_CHECK(!nc.evaluate(0.1));   // 0.1 not > 0.375
    A_CHECK(!nc.evaluate(0.375)); // 0.375 not > 0.375
    A_CHECK(nc.evaluate(0.5));    // 0.5 > 0.375
  }

  // NoteLength <= kShort
  {
    NoteCondition nc;
    nc.op = NoteCondition::Operator::LE;
    nc.threshold = NoteCondition::Threshold::Short;
    A_CHECK(nc.evaluate(0.1));    // <= 0.375
    A_CHECK(nc.evaluate(0.375));  // == 0.375, passes <=
    A_CHECK(!nc.evaluate(0.5));   // > 0.375
  }

  // NoteLength < kMedium (threshold = 0.75)
  {
    NoteCondition nc;
    nc.op = NoteCondition::Operator::LT;
    nc.threshold = NoteCondition::Threshold::Medium;
    A_CHECK(nc.evaluate(0.5));
    A_CHECK(!nc.evaluate(0.75));
    A_CHECK(!nc.evaluate(1.0));
  }

  // NoteLength >= kLong (threshold = 1.5)
  {
    NoteCondition nc;
    nc.op = NoteCondition::Operator::GE;
    nc.threshold = NoteCondition::Threshold::Long;
    A_CHECK(!nc.evaluate(1.0));
    A_CHECK(nc.evaluate(1.5));
    A_CHECK(nc.evaluate(2.0));
  }

  // Threshold values
  A_CHECK(std::abs(NoteCondition::thresholdSeconds(
              NoteCondition::Threshold::VeryShort) - 0.1875) < 0.0001);
  A_CHECK(std::abs(NoteCondition::thresholdSeconds(
              NoteCondition::Threshold::Short) - 0.375) < 0.0001);
  A_CHECK(std::abs(NoteCondition::thresholdSeconds(
              NoteCondition::Threshold::Medium) - 0.75) < 0.0001);
  A_CHECK(std::abs(NoteCondition::thresholdSeconds(
              NoteCondition::Threshold::Long) - 1.5) < 0.0001);
}

// ---------------------------------------------------------------------------
// Test: annotator selects different combination based on note duration
// ---------------------------------------------------------------------------

static void testAnnotatorConditionFiltering() {
  std::cout << "--- testAnnotatorConditionFiltering ---" << std::endl;

  // Build EM with two entries sharing {pt.natural} but different conditions:
  //   "Natural [Short]"  → NoteLength <= kShort  → KeySwitch C1 (24)
  //   "Natural [Long]"   → NoteLength > kShort   → KeySwitch C2 (36)
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "CondTest";

  TechniqueCombination shortTC;
  shortTC.name = "Natural [Short]";
  shortTC.techniqueIDs = {"pt.natural"};
  shortTC.conditionString = "NoteLength <= kShort";
  shortTC.condition = parseConditionString(shortTC.conditionString);
  shortTC.switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}};

  TechniqueCombination longTC;
  longTC.name = "Natural [Long]";
  longTC.techniqueIDs = {"pt.natural"};
  longTC.conditionString = "NoteLength > kShort";
  longTC.condition = parseConditionString(longTC.conditionString);
  longTC.switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}};

  em->combinations.push_back(shortTC);
  em->combinations.push_back(longTC);

  AnnotatorContext ctx;
  ctx.sampleRate = 44100;

  // Short note: 0.2s = 8820 samples → should pick "Natural [Short]"
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    note.set_duration_samples(8820); // 0.2s at 44100
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).param1(), 24); // Short keyswitch
  }

  // Long note: 1.0s = 44100 samples → should pick "Natural [Long]"
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    note.set_duration_samples(44100); // 1.0s
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).param1(), 36); // Long keyswitch
  }

  // Boundary: exactly kShort (0.375s = 16537 samples) → passes <=, picks Short
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    note.set_duration_samples(16537); // ~0.375s
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).param1(), 24); // Short
  }

  // Zero duration → LengthCategory::VeryLong, resolveMatch picks
  // whichever condition passes for VeryLong.
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    // no set_duration_samples → defaults to 0
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
  }
}

// ---------------------------------------------------------------------------
// Test: durationAdjustMs (two-layer composing)
// ---------------------------------------------------------------------------

static void testDurationAdjust() {
  std::cout << "--- testDurationAdjust ---" << std::endl;
  AnnotatorContext ctx;
  ctx.sampleRate = 44100.0;

  // Build an EM with timingOptions like the VSL Duality example:
  // noteDurationPercent=85, staccatoDurationPercent=80, legatoDurationPercent=101.
  // Dorico has already applied its defaults (90/50/105%) to the received
  // duration, so each override corrects by override/default.
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "DurationTest";
  em->timingOptions[DurationCategory::Note] = 85;
  em->timingOptions[DurationCategory::Staccato] = 80;
  em->timingOptions[DurationCategory::Staccatissimo] = 80;
  em->timingOptions[DurationCategory::Legato] = 101;

  // Natural: lengthFactor=1.0 (default note)
  TechniqueCombination natural;
  natural.name = "Natural";
  natural.techniqueIDs = {"pt.natural"};
  natural.lengthFactor = 1.0f;
  em->combinations.push_back(natural);

  // Staccato: lengthFactor=0.8
  TechniqueCombination staccato;
  staccato.name = "Staccato";
  staccato.techniqueIDs = {"pt.staccato"};
  staccato.lengthFactor = 0.8f;
  em->combinations.push_back(staccato);

  // Legato: lengthFactor=1.0
  TechniqueCombination legato;
  legato.name = "Legato";
  legato.techniqueIDs = {"pt.legato"};
  legato.lengthFactor = 1.0f;
  em->combinations.push_back(legato);

  // Default (Natural): 85/90 × 1.0
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    note.set_duration_samples(44100); // 1000ms
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    double delta = ann.durationAdjustMs();
    // Expected: (85/90 - 1.0) * 1000ms = -55.6ms
    A_CHECK(std::abs(delta - (-1000.0 / 18.0)) < 0.1);
  }

  // Staccato: 80/50 × 0.8 = 1.28
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.staccato");
    note.set_duration_samples(44100); // 1000ms
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    double delta = ann.durationAdjustMs();
    // Expected: (1.6 * 0.8 - 1.0) * 1000ms = +280ms
    A_CHECK(std::abs(delta - 280.0) < 0.1);
  }

  // Legato: 101/105 × 1.0
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.legato");
    note.set_duration_samples(44100); // 1000ms
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    double delta = ann.durationAdjustMs();
    // Expected: (101/105 - 1.0) * 1000ms = -38.1ms
    A_CHECK(std::abs(delta - (-4000.0 / 105.0)) < 0.1);
  }

  // No overrides: Dorico's scaling is not applied a second time
  for (const auto *pt : {"pt.natural", "pt.staccato", "pt.staccatissimo",
                         "pt.legato", "pt.tenuto", "pt.marcato"}) {
    auto plain = makeSimpleEM("Plain", {pt});
    ExpressionMapAnnotator ann(plain);
    auto note = makeNotePT(pt);
    note.set_duration_samples(44100);
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    A_CHECK_EQ(ann.durationAdjustMs(), 0.0);
  }

  // Only the overridden category changes; lengthFactor still composes
  {
    auto partial = makeSimpleEM("Partial", {"pt.tenuto"});
    partial->combinations[0].lengthFactor = 0.5f;
    partial->timingOptions[DurationCategory::Staccato] = 30;
    ExpressionMapAnnotator ann(partial);
    auto note = makeNotePT("pt.tenuto");
    note.set_duration_samples(44100);
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    A_CHECK(std::abs(ann.durationAdjustMs() - (-500.0)) < 0.1);
  }

  // No EM → delta = 0
  {
    ExpressionMapAnnotator ann(nullptr);
    auto note = makeNotePT("pt.natural");
    note.set_duration_samples(44100);
    ann.onNoteEnd(note, ctx);
    A_CHECK_EQ(ann.durationAdjustMs(), 0.0);
  }

  // Zero duration → delta = 0
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.staccato");
    // default duration = 0
    ann.onNoteEnd(note, ctx);
    A_CHECK_EQ(ann.durationAdjustMs(), 0.0);
  }
}

// ---------------------------------------------------------------------------
// AnnotationRecord tests
// ---------------------------------------------------------------------------

static void testAnnotationRecordBasicMatch() {
  std::cout << "--- testAnnotationRecordBasicMatch ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM("Staccato", {"pt.staccato"},
                         {{SwitchActionType::KeySwitch, 24, 100}});
  ExpressionMapAnnotator ann(em);

  auto note = makeNotePT("pt.staccato");
  ann.onNoteStart(note, ctx);

  auto *rec = ann.lastAnnotationRecord();
  A_CHECK(rec != nullptr);
  A_CHECK_EQ(rec->baseSwitchName, std::string("Staccato"));
  A_CHECK(rec->baseTechniqueIDs.count("pt.staccato"));
  A_CHECK(rec->addOnNames.empty());
  A_CHECK(rec->unmatchedTechniques.empty());
  A_CHECK(!rec->redundancySkipped);
  A_CHECK_EQ(rec->inputTechniques.size(), (size_t)1);
  A_CHECK(rec->inputTechniques.count("pt.staccato"));
}

static void testAnnotationRecordBaseAndAddOn() {
  std::cout << "--- testAnnotationRecordBaseAndAddOn ---" << std::endl;
  AnnotatorContext ctx;

  auto em = std::make_shared<ExpressionMapData>();
  em->name = "Tier2Test";

  // Base: pt.natural
  TechniqueCombination base;
  base.name = "Natural";
  base.techniqueIDs = {"pt.natural"};
  base.switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}};
  em->combinations.push_back(base);

  // Add-on: pt.muted
  TechniqueCombination addOn;
  addOn.name = "Muted";
  addOn.techniqueIDs = {"pt.muted"};
  addOn.isAddOn = true;
  addOn.switchOnActions = {{SwitchActionType::ControlChange, 1, 64}};
  em->combinations.push_back(addOn);

  ExpressionMapAnnotator ann(em);

  // Note with both techniques → Tier 2 match
  fiddle::Note note;
  note.set_note_number(60);
  note.set_start_velocity(100);
  (*note.mutable_notation_techniques())["articulation"] = "pt.natural";
  (*note.mutable_notation_techniques())["technique"] = "pt.muted";
  ann.onNoteStart(note, ctx);

  auto *rec = ann.lastAnnotationRecord();
  A_CHECK(rec != nullptr);
  A_CHECK_EQ(rec->baseSwitchName, std::string("Natural"));
  A_CHECK_EQ(rec->addOnNames.size(), (size_t)1);
  A_CHECK_EQ(rec->addOnNames[0], std::string("Muted"));
  A_CHECK(rec->unmatchedTechniques.empty());
}

static void testAnnotationRecordPartialMatch() {
  std::cout << "--- testAnnotationRecordPartialMatch ---" << std::endl;
  AnnotatorContext ctx;

  auto em = std::make_shared<ExpressionMapData>();
  em->name = "Tier3Test";

  // Base "AB" covers pt.a and pt.b
  TechniqueCombination ab;
  ab.name = "AB";
  ab.techniqueIDs = {"pt.a", "pt.b"};
  ab.switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}};
  em->combinations.push_back(ab);

  // Base "CD" covers pt.c and pt.d
  TechniqueCombination cd;
  cd.name = "CD";
  cd.techniqueIDs = {"pt.c", "pt.d"};
  cd.switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}};
  em->combinations.push_back(cd);

  ExpressionMapAnnotator ann(em);

  // Note asks for pt.a + pt.c → neither base is a SUBSET of {pt.a,pt.c}
  // (AB has pt.b not in input, CD has pt.d not in input)
  // → no match, no natural fallback
  fiddle::Note note;
  note.set_note_number(60);
  note.set_start_velocity(100);
  (*note.mutable_notation_techniques())["articulation"] = "pt.a";
  (*note.mutable_notation_techniques())["technique"] = "pt.c";
  ann.onNoteStart(note, ctx);

  auto *rec = ann.lastAnnotationRecord();
  A_CHECK(rec != nullptr);
  // No base is a subset → baseSwitchName empty, no MIDI emitted
  A_CHECK(rec->baseSwitchName.empty());
  A_CHECK(rec->midiActionsEmitted.empty());
}

static void testAnnotationRecordRedundancy() {
  std::cout << "--- testAnnotationRecordRedundancy ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM("Staccato", {"pt.staccato"},
                         {{SwitchActionType::KeySwitch, 24, 100}});
  ExpressionMapAnnotator ann(em);

  // First note → not redundant
  auto note1 = makeNotePT("pt.staccato");
  ann.onNoteStart(note1, ctx);
  auto *rec1 = ann.lastAnnotationRecord();
  A_CHECK(rec1 != nullptr);
  A_CHECK(!rec1->redundancySkipped);

  // Second identical note → redundant
  auto note2 = makeNotePT("pt.staccato");
  ann.onNoteStart(note2, ctx);
  auto *rec2 = ann.lastAnnotationRecord();
  A_CHECK(rec2 != nullptr);
  A_CHECK(rec2->redundancySkipped);
}

static void testAnnotationRecordConditionFallback() {
  std::cout << "--- testAnnotationRecordConditionFallback ---" << std::endl;

  // Build EM with condition: "Natural [Short]" only for short notes.
  // A long note should fail the condition and fall back.
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "CondFallback";

  TechniqueCombination shortTC;
  shortTC.name = "Natural [Short]";
  shortTC.techniqueIDs = {"pt.natural"};
  shortTC.conditionString = "NoteLength <= kShort";
  shortTC.condition = parseConditionString(shortTC.conditionString);
  shortTC.switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}};
  em->combinations.push_back(shortTC);

  TechniqueCombination longTC;
  longTC.name = "Natural [Long]";
  longTC.techniqueIDs = {"pt.natural"};
  longTC.conditionString = "NoteLength > kShort";
  longTC.condition = parseConditionString(longTC.conditionString);
  longTC.switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}};
  em->combinations.push_back(longTC);

  AnnotatorContext ctx;
  ctx.sampleRate = 44100;

  // Long note: 1s → fails "Short" condition → falls back to "Long"
  ExpressionMapAnnotator ann(em);
  auto note = makeNotePT("pt.natural");
  note.set_duration_samples(44100); // 1.0s
  ann.onNoteStart(note, ctx);

  auto *rec = ann.lastAnnotationRecord();
  A_CHECK(rec != nullptr);
  A_CHECK_EQ(rec->baseSwitchName, std::string("Natural [Long]"));
}

static void testAnnotationRecordModifiers() {
  std::cout << "--- testAnnotationRecordModifiers ---" << std::endl;
  AnnotatorContext ctx;

  auto em = makeSimpleEM("Modified", {"pt.staccato"},
                         {{SwitchActionType::KeySwitch, 24, 100}},
                         /* velocityFactor */ 0.5f,
                         /* transpose */ 12);
  ExpressionMapAnnotator ann(em);

  auto note = makeNotePT("pt.staccato", /* velocity */ 100, /* noteNum */ 60);
  ann.onNoteStart(note, ctx);

  auto *rec = ann.lastAnnotationRecord();
  A_CHECK(rec != nullptr);
  A_CHECK_EQ(rec->velocityBefore, 100);
  A_CHECK_EQ(rec->velocityAfter, 50); // 100 * 0.5
  A_CHECK_EQ(rec->transposeApplied, 12);
  A_CHECK_EQ(note.note_number(), 72); // 60 + 12
}

static void testAnnotationRecordDuration() {
  std::cout << "--- testAnnotationRecordDuration ---" << std::endl;
  AnnotatorContext ctx;
  ctx.sampleRate = 44100.0;

  auto em = std::make_shared<ExpressionMapData>();
  em->name = "DurTest";
  em->timingOptions[DurationCategory::Note] = 45; // half of Dorico's 90%

  TechniqueCombination tc;
  tc.name = "Natural";
  tc.techniqueIDs = {"pt.natural"};
  tc.lengthFactor = 1.0f;
  em->combinations.push_back(tc);

  ExpressionMapAnnotator ann(em);

  auto note = makeNotePT("pt.natural");
  note.set_duration_samples(44100); // 1.0s = 1000ms

  // onNoteStart to establish the match
  ann.onNoteStart(note, ctx);
  // onNoteEnd to compute duration
  ann.onNoteEnd(note, ctx);

  auto *rec = ann.lastAnnotationRecord();
  A_CHECK(rec != nullptr);
  // correction=45/90, lengthFactor=1.0
  // combined = 0.5 * 1.0 = 0.5
  // delta = (0.5 - 1.0) * 1000ms = -500ms
  double expected = -500.0;
  double actual = rec->durationAdjustMs;
  A_CHECK(std::abs(actual - expected) < 0.1);
}

static void testIncomingRecordWithoutExpressionMap() {
  std::cout << "--- testIncomingRecordWithoutExpressionMap ---" << std::endl;

  fiddle::Note note;
  note.set_note_number(64);
  note.set_channel(3);
  note.set_start_velocity(91);
  (*note.mutable_notation_techniques())["articulation"] = "Staccato";
  (*note.mutable_notation_techniques())["secondary"] = "Natural";
  (*note.mutable_notation_is_default())["secondary"] = true;
  (*note.mutable_notation_dimensions())["dorico_length_category"] = 2.0f;
  (*note.mutable_notation_dimensions())["custom_amount"] = 0.75f;

  const auto input = makeIncomingNoteRecord(note, 48000.0, false);
  A_CHECK_EQ(input.noteNumber, 64);
  A_CHECK_EQ(input.inputChannel, 3);
  A_CHECK_EQ(input.velocityBefore, 91);
  A_CHECK_EQ(input.velocityAfter, 91);
  A_CHECK(input.lengthCategory == LengthCategory::Medium);
  A_CHECK(!input.expressionMapAssigned);
  A_CHECK(input.inputTechniques.count("pt.staccato"));
  A_CHECK(!input.inputTechniques.count("pt.natural"));
  A_CHECK_EQ(input.receivedTechniques.at("secondary"),
             std::string("Natural"));
  A_CHECK(input.defaultTechniqueDimensions.count("secondary"));
  A_CHECK(input.receivedDimensions.count("custom_amount"));

  auto mappedInput = input;
  mappedInput.expressionMapAssigned = true;
  AnnotationRecord decision;
  decision.noteNumber = 76;
  decision.velocityBefore = 80;
  decision.velocityAfter = 42;
  decision.baseSwitchName = "Staccato Short";
  const auto merged = mergeNoteInspection(mappedInput, &decision);
  A_CHECK(merged.expressionMapAssigned);
  A_CHECK_EQ(merged.noteNumber, 64);
  A_CHECK_EQ(merged.inputChannel, 3);
  A_CHECK_EQ(merged.velocityBefore, 91);
  A_CHECK_EQ(merged.velocityAfter, 42);
  A_CHECK_EQ(merged.baseSwitchName, std::string("Staccato Short"));
  A_CHECK_EQ(merged.receivedTechniques.at("articulation"),
             std::string("Staccato"));
}


// ---------------------------------------------------------------------------
// Forward declarations for IncomingSwitchTracker tests
// ---------------------------------------------------------------------------
static void testIncomingSwitchTrackerBasic();
static void testIncomingSwitchTrackerConditionDisambiguation();
static void testIncomingSwitchTrackerCCMatching();
static void testIncomingSwitchTrackerAmbiguity();

// ---------------------------------------------------------------------------
// Runner
// ---------------------------------------------------------------------------

static void testBridgeWitnessesThroughIncomingNotes() {
  std::cout << "--- testBridgeWitnessesThroughIncomingNotes ---" << std::endl;
  ExpressionMap bridge;
  const bool loaded = bridge.loadFromDoricolib(juce::File::getCurrentWorkingDirectory().getChildFile("resources/Fiddle_Universal.doricolib"));
  A_CHECK(loaded);
  if (!loaded) return;
  ExpressionMapData map;
  for (const auto &id : {"pt.natural", "pt.staccato", "pt.legato", "pt.muted", "pt.sulTasto"}) {
    TechniqueCombination c; c.name = id; c.techniqueIDs = {id}; map.combinations.push_back(c);
  }
  BridgeReachability::Assignment assignment{"chair", "Chair", "layer", "Layer", map};
  const auto result = BridgeReachability::audit(bridge.getDimensions(), {assignment});
  A_CHECK(bool(result["decoderAvailable"]));
  int replayed = 0;
  for (const auto &row : *result["chairs"][0]["layers"][0]["rows"].getArray()) {
    if (row["status"].toString() != "witness") continue;
    NoteStreamTracker tracker;
    tracker.setExpressionMap(&bridge);
    bool received = false;
    NoteStreamTracker::Callbacks callbacks;
    callbacks.onNoteStarted = [&](const fiddle::Note &note) {
      received = true;
      const auto ids = collectInputTechniqueIds(note);
      std::set<std::string> expected;
      for (const auto &id : *row["witness"]["techniques"].getArray()) expected.insert(id.toString().toStdString());
      A_CHECK(ids == expected);
      const auto length = inputLengthCategory(note, 44100.0);
      A_CHECK(int(length) == int(row["witness"]["length"]));
      const auto match = map.resolveMatch(ids, length);
      A_CHECK(match.base == &map.combinations[int(row["index"])]);
    };
    tracker.setCallbacks(std::move(callbacks));
    for (const auto &cc : *row["witness"]["cc"].getArray()) {
      fiddle::MidiEvent event;
      event.set_channel(1); event.set_host_sample_position(0);
      event.mutable_cc()->set_controller_number(int(cc["cc"]));
      event.mutable_cc()->set_controller_value(int(cc["value"]));
      tracker.processEvent(event);
    }
    fiddle::MidiEvent event;
    event.set_channel(1); event.set_host_sample_position(100);
    event.mutable_note_on()->set_note_number(60); event.mutable_note_on()->set_velocity(80);
    tracker.processEvent(event);
    A_CHECK(received);
    ++replayed;
  }
  A_CHECK(replayed >= 3);
  // The current legacy decoder lowercases this canonical camel-case identity.
  A_CHECK(result["chairs"][0]["layers"][0]["rows"][4]["status"].toString() == "missing");
}

// ---------------------------------------------------------------------------
// Dorico-equivalence tests for a layer's own expression map
// ---------------------------------------------------------------------------

/// A note carrying several pt.xxx IDs, one per dimension.
static fiddle::Note makeNotePTs(const std::vector<std::string> &ids,
                                uint64_t id = 0, int noteNum = 60,
                                int velocity = 100, uint32_t channel = 1) {
  fiddle::Note note;
  note.set_id(id);
  note.set_channel(channel);
  note.set_note_number(noteNum);
  note.set_start_velocity(velocity);
  for (size_t i = 0; i < ids.size(); ++i)
    (*note.mutable_notation_techniques())["dim" + std::to_string(i)] = ids[i];
  return note;
}

static bool isInstruction(const fiddle::MidiInstruction &inst,
                          fiddle::MidiInstruction::Type type, int p1, int p2,
                          int channel = 0) {
  return inst.type() == type && inst.param1() == p1 && inst.param2() == p2 &&
         inst.channel() == channel;
}

static void testSwitchOffActionsOnChange() {
  std::cout << "--- testSwitchOffActionsOnChange ---" << std::endl;
  AnnotatorContext ctx;
  using MI = fiddle::MidiInstruction;

  auto em = std::make_shared<ExpressionMapData>();
  em->combinations.push_back(TechniqueCombination{
      .name = "Natural", .techniqueIDs = {"pt.natural"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}},
      .switchOffActions = {{SwitchActionType::CC, 64, 0}}});
  em->combinations.push_back(TechniqueCombination{
      .name = "Staccato", .techniqueIDs = {"pt.staccato"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}}});
  em->combinations.push_back(TechniqueCombination{
      .name = "Mute", .techniqueIDs = {"pt.muted"}, .isAddOn = true,
      .switchOnActions = {{SwitchActionType::CC, 20, 127}},
      .switchOffActions = {{SwitchActionType::CC, 20, 0}}});
  ExpressionMapAnnotator ann(em);

  // Repeated notes of a switch with Off actions keep that switch.
  auto first = makeNotePTs({"pt.natural"}, 1);
  ann.onNoteStart(first, ctx);
  A_CHECK_EQ(first.pre_note_size(), 1);
  ann.onNoteEnd(first, ctx);
  A_CHECK_EQ(first.post_note_size(), 0);
  auto second = makeNotePTs({"pt.natural"}, 2);
  ann.onNoteStart(second, ctx);
  A_CHECK_EQ(second.pre_note_size(), 0);

  // Changing away sends the old Off before the new On.
  auto third = makeNotePTs({"pt.staccato"}, 3, 62);
  ann.onNoteStart(third, ctx);
  A_CHECK_EQ(third.pre_note_size(), 2);
  if (third.pre_note_size() == 2) {
    A_CHECK(isInstruction(third.pre_note(0), MI::CC, 64, 0));
    A_CHECK(isInstruction(third.pre_note(1), MI::KEY_SWITCH, 36, 100));
  }

  // The overlapped predecessor ending does not clear its successor.
  ann.onNoteEnd(second, ctx);
  A_CHECK_EQ(second.post_note_size(), 0);
  auto fourth = makeNotePTs({"pt.staccato"}, 4, 64);
  ann.onNoteStart(fourth, ctx);
  A_CHECK_EQ(fourth.pre_note_size(), 0);

  // An add-on that drops out sends its own Off; a base that stays does not.
  auto muted = makeNotePTs({"pt.natural", "pt.muted"}, 5);
  ann.onNoteStart(muted, ctx);
  auto open = makeNotePTs({"pt.natural"}, 6);
  ann.onNoteStart(open, ctx);
  A_CHECK_EQ(open.pre_note_size(), 2);
  if (open.pre_note_size() == 2) {
    A_CHECK(isInstruction(open.pre_note(0), MI::CC, 20, 0));
    A_CHECK(isInstruction(open.pre_note(1), MI::KEY_SWITCH, 24, 100));
  }
}

static void testChannelSwitchRouting() {
  std::cout << "--- testChannelSwitchRouting ---" << std::endl;
  AnnotatorContext ctx;
  using MI = fiddle::MidiInstruction;

  auto em = std::make_shared<ExpressionMapData>();
  em->combinations.push_back(TechniqueCombination{
      .name = "Tremolo", .techniqueIDs = {"pt.tremolo"},
      .switchOnActions = {{SwitchActionType::ChannelSwitch, 3, 0},
                          {SwitchActionType::KeySwitch, 24, 100}},
      .switchOffActions = {{SwitchActionType::CC, 64, 0}},
      .transpose = -12});
  em->combinations.push_back(TechniqueCombination{
      .name = "Natural", .techniqueIDs = {"pt.natural"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 36, 100}}});
  ExpressionMapAnnotator ann(em);

  auto tremolo = makeNotePTs({"pt.tremolo"}, 1, 72);
  ann.onNoteStart(tremolo, ctx);
  A_CHECK_EQ(tremolo.channel(), 3u);
  A_CHECK_EQ(tremolo.note_number(), 60u);
  A_CHECK_EQ(tremolo.pre_note_size(), 1);
  if (tremolo.pre_note_size() == 1)
    A_CHECK(isInstruction(tremolo.pre_note(0), MI::KEY_SWITCH, 24, 100, 3));

  // Controllers follow the switched channel; Dorico's dimensions stay hidden.
  fiddle::MidiEvent cc7;
  cc7.mutable_cc()->set_controller_number(7);
  cc7.mutable_cc()->set_controller_value(90);
  A_CHECK(!ann.onCC(cc7, ctx));
  std::vector<fiddle::MidiInstruction> out;
  ann.translateCC(cc7, ctx, out);
  A_CHECK_EQ(out.size(), 1u);
  if (out.size() == 1)
    A_CHECK(isInstruction(out[0], MI::CC, 7, 90, 3));
  fiddle::MidiEvent dimension;
  dimension.mutable_cc()->set_controller_number(105);
  dimension.mutable_cc()->set_controller_value(2);
  out.clear();
  A_CHECK(!ann.onCC(dimension, ctx));
  ann.translateCC(dimension, ctx, out);
  A_CHECK(out.empty());

  // The next switch's Off goes where the old switch played.
  auto natural = makeNotePTs({"pt.natural"}, 2, 62);
  ann.onNoteStart(natural, ctx);
  A_CHECK_EQ(natural.channel(), 1u);
  A_CHECK_EQ(natural.pre_note_size(), 2);
  if (natural.pre_note_size() == 2) {
    A_CHECK(isInstruction(natural.pre_note(0), MI::CC, 64, 0, 3));
    A_CHECK(isInstruction(natural.pre_note(1), MI::KEY_SWITCH, 36, 100));
  }
  A_CHECK(ann.onCC(cc7, ctx));

  // The overlapped tremolo note still ends on its own channel and pitch.
  auto tremoloOff = makeNotePTs({"pt.tremolo"}, 1, 72);
  ann.onNoteEnd(tremoloOff, ctx);
  A_CHECK_EQ(tremoloOff.channel(), 3u);
  A_CHECK_EQ(tremoloOff.note_number(), 60u);
  auto naturalOff = makeNotePTs({"pt.natural"}, 2, 62);
  ann.onNoteEnd(naturalOff, ctx);
  A_CHECK_EQ(naturalOff.channel(), 1u);
}

static void testSwitchLeadTime() {
  std::cout << "--- testSwitchLeadTime ---" << std::endl;
  AnnotatorContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.tempoBpm = 120.0;

  auto em = std::make_shared<ExpressionMapData>();
  em->combinations.push_back(TechniqueCombination{
      .name = "Natural", .techniqueIDs = {"pt.natural"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}},
      .millisecondsBefore = 30});
  em->combinations.push_back(TechniqueCombination{
      .name = "Legato", .techniqueIDs = {"pt.legato"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 25, 100}},
      .ticksBefore = 240, .millisecondsBefore = 10,
      .applyMillisecondsBeforeToEndOffsets = true,
      .applyMillisecondsBeforeToControllers = true});
  em->combinations.push_back(TechniqueCombination{
      .name = "Staccato", .techniqueIDs = {"pt.staccato"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 26, 100}}});
  em->timingOptions[DurationCategory::Legato] = 105;
  ExpressionMapAnnotator ann(em);

  // The note and its keyswitch move together; the keyswitch also leads the
  // note by the project's "switches before notes" (Dorico's default 10 ticks
  // = 10.4 ms at 120 BPM). The end and controllers stay unless the map asks.
  auto natural = makeNotePTs({"pt.natural"}, 1);
  natural.set_duration_samples(44100);
  ann.onNoteStart(natural, ctx);
  A_CHECK_EQ(ann.noteStartOffsetMs(), -30.0);
  A_CHECK_EQ(ann.controllerOffsetMs(), 0.0);
  A_CHECK_EQ(natural.pre_note_size(), 1);
  if (natural.pre_note_size() == 1)
    A_CHECK_EQ(natural.pre_note(0).delay_ms(), -10);
  ann.onNoteEnd(natural, ctx);
  A_CHECK_EQ(ann.durationAdjustMs(), 0.0);

  // The note leads by 10 ms (with its end and controllers); its switch
  // actions lead by its own 240 ticks = 250 ms at 120 BPM (480 per quarter),
  // which replaces the project's lead.
  auto legato = makeNotePTs({"pt.legato"}, 2);
  legato.set_duration_samples(44100);
  ann.onNoteStart(legato, ctx);
  A_CHECK_EQ(ann.noteStartOffsetMs(), -10.0);
  A_CHECK_EQ(ann.controllerOffsetMs(), -10.0);
  A_CHECK_EQ(legato.pre_note_size(), 1);
  if (legato.pre_note_size() == 1)
    A_CHECK_EQ(legato.pre_note(0).delay_ms(), -250);
  ann.onNoteEnd(legato, ctx);
  A_CHECK(std::abs(ann.durationAdjustMs() - (-10.0)) < 1e-9);

  auto staccato = makeNotePTs({"pt.staccato"}, 3);
  ann.onNoteStart(staccato, ctx);
  A_CHECK_EQ(ann.noteStartOffsetMs(), 0.0);
  A_CHECK_EQ(ann.controllerOffsetMs(), 0.0);
}

static void testSwitchLeadSources() {
  std::cout << "--- testSwitchLeadSources ---" << std::endl;
  AnnotatorContext ctx;
  ctx.tempoBpm = 120.0;
  const auto delayFor = [&](const std::shared_ptr<ExpressionMapData> &map, int projectTicks) {
    ctx.doricoSwitchLeadTicks = projectTicks;
    ExpressionMapAnnotator ann(map);
    auto note = makeNotePTs({"pt.natural"});
    ann.onNoteStart(note, ctx);
    return note.pre_note_size() == 1 ? note.pre_note(0).delay_ms() : 999;
  };
  auto map = makeSimpleEM("Natural", {"pt.natural"}, {{SwitchActionType::KeySwitch, 24, 100}});
  A_CHECK_EQ(delayFor(map, 0), 0);    // project: no lead
  A_CHECK_EQ(delayFor(map, 20), -21); // 20 ticks at 120 BPM = 20.8 ms
  A_CHECK_EQ(delayFor(map, 96), -100);

  // A map's own playback-option override replaces the project's value.
  ExpressionMapData parsed;
  A_CHECK(parseExpressionMapXml(R"xml(<?xml version="1.0" encoding="utf-8"?>
<kScoreLibrary><expressionMapDefinitions><entities array="true">
<ExpressionMapDefinition><name>Lead</name><entityID>map.lead</entityID>
  <playingTechniqueCombinations array="true"><playingTechniqueCombination>
    <name>Natural</name><techniqueIDs>pt.natural</techniqueIDs>
    <switchOnActions array="true"><switchOnAction><type>kKeySwitch</type><param1>24</param1><param2>100</param2></switchOnAction></switchOnActions>
  </playingTechniqueCombination></playingTechniqueCombinations>
  <playbackOptionsOverrides array="true"><optionsOverride>
    <option>timingOptions.numTicksBeforeForSwitches</option><value>int: 48</value>
  </optionsOverride></playbackOptionsOverrides>
</ExpressionMapDefinition></entities></expressionMapDefinitions></kScoreLibrary>)xml",
                                parsed));
  A_CHECK_EQ(parsed.timingOptions.switchLeadTicks.value_or(-1), 48);
  A_CHECK_EQ(delayFor(std::make_shared<ExpressionMapData>(parsed), 0), -50);

  // A switch's own ticksBefore replaces both.
  map->combinations[0].ticksBefore = 192;
  A_CHECK_EQ(delayFor(map, 20), -200);
}

static void testVelocityAndPitchRanges() {
  std::cout << "--- testVelocityAndPitchRanges ---" << std::endl;
  AnnotatorContext ctx;

  auto em = std::make_shared<ExpressionMapData>();
  em->combinations.push_back(TechniqueCombination{
      .name = "Natural", .techniqueIDs = {"pt.natural"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 24, 100}}});
  em->combinations.push_back(TechniqueCombination{
      .name = "Sfz", .techniqueIDs = {"pt.sfz"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 87, 127}},
      .velocityMin = 56});
  em->combinations.push_back(TechniqueCombination{
      .name = "Pizz low", .techniqueIDs = {"pt.pizzicato"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 30, 100}},
      .pitchMax = 59});
  em->combinations.push_back(TechniqueCombination{
      .name = "Pizz high", .techniqueIDs = {"pt.pizzicato"},
      .switchOnActions = {{SwitchActionType::KeySwitch, 31, 100}},
      .pitchMin = 60});

  A_CHECK(em->resolveMatch({"pt.sfz"}, LengthCategory::VeryLong, nullptr, 100, 60).base ==
          &em->combinations[1]);
  A_CHECK(em->resolveMatch({"pt.sfz"}, LengthCategory::VeryLong, nullptr, 40, 60).base ==
          &em->combinations[0]);
  A_CHECK(em->resolveMatch({"pt.pizzicato"}, LengthCategory::VeryLong, nullptr, 100, 50).base ==
          &em->combinations[2]);
  A_CHECK(em->resolveMatch({"pt.pizzicato"}, LengthCategory::VeryLong, nullptr, 100, 70).base ==
          &em->combinations[3]);

  // The annotator's cache distinguishes notes by the ranges' inputs.
  ExpressionMapAnnotator ann(em);
  const auto keyswitchFor = [&](const char *pt, int pitch, int velocity) {
    auto note = makeNotePTs({pt}, 0, pitch, velocity);
    ann.onNoteStart(note, ctx);
    return note.pre_note_size() ? note.pre_note(0).param1() : -1;
  };
  A_CHECK_EQ(keyswitchFor("pt.pizzicato", 50, 100), 30);
  A_CHECK_EQ(keyswitchFor("pt.pizzicato", 70, 100), 31);
  A_CHECK_EQ(keyswitchFor("pt.sfz", 70, 100), 87);
  A_CHECK_EQ(keyswitchFor("pt.sfz", 70, 30), 24);
}

static void testInitActionsAtTransportStart() {
  std::cout << "--- testInitActionsAtTransportStart ---" << std::endl;
  AnnotatorContext ctx;
  using MI = fiddle::MidiInstruction;

  ExpressionMapData parsed;
  A_CHECK(parseExpressionMapXml(R"xml(<?xml version="1.0" encoding="utf-8"?>
<kScoreLibrary><expressionMapDefinitions><entities array="true">
<ExpressionMapDefinition><name>Init</name><entityID>map.init</entityID>
  <initSwitchData><enabled>true</enabled><initActions array="true">
    <switchOnAction><type>kControlChange</type><param1>7</param1><param2>100</param2></switchOnAction>
    <switchOnAction><type>kKeySwitch</type><param1>12</param1><param2>90</param2></switchOnAction>
    <switchOnAction><type>kChannelSwitch</type><param1>3</param1><param2>0</param2></switchOnAction>
  </initActions></initSwitchData>
  <playingTechniqueCombinations array="true"><playingTechniqueCombination>
    <name>Natural</name><techniqueIDs>pt.natural</techniqueIDs>
    <millisecondsBefore>15</millisecondsBefore>
    <applyMillisecondsBeforeToEndOffsets>true</applyMillisecondsBeforeToEndOffsets>
    <applyMillisecondsBeforeToControllers>false</applyMillisecondsBeforeToControllers>
    <switchOnActions array="true"><switchOnAction><type>kKeySwitch</type><param1>24</param1><param2>100</param2></switchOnAction></switchOnActions>
    <switchOffActions array="true"/>
  </playingTechniqueCombination></playingTechniqueCombinations>
</ExpressionMapDefinition></entities></expressionMapDefinitions></kScoreLibrary>)xml",
                                parsed));
  A_CHECK_EQ(parsed.initActions.size(), 3u);
  A_CHECK_EQ(parsed.combinations[0].millisecondsBefore, 15);
  A_CHECK(parsed.combinations[0].applyMillisecondsBeforeToEndOffsets);
  A_CHECK(!parsed.combinations[0].applyMillisecondsBeforeToControllers);

  ExpressionMapAnnotator ann(std::make_shared<ExpressionMapData>(parsed));
  std::vector<fiddle::MidiInstruction> init;
  ann.onTransportStart(ctx, init);
  A_CHECK_EQ(init.size(), 2u);
  if (init.size() == 2) {
    A_CHECK(isInstruction(init[0], MI::CC, 7, 100));
    A_CHECK(isInstruction(init[1], MI::KEY_SWITCH, 12, 90));
  }

  // A new playback re-sends the first note's switch.
  auto first = makeNotePTs({"pt.natural"}, 1);
  ann.onNoteStart(first, ctx);
  auto repeat = makeNotePTs({"pt.natural"}, 2);
  ann.onNoteStart(repeat, ctx);
  A_CHECK_EQ(repeat.pre_note_size(), 0);
  init.clear();
  ann.onTransportStart(ctx, init);
  auto restarted = makeNotePTs({"pt.natural"}, 3);
  ann.onNoteStart(restarted, ctx);
  A_CHECK_EQ(restarted.pre_note_size(), 1);

  // A disabled block sends nothing.
  ExpressionMapData disabled;
  A_CHECK(parseExpressionMapXml(R"xml(<?xml version="1.0" encoding="utf-8"?>
<kScoreLibrary><expressionMapDefinitions><entities array="true">
<ExpressionMapDefinition><name>Off</name><entityID>map.off</entityID>
  <initSwitchData><enabled>false</enabled><initActions array="true">
    <switchOnAction><type>kControlChange</type><param1>7</param1><param2>100</param2></switchOnAction>
  </initActions></initSwitchData>
  <playingTechniqueCombinations array="true"><playingTechniqueCombination>
    <name>Natural</name><techniqueIDs>pt.natural</techniqueIDs>
  </playingTechniqueCombination></playingTechniqueCombinations>
</ExpressionMapDefinition></entities></expressionMapDefinitions></kScoreLibrary>)xml",
                                disabled));
  A_CHECK(disabled.initActions.empty());
}

static void testCanonicalTechniquePriority() {
  std::cout << "--- testCanonicalTechniquePriority ---" << std::endl;
  for (const auto *pt : {"pt.naturalHarmonic1", "pt.naturalHarmonic2",
                         "pt.artificialHarmonic", "pt.pizzicato",
                         "pt.bartoksnapPizzicato", "pt.naturalharmonic"})
    A_CHECK_EQ(ExpressionMapData::techniquePriority(pt), 100);
  for (const auto *pt : {"pt.muted", "pt.cupMute", "pt.straightMute",
                         "pt.harmonMuteStemIn", "pt.harmonMuteStemOut",
                         "pt.harmonMuteStemExtended", "pt.plungerMute",
                         "pt.bucketMute", "pt.cupmute"})
    A_CHECK_EQ(ExpressionMapData::techniquePriority(pt), 50);
  A_CHECK_EQ(ExpressionMapData::techniquePriority("pt.flutterTongue"), 10);

  // As in Daniel's Mute + Flutter-tongue example: with no combined switch
  // the mute wins over an ordinary technique (here one that sorts first).
  ExpressionMapData map;
  map.combinations.push_back(TechniqueCombination{
      .name = "Accent", .techniqueIDs = {"pt.accent"}});
  map.combinations.push_back(TechniqueCombination{
      .name = "Cup", .techniqueIDs = {"pt.cupMute"}});
  map.combinations.push_back(TechniqueCombination{
      .name = "Harmonic", .techniqueIDs = {"pt.naturalHarmonic1"}});
  A_CHECK(map.resolveMatch({"pt.accent", "pt.cupMute"}).base ==
          &map.combinations[1]);
  A_CHECK(map.resolveMatch({"pt.cupMute", "pt.naturalHarmonic1"}).base ==
          &map.combinations[2]);
}

static void testWrittenDurations() {
  std::cout << "--- testWrittenDurations ---" << std::endl;
  AnnotatorContext ctx;
  ctx.sampleRate = 44100.0;
  ctx.doricoDurationPercents = {80, 40, 20, 110, 100, 70}; // the project's options

  auto em = std::make_shared<ExpressionMapData>();
  em->combinations.push_back(TechniqueCombination{.name = "Natural", .techniqueIDs = {"pt.natural"}});
  em->combinations.push_back(TechniqueCombination{.name = "Staccato", .techniqueIDs = {"pt.staccato"}});
  em->timingOptions[DurationCategory::Staccato] = 30;

  // A 1000 ms note, sent at written length when `written`.
  const auto adjust = [&](Annotator &ann, const char *pt, bool written) {
    auto note = makeNotePTs({pt});
    note.set_duration_samples(44100);
    if (written)
      (*note.mutable_notation_dimensions())[kWrittenDurationDimension] = 1.0f;
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    return ann.durationAdjustMs();
  };
  const auto near = [](double a, double b) { return std::abs(a - b) < 0.1; };

  // From written length: the map's override, else the project's option.
  ExpressionMapAnnotator ann(em);
  A_CHECK(near(adjust(ann, "pt.natural", true), -200.0));
  A_CHECK(near(adjust(ann, "pt.staccato", true), -700.0));
  A_CHECK(near(adjust(ann, "pt.legato", true), 100.0));
  // Dorico already applied the project's option: only overrides change it.
  A_CHECK(near(adjust(ann, "pt.natural", false), 0.0));
  A_CHECK(near(adjust(ann, "pt.staccato", false), -250.0));

  // Layers without an expression map still get the project's options.
  ExpressionMapAnnotator noMap(nullptr);
  A_CHECK(near(adjust(noMap, "pt.marcato", true), -300.0));
  A_CHECK(near(adjust(noMap, "pt.marcato", false), 0.0));
  PassthroughAnnotator passthrough;
  A_CHECK(near(adjust(passthrough, "pt.staccatissimo", true), -800.0));
  A_CHECK(near(adjust(passthrough, "pt.tenuto", true), 0.0));
  A_CHECK(near(adjust(passthrough, "pt.staccatissimo", false), 0.0));
}

void runAnnotatorTests() {
  testBridgeWitnessesThroughIncomingNotes();
  testReverseHumanize();
  testActionToInstruction();
  testRedundancyTracking();
  testModifiers();
  testEmitDynamicsVelocityToVelocity();
  testEmitDynamicsCCToVelocity();
  testEmitDynamicsVelocityToCC();
  testEmitDynamicsCCToCC();
  testEmitDynamicsCCToCCDifferentTarget();
  testLiveCC1Dynamics();
  testResolveMatchTier2();
  testResolveMatchNaturalFallback();
  testResolveMatchImplicitNatural();
  testNaturalFallbackAddOnsAndCompoundLengths();
  testCompoundConditionParsing();
  testNullExpressionMap();
  testEmptyTechniques();
  testNoMatch();
  testMatchResultEquality();
  testCacheBehavior();
  testMultipleSwitchOnActions();
  testOnNoteEnd();
  testConditionParsing();
  testConditionEvaluation();
  testAnnotatorConditionFiltering();
  testDurationAdjust();
  testSwitchOffActionsOnChange();
  testChannelSwitchRouting();
  testSwitchLeadTime();
  testSwitchLeadSources();
  testVelocityAndPitchRanges();
  testInitActionsAtTransportStart();
  testCanonicalTechniquePriority();
  testWrittenDurations();

  // AnnotationRecord tests
  testAnnotationRecordBasicMatch();
  testAnnotationRecordBaseAndAddOn();
  testAnnotationRecordPartialMatch();
  testAnnotationRecordRedundancy();
  testAnnotationRecordConditionFallback();
  testAnnotationRecordModifiers();
  testAnnotationRecordDuration();
  testIncomingRecordWithoutExpressionMap();

  // IncomingSwitchTracker tests
  testIncomingSwitchTrackerBasic();
  testIncomingSwitchTrackerConditionDisambiguation();
  testIncomingSwitchTrackerCCMatching();
  testIncomingSwitchTrackerAmbiguity();
}

// ===========================================================================
// IncomingSwitchTracker tests
// ===========================================================================

#include "../IncomingSwitchTracker.h"

/// Build an EM with two keyswitched base switches.
static std::shared_ptr<ExpressionMapData> makeKeyswitchedEM() {
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "Test KS EM";

  // Switch 1: "Legato" — keyswitches 24 + 41
  TechniqueCombination tc1;
  tc1.name = "Legato";
  tc1.techniqueIDs = {"pt.legato"};
  tc1.switchOnActions = {
      {SwitchActionType::KeySwitch, 24, 120},
      {SwitchActionType::KeySwitch, 41, 120},
  };
  em->combinations.push_back(tc1);

  // Switch 2: "Marcato" — keyswitches 25 + 36
  TechniqueCombination tc2;
  tc2.name = "Marcato";
  tc2.techniqueIDs = {"pt.marcato"};
  tc2.switchOnActions = {
      {SwitchActionType::KeySwitch, 25, 120},
      {SwitchActionType::KeySwitch, 36, 120},
  };
  em->combinations.push_back(tc2);

  return em;
}

/// Build an EM with condition-differentiated switches sharing the same
/// keyswitches.
static std::shared_ptr<ExpressionMapData> makeConditionEM() {
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "Condition EM";

  // "Natural short" — keyswitch 24, condition: NoteLength <= kShort
  TechniqueCombination tc1;
  tc1.name = "Natural short";
  tc1.techniqueIDs = {"pt.natural"};
  tc1.switchOnActions = {{SwitchActionType::KeySwitch, 24, 120}};
  tc1.conditionString = "NoteLength <= kShort";
  tc1.condition = parseConditionString(tc1.conditionString);
  em->combinations.push_back(tc1);

  // "Natural long" — same keyswitch 24, condition: NoteLength > kShort
  TechniqueCombination tc2;
  tc2.name = "Natural long";
  tc2.techniqueIDs = {"pt.natural"};
  tc2.switchOnActions = {{SwitchActionType::KeySwitch, 24, 120}};
  tc2.conditionString = "NoteLength > kShort";
  tc2.condition = parseConditionString(tc2.conditionString);
  em->combinations.push_back(tc2);

  return em;
}

/// Build an EM with CC-based switches.
static std::shared_ptr<ExpressionMapData> makeCCEM() {
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "CC EM";

  TechniqueCombination tc1;
  tc1.name = "Natural";
  tc1.techniqueIDs = {"pt.natural"};
  tc1.switchOnActions = {{SwitchActionType::CC, 102, 0}};
  em->combinations.push_back(tc1);

  TechniqueCombination tc2;
  tc2.name = "Pizzicato";
  tc2.techniqueIDs = {"pt.pizzicato"};
  tc2.switchOnActions = {{SwitchActionType::CC, 102, 1}};
  em->combinations.push_back(tc2);

  return em;
}

static void testIncomingSwitchTrackerBasic() {
  std::cout << "--- testIncomingSwitchTrackerBasic ---" << std::endl;

  auto em = makeKeyswitchedEM();
  IncomingSwitchTracker tracker(em);

  // Notes 24, 41, 25, 36 should be detected as keyswitches
  A_CHECK(tracker.isKeyswitch(24));
  A_CHECK(tracker.isKeyswitch(41));
  A_CHECK(tracker.isKeyswitch(25));
  A_CHECK(tracker.isKeyswitch(36));
  A_CHECK(!tracker.isKeyswitch(60)); // real note

  // Simulate: keyswitches 24+41, then NoteOn(60) at t=0, NoteOff(60) at t=500
  tracker.onMidi(CapturedMidiEvent::NoteOn, 24, 120, 0.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 41, 120, 0.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 60, 90, 100.0); // real note

  auto names = tracker.resolveNoteOff(60, 600.0);
  A_CHECK_EQ((int)names.size(), 1);
  if (!names.empty())
    A_CHECK_EQ(names[0], std::string("Legato"));

  // Now send keyswitches 25+36 and another note
  tracker.onMidi(CapturedMidiEvent::NoteOn, 25, 120, 700.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 36, 120, 700.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 67, 80, 800.0);

  auto names2 = tracker.resolveNoteOff(67, 1300.0);
  A_CHECK_EQ((int)names2.size(), 1);
  if (!names2.empty())
    A_CHECK_EQ(names2[0], std::string("Marcato"));
}

static void testIncomingSwitchTrackerConditionDisambiguation() {
  std::cout << "--- testIncomingSwitchTrackerConditionDisambiguation ---"
            << std::endl;

  auto em = makeConditionEM();
  IncomingSwitchTracker tracker(em);

  // Send keyswitch 24
  tracker.onMidi(CapturedMidiEvent::NoteOn, 24, 120, 0.0);

  // Short note: NoteOn at t=100, NoteOff at t=200 (100ms = 0.1s < kShort=0.375s)
  tracker.onMidi(CapturedMidiEvent::NoteOn, 60, 80, 100.0);
  auto nameShort = tracker.resolveNoteOff(60, 200.0);
  A_CHECK_EQ((int)nameShort.size(), 1);
  if (!nameShort.empty())
    A_CHECK_EQ(nameShort[0], std::string("Natural short"));

  // Long note: NoteOn at t=300, NoteOff at t=1300 (1000ms = 1.0s > kShort)
  tracker.onMidi(CapturedMidiEvent::NoteOn, 60, 80, 300.0);
  auto nameLong = tracker.resolveNoteOff(60, 1300.0);
  A_CHECK_EQ((int)nameLong.size(), 1);
  if (!nameLong.empty())
    A_CHECK_EQ(nameLong[0], std::string("Natural long"));
}

static void testIncomingSwitchTrackerCCMatching() {
  std::cout << "--- testIncomingSwitchTrackerCCMatching ---" << std::endl;

  auto em = makeCCEM();
  IncomingSwitchTracker tracker(em);

  // Send CC102=0 → Natural
  tracker.onMidi(CapturedMidiEvent::CC, 102, 0, 0.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 60, 80, 100.0);
  auto names1 = tracker.resolveNoteOff(60, 600.0);
  A_CHECK_EQ((int)names1.size(), 1);
  if (!names1.empty())
    A_CHECK_EQ(names1[0], std::string("Natural"));

  // Send CC102=1 → Pizzicato
  tracker.onMidi(CapturedMidiEvent::CC, 102, 1, 700.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 60, 80, 800.0);
  auto names2 = tracker.resolveNoteOff(60, 1300.0);
  A_CHECK_EQ((int)names2.size(), 1);
  if (!names2.empty())
    A_CHECK_EQ(names2[0], std::string("Pizzicato"));
}

/// Two switches share the same keyswitch — both should be returned.
static std::shared_ptr<ExpressionMapData> makeAmbiguousEM() {
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "Ambiguous EM";

  TechniqueCombination tc1;
  tc1.name = "Natural";
  tc1.techniqueIDs = {"pt.natural"};
  tc1.switchOnActions = {{SwitchActionType::KeySwitch, 24, 120}};
  em->combinations.push_back(tc1);

  TechniqueCombination tc2;
  tc2.name = "Sustain";
  tc2.techniqueIDs = {"pt.sustain"};
  tc2.switchOnActions = {{SwitchActionType::KeySwitch, 24, 120}};
  em->combinations.push_back(tc2);

  return em;
}

static void testIncomingSwitchTrackerAmbiguity() {
  std::cout << "--- testIncomingSwitchTrackerAmbiguity ---" << std::endl;

  auto em = makeAmbiguousEM();
  IncomingSwitchTracker tracker(em);

  tracker.onMidi(CapturedMidiEvent::NoteOn, 24, 120, 0.0);
  tracker.onMidi(CapturedMidiEvent::NoteOn, 60, 80, 100.0);
  auto names = tracker.resolveNoteOff(60, 600.0);
  // Both "Natural" and "Sustain" share keyswitch 24 — both should match
  A_CHECK_EQ((int)names.size(), 2);

  // Check both names are present (order may vary by EM order)
  bool hasNatural = false, hasSustain = false;
  for (const auto &n : names) {
    if (n == "Natural")
      hasNatural = true;
    if (n == "Sustain")
      hasSustain = true;
  }
  A_CHECK(hasNatural);
  A_CHECK(hasSustain);
}
