#include "../ExpressionMapAnnotator.h"
#include "../ExpressionMapData.h"
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
    ann.onNoteStart(note, ctx);
    A_CHECK_EQ(note.pre_note_size(), 1);
    A_CHECK_EQ(note.pre_note(0).type(), fiddle::MidiInstruction::CC);
    A_CHECK_EQ(note.pre_note(0).channel(), 3);
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

  // Case 1: no switch-off actions → post_note is empty
  {
    auto em = makeSimpleEM("Natural", {"pt.natural"});
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    ann.onNoteEnd(note, ctx);
    A_CHECK_EQ(note.post_note_size(), 0);
  }

  // Case 2: CC switch-off action → emitted in post_note
  {
    auto em = std::make_shared<ExpressionMapData>();
    em->name = "OffTest";
    TechniqueCombination tc;
    tc.name = "Natural";
    tc.techniqueIDs = {"pt.natural"};
    // switch-off: CC#1 → 0
    SwitchAction offAction;
    offAction.type = SwitchActionType::CC;
    offAction.param1 = 1;
    offAction.param2 = 0;
    tc.switchOffActions.push_back(offAction);
    em->combinations.push_back(tc);

    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    ann.onNoteEnd(note, ctx);
    A_CHECK_EQ(note.post_note_size(), 1);
    A_CHECK_EQ((int)note.post_note(0).type(),
               (int)fiddle::MidiInstruction::CC);
    A_CHECK_EQ(note.post_note(0).param1(), 1);
    A_CHECK_EQ(note.post_note(0).param2(), 0);
  }

  // Case 3: keyswitch switch-off action → emitted in post_note
  {
    auto em = std::make_shared<ExpressionMapData>();
    em->name = "KSOffTest";
    TechniqueCombination tc;
    tc.name = "Legato";
    tc.techniqueIDs = {"pt.legato"};
    SwitchAction offKS;
    offKS.type = SwitchActionType::KeySwitch;
    offKS.param1 = 24;
    offKS.param2 = 100;
    tc.switchOffActions.push_back(offKS);
    em->combinations.push_back(tc);

    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.legato");
    ann.onNoteEnd(note, ctx);
    A_CHECK_EQ(note.post_note_size(), 1);
    A_CHECK_EQ((int)note.post_note(0).type(),
               (int)fiddle::MidiInstruction::KEY_SWITCH);
    A_CHECK_EQ(note.post_note(0).param1(), 24);
  }
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
  // noteDurationPercent=85, staccatoDurationPercent=80, legatoDurationPercent=101
  auto em = std::make_shared<ExpressionMapData>();
  em->name = "DurationTest";
  em->timingOptions.noteDurationPercent = 85;
  em->timingOptions.staccatoDurationPercent = 80;
  em->timingOptions.staccatissimoDurationPercent = 80;
  em->timingOptions.legatoDurationPercent = 101;

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

  // Default (Natural): 85% × 1.0 = 0.85 → delta = -15%
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.natural");
    note.set_duration_samples(44100); // 1000ms
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    double delta = ann.durationAdjustMs();
    // Expected: (0.85 - 1.0) * 1000ms = -150ms
    A_CHECK(std::abs(delta - (-150.0)) < 0.1);
  }

  // Staccato: 80% × 0.8 = 0.64 → delta = -36%
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.staccato");
    note.set_duration_samples(44100); // 1000ms
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    double delta = ann.durationAdjustMs();
    // Expected: (0.80 * 0.80 - 1.0) * 1000ms = (0.64 - 1.0) * 1000 = -360ms
    A_CHECK(std::abs(delta - (-360.0)) < 0.1);
  }

  // Legato: 101% × 1.0 = 1.01 → delta = +1%
  {
    ExpressionMapAnnotator ann(em);
    auto note = makeNotePT("pt.legato");
    note.set_duration_samples(44100); // 1000ms
    ann.onNoteStart(note, ctx);
    ann.onNoteEnd(note, ctx);
    double delta = ann.durationAdjustMs();
    // Expected: (1.01 - 1.0) * 1000ms = +10ms
    A_CHECK(std::abs(delta - 10.0) < 0.1);
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
  em->timingOptions.noteDurationPercent = 50; // cut duration in half

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
  // categoryPercent=50, lengthFactor=1.0
  // combined = 0.5 * 1.0 = 0.5
  // delta = (0.5 - 1.0) * 1000ms = -500ms
  double expected = -500.0;
  double actual = rec->durationAdjustMs;
  A_CHECK(std::abs(actual - expected) < 0.1);
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

void runAnnotatorTests() {
  testReverseHumanize();
  testActionToInstruction();
  testRedundancyTracking();
  testModifiers();
  testEmitDynamicsVelocityToVelocity();
  testEmitDynamicsCCToVelocity();
  testEmitDynamicsVelocityToCC();
  testEmitDynamicsCCToCC();
  testEmitDynamicsCCToCCDifferentTarget();
  testResolveMatchTier2();
  testResolveMatchNaturalFallback();
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

  // AnnotationRecord tests
  testAnnotationRecordBasicMatch();
  testAnnotationRecordBaseAndAddOn();
  testAnnotationRecordPartialMatch();
  testAnnotationRecordRedundancy();
  testAnnotationRecordConditionFallback();
  testAnnotationRecordModifiers();
  testAnnotationRecordDuration();

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
