//==============================================================================
// HarmonicAnalyzerTest.cpp
//
// Unit tests for the HMM harmonic analysis engine.
// Tests: state indexing, emission math, beam search, ring buffer, dual-bound lag.
//==============================================================================

#include "HarmonicState.h"
#include "HarmonicAnalyzer.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <string>

namespace {

int passed = 0;
int failed = 0;

void check(bool condition, const std::string& testName) {
  if (condition) {
    ++passed;
  } else {
    ++failed;
    std::cerr << "  FAIL: " << testName << std::endl;
  }
}

using namespace fiddle::hmm;

//==============================================================================
// State Index Round-Trip Tests
//==============================================================================
void testStateIndexRoundTrip() {
  std::cerr << "=== State Index Round-Trip ===" << std::endl;

  // Test all 24 key indices
  for (int root = 0; root < 12; ++root) {
    for (bool minor : {false, true}) {
      int ki = keyIndex(root, minor);
      auto [r, m] = keyFromIndex(ki);
      check(r == root && m == minor,
            "keyIndex roundtrip root=" + std::to_string(root) +
            " minor=" + std::to_string(minor));
    }
  }

  // Test all 108 chord indices
  for (int root = 0; root < 12; ++root) {
    for (int q = 0; q < kNumQualities; ++q) {
      auto quality = static_cast<ChordQuality>(q);
      int ci = chordIndex(root, quality);
      auto [r, qual] = chordFromIndex(ci);
      check(r == root && qual == quality,
            "chordIndex roundtrip root=" + std::to_string(root) +
            " quality=" + std::to_string(q));
    }
  }

  // Test all 2592 flat state indices
  for (int ki = 0; ki < kNumKeys; ++ki) {
    for (int ci = 0; ci < kNumChords; ++ci) {
      int si = stateIndex(ki, ci);
      auto [rki, rci] = stateFromIndex(si);
      check(rki == ki && rci == ci,
            "stateIndex roundtrip ki=" + std::to_string(ki) +
            " ci=" + std::to_string(ci));
    }
  }

  // Test HarmonicState roundtrip
  HarmonicState s;
  s.keyRoot = 7;     // G
  s.keyMinor = true;  // G minor
  s.chordRoot = 2;   // D
  s.quality = ChordQuality::Dom7;
  int idx = s.toStateIndex();
  HarmonicState s2 = HarmonicState::fromStateIndex(idx);
  check(s2.keyRoot == 7 && s2.keyMinor == true &&
        s2.chordRoot == 2 && s2.quality == ChordQuality::Dom7,
        "HarmonicState roundtrip G minor, D dom7");
}

//==============================================================================
// Emission Computation Tests
//==============================================================================
void testEmissionComputation() {
  std::cerr << "=== Emission Computation ===" << std::endl;

  HarmonicAnalyzer analyzer;
  analyzer.buildDefaultEmissionTemplates();

  // C major chord (root position): bass = C (0), upper = C E G
  Observation obs;
  obs.bassPitchClass = 0; // C
  obs.upperChroma.fill(0.f);
  obs.upperChroma[0] = 1.f; // C
  obs.upperChroma[4] = 1.f; // E
  obs.upperChroma[7] = 1.f; // G

  // The C major chord emission should be the highest among all chords
  // for this observation.
  int cMajIdx = chordIndex(0, ChordQuality::Major);

  // Compare C major emission against several others.
  // Use observeEvent to indirectly test emission (we can't call
  // computeEmission directly since it's private), but we can check
  // that the analyzer selects C major as the best state.
  analyzer.observeEvent(obs);
  HarmonicState best = analyzer.currentBestState();

  // The best chord should be C major (root=0, quality=Major).
  check(best.chordRoot == 0, "C major observation → chord root = C");
  check(best.quality == ChordQuality::Major,
        "C major observation → quality = Major");
}

//==============================================================================
// Beam Search: I–IV–V–I Progression
//==============================================================================
void testBeamSearchProgression() {
  std::cerr << "=== Beam Search Progression ===" << std::endl;

  HarmonicAnalyzer analyzer;
  analyzer.buildDefaultEmissionTemplates();
  analyzer.setLag(1); // Short lag for testing.

  // Simulate I–IV–V–I in C major.
  // I: C E G
  {
    Observation obs;
    obs.bassPitchClass = 0;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[0] = 1.f; // C
    obs.upperChroma[4] = 1.f; // E
    obs.upperChroma[7] = 1.f; // G
    obs.timestampMs = 0.0;
    analyzer.observeEvent(obs);
  }

  // IV: F A C
  {
    Observation obs;
    obs.bassPitchClass = 5;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[5] = 1.f; // F
    obs.upperChroma[9] = 1.f; // A
    obs.upperChroma[0] = 1.f; // C
    obs.timestampMs = 500.0;
    analyzer.observeEvent(obs);
  }

  // V: G B D
  {
    Observation obs;
    obs.bassPitchClass = 7;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[7] = 1.f; // G
    obs.upperChroma[11] = 1.f; // B
    obs.upperChroma[2] = 1.f; // D
    obs.timestampMs = 1000.0;
    analyzer.observeEvent(obs);
  }

  // I: C E G
  {
    Observation obs;
    obs.bassPitchClass = 0;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[0] = 1.f; // C
    obs.upperChroma[4] = 1.f; // E
    obs.upperChroma[7] = 1.f; // G
    obs.timestampMs = 1500.0;
    analyzer.observeEvent(obs);
  }

  // After 4 events with lag=1, commitDecision should return an earlier state.
  HarmonicState decided = analyzer.commitDecision();
  // We can't guarantee exact chord with default templates and no transitions,
  // but the decided state should at least be valid (non-default).
  check(analyzer.eventCount() == 4, "4 events processed");
  check(decided.logProbability > -1e29f, "decided state has valid probability");

  // The current best should reflect the last observation (C major or close).
  HarmonicState current = analyzer.currentBestState();
  check(current.chordRoot == 0 || current.chordRoot == 7,
        "current best root is C or G (reasonable for C major chord)");
}

//==============================================================================
// Ring Buffer Wraparound
//==============================================================================
void testRingBufferWraparound() {
  std::cerr << "=== Ring Buffer Wraparound ===" << std::endl;

  HarmonicAnalyzer analyzer;
  analyzer.buildDefaultEmissionTemplates();
  analyzer.setLag(3);  // Ring buffer size = 4.

  // Feed 10 events to force multiple wraparounds.
  for (int i = 0; i < 10; ++i) {
    Observation obs;
    obs.bassPitchClass = i % 12;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[i % 12] = 1.f;
    obs.upperChroma[(i + 4) % 12] = 1.f; // major third
    obs.upperChroma[(i + 7) % 12] = 1.f; // fifth
    obs.timestampMs = i * 200.0;
    analyzer.observeEvent(obs);
  }

  check(analyzer.eventCount() == 10, "10 events after wraparound");

  // commitDecision should still work after wraparound.
  HarmonicState decided = analyzer.commitDecision();
  check(decided.logProbability > -1e29f,
        "valid decision after ring buffer wraparound");
}

//==============================================================================
// Reset
//==============================================================================
void testReset() {
  std::cerr << "=== Reset ===" << std::endl;

  HarmonicAnalyzer analyzer;
  analyzer.buildDefaultEmissionTemplates();

  // Feed some events.
  for (int i = 0; i < 5; ++i) {
    Observation obs;
    obs.bassPitchClass = 0;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[0] = 1.f;
    obs.timestampMs = i * 100.0;
    analyzer.observeEvent(obs);
  }

  check(analyzer.eventCount() == 5, "5 events before reset");

  analyzer.reset();
  check(analyzer.eventCount() == 0, "0 events after reset");

  // Should be able to feed events again after reset.
  Observation obs;
  obs.bassPitchClass = 0;
  obs.upperChroma.fill(0.f);
  obs.upperChroma[0] = 1.f;
  obs.timestampMs = 0.0;
  analyzer.observeEvent(obs);
  check(analyzer.eventCount() == 1, "1 event after reset + feed");
}

//==============================================================================
// Dual-Bound Lag (Time-Based Force Commit)
//==============================================================================
void testDualBoundLag() {
  std::cerr << "=== Dual-Bound Lag ===" << std::endl;

  HarmonicAnalyzer analyzer;
  analyzer.buildDefaultEmissionTemplates();
  analyzer.setLag(10); // Very large lag — normally needs 10 events to commit.

  // Feed only 2 events.
  {
    Observation obs;
    obs.bassPitchClass = 0;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[0] = 1.f;
    obs.timestampMs = 1000.0;
    analyzer.observeEvent(obs);
  }
  {
    Observation obs;
    obs.bassPitchClass = 0;
    obs.upperChroma.fill(0.f);
    obs.upperChroma[0] = 1.f;
    obs.timestampMs = 1100.0;
    analyzer.observeEvent(obs);
  }

  // With a playback delay of 500ms, querying at time 1600ms should force commit
  // (oldest event at 1000ms is 600ms ago, > 500ms delay).
  check(analyzer.shouldForceCommit(1600.0, 500.0),
        "force commit when oldest event exceeds delay");

  // At time 1200ms, oldest event is only 200ms ago — no force.
  check(!analyzer.shouldForceCommit(1200.0, 500.0),
        "no force commit when within delay budget");
}

//==============================================================================
// Quality Names
//==============================================================================
void testQualityNames() {
  std::cerr << "=== Quality Names ===" << std::endl;

  check(std::string(qualityName(ChordQuality::Major)) == "maj", "Major → maj");
  check(std::string(qualityName(ChordQuality::Minor)) == "min", "Minor → min");
  check(std::string(qualityName(ChordQuality::Dom7)) == "dom7", "Dom7 → dom7");
  check(std::string(qualityName(ChordQuality::HalfDim7)) == "hdim7",
        "HalfDim7 → hdim7");
  check(std::string(pitchClassName(0)) == "C", "PC 0 → C");
  check(std::string(pitchClassName(7)) == "G", "PC 7 → G");
}

} // anonymous namespace

int main() {
  std::cerr << "\n╔══════════════════════════════════════════╗" << std::endl;
  std::cerr << "║    HarmonicAnalyzer Unit Tests           ║" << std::endl;
  std::cerr << "╚══════════════════════════════════════════╝\n" << std::endl;

  testStateIndexRoundTrip();
  testEmissionComputation();
  testBeamSearchProgression();
  testRingBufferWraparound();
  testReset();
  testDualBoundLag();
  testQualityNames();

  std::cerr << "\n──────────────────────────────────────────" << std::endl;
  std::cerr << "Results: " << passed << " passed, " << failed << " failed"
            << std::endl;
  std::cerr << "──────────────────────────────────────────\n" << std::endl;

  return failed > 0 ? 1 : 0;
}
