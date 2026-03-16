#include "../GainMath.h"

#include <cmath>
#include <iostream>
#include <string>

// ---------------------------------------------------------------------------
// Minimal test harness (same pattern as VersionStoreTest)
// ---------------------------------------------------------------------------

static int gTestsPassed = 0;
static int gTestsFailed = 0;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "  FAIL: " << #cond << " (" << __FILE__ << ":" << __LINE__  \
                << ")" << std::endl;                                           \
      gTestsFailed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define CHECK_NEAR(a, b, eps)                                                  \
  do {                                                                         \
    float _a = (a), _b = (b);                                                  \
    if (std::fabs(_a - _b) > (eps)) {                                          \
      std::cerr << "  FAIL: " << #a << " ≈ " << #b << " (eps=" << (eps)       \
                << ")" << std::endl;                                           \
      std::cerr << "    got: " << _a << " vs " << _b << std::endl;            \
      gTestsFailed++;                                                          \
      return;                                                                  \
    }                                                                          \
  } while (0)

#define RUN_TEST(fn)                                                           \
  do {                                                                         \
    std::cout << "Running " << #fn << "..." << std::flush;                     \
    fn();                                                                      \
    if (gTestsFailed == 0)                                                     \
      std::cout << " PASS" << std::endl;                                       \
    gTestsPassed++;                                                            \
  } while (0)

using namespace fiddle;
using Strip = GainMath::Strip;

static constexpr float kEps = 1e-4f;

// ===========================================================================
// Basic conversion tests
// ===========================================================================

void testGainFromDb() {
  // 0 dB → gain 1
  CHECK_NEAR(GainMath::gainFromDb(0.0f), 1.0f, kEps);
  // -6 dB → ~0.5012 (half amplitude)
  CHECK_NEAR(GainMath::gainFromDb(-6.0f), std::pow(10.0f, -6.0f / 20.0f), kEps);
  // +6 dB → ~1.9953
  CHECK_NEAR(GainMath::gainFromDb(6.0f), std::pow(10.0f, 6.0f / 20.0f), kEps);
  // At or below -100 dB → 0 (silence)
  CHECK_NEAR(GainMath::gainFromDb(-100.0f), 0.0f, kEps);
  CHECK_NEAR(GainMath::gainFromDb(-120.0f), 0.0f, kEps);
}

void testDbFromGain() {
  // gain 1 → 0 dB
  CHECK_NEAR(GainMath::dbFromGain(1.0f), 0.0f, kEps);
  // Round-trip
  CHECK_NEAR(GainMath::dbFromGain(GainMath::gainFromDb(-12.0f)), -12.0f, kEps);
  // gain 0 → -100 dB (minus infinity)
  CHECK_NEAR(GainMath::dbFromGain(0.0f), GainMath::kMinusInfinityDb, kEps);
  // negative gain → -100 dB
  CHECK_NEAR(GainMath::dbFromGain(-1.0f), GainMath::kMinusInfinityDb, kEps);
}

void testPowerFromDb() {
  // 0 dB → gain 1 → power 1
  CHECK_NEAR(GainMath::powerFromDb(0.0f), 1.0f, kEps);
  // -6 dB → power ≈ 0.2512
  float g6 = GainMath::gainFromDb(-6.0f);
  CHECK_NEAR(GainMath::powerFromDb(-6.0f), g6 * g6, kEps);
}

// ===========================================================================
// Doc Example 1: Three strips at 0 dB, not locked
// ===========================================================================

void testExample1_ThreeStripsUnlocked() {
  std::vector<Strip> strips = {{0.0f, false}, {0.0f, false}, {0.0f, false}};

  float gp = GainMath::groupPower(strips);
  CHECK_NEAR(gp, 3.0f, kEps);

  float gg = GainMath::groupGain(strips);
  CHECK_NEAR(gg, std::sqrt(3.0f), kEps);

  float gdb = GainMath::groupDb(strips);
  CHECK_NEAR(gdb, GainMath::dbFromGain(std::sqrt(3.0f)), kEps);
}

// ===========================================================================
// Doc Example 2: Mute one of three strips, not locked
// ===========================================================================

void testExample2_MuteOneUnlocked() {
  std::vector<Strip> strips = {{0.0f, false}, {0.0f, false}, {0.0f, true}};

  float gp = GainMath::groupPower(strips);
  CHECK_NEAR(gp, 2.0f, kEps);

  float gg = GainMath::groupGain(strips);
  CHECK_NEAR(gg, std::sqrt(2.0f), kEps);
}

// ===========================================================================
// Doc Example 3: Locked group, only one unmuted strip — group tracks it
// ===========================================================================

void testExample3_LockedOnlyOneUnmuted() {
  // Two strips muted, one at 0 dB. User moves non-muted strip to +3 dB.
  std::vector<Strip> strips = {{3.0f, false}, {0.0f, true}, {0.0f, true}};

  float oldGroupPower = 1.0f; // Was 1 strip at 0 dB = power 1
  // (Actually before the move, only strip 0 was unmuted at 0 dB = power 1)

  auto result =
      GainMath::redistributeForLockedGroup(strips, 0, oldGroupPower);

  // No other unmuted strips to redistribute to, so result == input
  // (group fader must follow the strip instead)
  CHECK_NEAR(result[0].db, 3.0f, kEps);

  // Verify group power now reflects just the one strip
  float newGP = GainMath::groupPower(result);
  float expectedPower = GainMath::powerFromDb(3.0f);
  CHECK_NEAR(newGP, expectedPower, kEps);
}

// ===========================================================================
// Doc Example 4: Locked group, mute one strip → others compensate
// ===========================================================================

void testExample4_LockedMuteOne() {
  // Start: 3 strips at 0 dB, group power = 3.
  // User mutes strip 2. Treat mute as "changed index" with muted=true.
  float oldGroupPower = 3.0f;

  std::vector<Strip> strips = {{0.0f, false}, {0.0f, false}, {0.0f, true}};

  auto result = GainMath::redistributeForLockedGroup(strips, 2, oldGroupPower);

  // Muted strip contributes 0, so other two must total 3.
  // Each was at power 1, now must be at power 3/2.
  float expectedStripGain = std::sqrt(3.0f / 2.0f);
  float expectedStripDb = GainMath::dbFromGain(expectedStripGain);

  CHECK_NEAR(result[0].db, expectedStripDb, kEps);
  CHECK_NEAR(result[1].db, expectedStripDb, kEps);
  CHECK(result[2].muted);

  // Verify group power is preserved
  float newGP = GainMath::groupPower(result);
  CHECK_NEAR(newGP, oldGroupPower, kEps);
}

// ===========================================================================
// Doc Example 5: Locked group, group fader moved down 3 dB
// ===========================================================================

void testExample5_LockedGroupFaderMoved() {
  // Start: 3 strips at 0 dB. Group power = 3. Group gain = sqrt(3).
  // Group fader = dbFromGain(sqrt(3)).
  // User moves group fader down 3 dB.

  float oldGroupPower = 3.0f;
  float oldGroupDbVal = GainMath::dbFromGain(std::sqrt(3.0f));
  float newGroupDbVal = oldGroupDbVal - 3.0f;

  std::vector<Strip> strips = {{0.0f, false}, {0.0f, false}, {0.0f, false}};

  auto result = GainMath::scaleAllStrips(strips, oldGroupPower, newGroupDbVal);

  // New group gain
  float newGroupGain = GainMath::gainFromDb(newGroupDbVal);
  float newGroupPower = newGroupGain * newGroupGain;

  // Each strip should have power = newGroupPower / 3
  float expectedStripPower = newGroupPower / 3.0f;
  float expectedStripGain = std::sqrt(expectedStripPower);
  float expectedStripDb = GainMath::dbFromGain(expectedStripGain);

  CHECK_NEAR(result[0].db, expectedStripDb, kEps);
  CHECK_NEAR(result[1].db, expectedStripDb, kEps);
  CHECK_NEAR(result[2].db, expectedStripDb, kEps);

  // Verify group power
  float resultGroupPower = GainMath::groupPower(result);
  CHECK_NEAR(resultGroupPower, newGroupPower, kEps);
}

// ===========================================================================
// Edge case: all muted → group is silence
// ===========================================================================

void testAllMuted() {
  std::vector<Strip> strips = {{0.0f, true}, {-6.0f, true}, {3.0f, true}};

  CHECK_NEAR(GainMath::groupPower(strips), 0.0f, kEps);
  CHECK_NEAR(GainMath::groupGain(strips), 0.0f, kEps);
  CHECK_NEAR(GainMath::groupDb(strips), GainMath::kMinusInfinityDb, kEps);
}

// ===========================================================================
// Edge case: single strip, locked → group tracks strip
// ===========================================================================

void testSingleStripLocked() {
  std::vector<Strip> strips = {{-12.0f, false}};

  // Group should exactly equal the one strip
  CHECK_NEAR(GainMath::groupDb(strips), -12.0f, kEps);

  // "Redistribute" with single strip: no other strips → result unchanged
  float oldGP = GainMath::groupPower(strips);
  strips[0].db = -6.0f;
  auto result = GainMath::redistributeForLockedGroup(strips, 0, oldGP);
  CHECK_NEAR(result[0].db, -6.0f, kEps);
}

// ===========================================================================
// Edge case: mute then unmute round-trip preserves faders
// ===========================================================================

void testMuteUnmuteRoundTrip() {
  // 3 strips at different levels, locked.
  std::vector<Strip> strips = {{0.0f, false}, {-6.0f, false}, {-12.0f, false}};

  float originalGroupPower = GainMath::groupPower(strips);

  // Mute strip 1 (treat as changedIndex=1)
  std::vector<Strip> afterMute = strips;
  afterMute[1].muted = true;
  auto redistributed =
      GainMath::redistributeForLockedGroup(afterMute, 1, originalGroupPower);

  // Group power should be preserved
  CHECK_NEAR(GainMath::groupPower(redistributed), originalGroupPower, kEps);

  // Now unmute strip 1 — restore its old dB value.
  // This is the inverse: strip 1 goes from muted→unmuted with remembered dB.
  std::vector<Strip> afterUnmute = redistributed;
  afterUnmute[1].muted = false;
  afterUnmute[1].db = -6.0f; // restored from memory

  // The group power changes, so the group fader moves (since unmute acts
  // like moving from -inf to the remembered value).
  // In unlocked mode, the group fader simply updates.
  // In locked mode, we redistribute from the other strips.
  auto restored = GainMath::redistributeForLockedGroup(
      afterUnmute, 1, originalGroupPower);

  // Group power should still be originalGroupPower
  CHECK_NEAR(GainMath::groupPower(restored), originalGroupPower, kEps);

  // Strip 1 should be at its remembered value
  CHECK_NEAR(restored[1].db, -6.0f, kEps);

  // Strips 0 and 2 should be back to their original values
  CHECK_NEAR(restored[0].db, 0.0f, kEps);
  CHECK_NEAR(restored[2].db, -12.0f, kEps);
}

// ===========================================================================
// Edge case: group fader moved to silence
// ===========================================================================

void testGroupFaderMovedToSilence() {
  std::vector<Strip> strips = {{0.0f, false}, {-3.0f, false}};
  float oldGP = GainMath::groupPower(strips);

  auto result =
      GainMath::scaleAllStrips(strips, oldGP, GainMath::kMinusInfinityDb);

  // All strips should be at silence
  CHECK_NEAR(result[0].db, GainMath::kMinusInfinityDb, kEps);
  CHECK_NEAR(result[1].db, GainMath::kMinusInfinityDb, kEps);
  CHECK_NEAR(GainMath::groupPower(result), 0.0f, kEps);
}

// ===========================================================================
// Edge case: non-uniform strips, locked, one strip moved
// ===========================================================================

void testNonUniformStripsLocked() {
  // Strips at 0 dB, -6 dB, -12 dB. User moves strip 0 to -3 dB (quieter).
  std::vector<Strip> strips = {{0.0f, false}, {-6.0f, false}, {-12.0f, false}};
  float oldGP = GainMath::groupPower(strips); // compute BEFORE the move

  // Move strip 0 to -3 dB (frees up power for the others)
  strips[0].db = -3.0f;
  auto result = GainMath::redistributeForLockedGroup(strips, 0, oldGP);

  // Group power should be preserved
  CHECK_NEAR(GainMath::groupPower(result), oldGP, kEps);

  // Strip 0 should be at its new value
  CHECK_NEAR(result[0].db, -3.0f, kEps);

  // Strips 1 and 2 should have been scaled proportionally — their ratio
  // of powers should be the same as before.
  float p1 = GainMath::powerFromDb(result[1].db);
  float p2 = GainMath::powerFromDb(result[2].db);
  float origP1 = GainMath::powerFromDb(-6.0f);
  float origP2 = GainMath::powerFromDb(-12.0f);

  // p1/p2 should equal origP1/origP2
  CHECK_NEAR(p1 / p2, origP1 / origP2, kEps);
}

// ===========================================================================
// Edge case: redistribute when all other strips are at silence
// ===========================================================================

void testRedistributeAllOthersAtSilence() {
  // Strip 0 at 0 dB, strips 1 and 2 at -100 dB (silence).
  std::vector<Strip> strips = {{0.0f, false},
                                {GainMath::kMinusInfinityDb, false},
                                {GainMath::kMinusInfinityDb, false}};

  float oldGP = GainMath::groupPower(strips); // ≈ 1.0

  // Move strip 0 to +3 dB
  strips[0].db = 3.0f;
  auto result = GainMath::redistributeForLockedGroup(strips, 0, oldGP);

  // Other strips are at zero power — they can't absorb.
  // Result should be returned as-is (group fader must track).
  CHECK_NEAR(result[0].db, 3.0f, kEps);
  CHECK_NEAR(result[1].db, GainMath::kMinusInfinityDb, kEps);
  CHECK_NEAR(result[2].db, GainMath::kMinusInfinityDb, kEps);
}

// ===========================================================================
// Main
// ===========================================================================

int main() {
  std::cout << "===== GainMath Tests =====" << std::endl;

  RUN_TEST(testGainFromDb);
  RUN_TEST(testDbFromGain);
  RUN_TEST(testPowerFromDb);
  RUN_TEST(testExample1_ThreeStripsUnlocked);
  RUN_TEST(testExample2_MuteOneUnlocked);
  RUN_TEST(testExample3_LockedOnlyOneUnmuted);
  RUN_TEST(testExample4_LockedMuteOne);
  RUN_TEST(testExample5_LockedGroupFaderMoved);
  RUN_TEST(testAllMuted);
  RUN_TEST(testSingleStripLocked);
  RUN_TEST(testMuteUnmuteRoundTrip);
  RUN_TEST(testGroupFaderMovedToSilence);
  RUN_TEST(testNonUniformStripsLocked);
  RUN_TEST(testRedistributeAllOthersAtSilence);

  std::cout << std::endl;
  std::cout << "Passed: " << gTestsPassed << std::endl;
  std::cout << "Failed: " << gTestsFailed << std::endl;

  return gTestsFailed > 0 ? 1 : 0;
}
