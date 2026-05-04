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

//==============================================================================
// Chord Progression Integration Tests
//
// Tests are written in chord-symbol notation:
//   runChordTest("name", "C", {"C", "Dm7", "G7", "C"}, analyzer);
// The framework parses symbols → observations → expected states.
//==============================================================================

namespace progression_tests {

using namespace fiddle::hmm;

// ── Pitch class parsing ──────────────────────────────────────────────────────

/// Parse a pitch class name (C, C#, Db, D, ..., B) from a string.
/// Returns the pitch class (0-11) and the number of characters consumed.
std::pair<int, int> parsePitchClass(const std::string& s, int pos = 0) {
  if (pos >= static_cast<int>(s.size()))
    return {-1, 0};

  // Map note letter to pitch class.
  int pc = -1;
  switch (s[pos]) {
    case 'C': pc = 0;  break;
    case 'D': pc = 2;  break;
    case 'E': pc = 4;  break;
    case 'F': pc = 5;  break;
    case 'G': pc = 7;  break;
    case 'A': pc = 9;  break;
    case 'B': pc = 11; break;
    default:  return {-1, 0};
  }
  int consumed = 1;

  // Check for sharp/flat.
  if (pos + 1 < static_cast<int>(s.size())) {
    if (s[pos + 1] == '#') { pc = (pc + 1) % 12; ++consumed; }
    else if (s[pos + 1] == 'b') { pc = (pc + 11) % 12; ++consumed; }
  }
  return {pc, consumed};
}

// ── Chord symbol parsing ─────────────────────────────────────────────────────

/// A parsed chord symbol.
struct ParsedChord {
  int root = -1;                           // pitch class 0-11
  ChordQuality quality = ChordQuality::Major;
  int bass = -1;                           // pitch class for slash bass, or -1
  std::string label;                       // original symbol
};

/// Interval patterns per quality (semitones above root).
const std::vector<int>& qualityIntervals(ChordQuality q) {
  static const std::vector<int> patterns[] = {
    {0, 4, 7},           // Major
    {0, 3, 7},           // Minor
    {0, 3, 6},           // Diminished
    {0, 4, 8},           // Augmented
    {0, 4, 7, 10},       // Dom7
    {0, 4, 7, 11},       // Maj7
    {0, 3, 7, 10},       // Min7
    {0, 3, 6, 9},        // Dim7
    {0, 3, 6, 10},       // HalfDim7
  };
  return patterns[static_cast<int>(q)];
}

/// Parse a chord symbol like "Dm7/C", "G#aug", "Bb", "F#m", "Bhdim7".
ParsedChord parseChord(const std::string& symbol) {
  ParsedChord chord;
  chord.label = symbol;

  // Parse root.
  auto [rootPc, rootLen] = parsePitchClass(symbol, 0);
  if (rootPc < 0) {
    std::cerr << "    ERROR: Can't parse root from '" << symbol << "'" << std::endl;
    return chord;
  }
  chord.root = rootPc;

  // Parse quality from remainder (before any '/').
  std::string rest = symbol.substr(rootLen);
  size_t slashPos = rest.find('/');
  std::string qualStr = (slashPos != std::string::npos)
                            ? rest.substr(0, slashPos)
                            : rest;

  if      (qualStr.empty())        chord.quality = ChordQuality::Major;
  else if (qualStr == "m")         chord.quality = ChordQuality::Minor;
  else if (qualStr == "dim")       chord.quality = ChordQuality::Diminished;
  else if (qualStr == "aug")       chord.quality = ChordQuality::Augmented;
  else if (qualStr == "7")         chord.quality = ChordQuality::Dom7;
  else if (qualStr == "maj7")      chord.quality = ChordQuality::Maj7;
  else if (qualStr == "m7")        chord.quality = ChordQuality::Min7;
  else if (qualStr == "dim7")      chord.quality = ChordQuality::Dim7;
  else if (qualStr == "hdim7")     chord.quality = ChordQuality::HalfDim7;
  else {
    std::cerr << "    WARNING: Unknown quality '" << qualStr
              << "' in '" << symbol << "', defaulting to Major" << std::endl;
  }

  // Parse slash bass.
  if (slashPos != std::string::npos) {
    std::string bassStr = rest.substr(slashPos + 1);
    auto [bassPc, bassLen] = parsePitchClass(bassStr, 0);
    if (bassPc >= 0)
      chord.bass = bassPc;
    else
      std::cerr << "    WARNING: Can't parse bass from '/" << bassStr
                << "' in '" << symbol << "'" << std::endl;
  }

  // Default bass = root.
  if (chord.bass < 0)
    chord.bass = chord.root;

  return chord;
}

/// Parse a key string like "C", "Am", "F#", "Bbm".
std::pair<int, bool> parseKey(const std::string& key) {
  auto [pc, len] = parsePitchClass(key, 0);
  bool minor = (len < static_cast<int>(key.size()) && key[len] == 'm');
  return {pc, minor};
}

// ── Observation building ─────────────────────────────────────────────────────

/// Build an Observation from a parsed chord.
Observation chordToObs(const ParsedChord& chord, double timeMs) {
  Observation obs;
  obs.bassPitchClass = chord.bass;
  obs.upperChroma.fill(0.f);
  obs.timestampMs = timeMs;

  const auto& intervals = qualityIntervals(chord.quality);
  for (int iv : intervals) {
    int pc = (chord.root + iv) % 12;
    obs.upperChroma[pc] += 100.f / 127.f;  // ~0.787, velocity=100
  }
  return obs;
}

// ── Test helpers ─────────────────────────────────────────────────────────────

/// Load binary file into a vector<uint8_t>. Returns empty on failure.
std::vector<uint8_t> loadFile(const std::string& path) {
  FILE* f = fopen(path.c_str(), "rb");
  if (!f) return {};
  fseek(f, 0, SEEK_END);
  auto size = ftell(f);
  fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(size);
  fread(data.data(), 1, size, f);
  fclose(f);
  return data;
}

/// Forward declaration.
std::pair<int, int> runModulatingTest(
    const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& entries,
    HarmonicAnalyzer& analyzer);

/// Run a chord progression test from string notation (single key).
/// Returns (passed, failed) counts.
std::pair<int, int> runChordTest(
    const std::string& name,
    const std::string& keyStr,
    const std::vector<std::string>& chordSymbols,
    HarmonicAnalyzer& analyzer) {

  // Convert to per-chord key entries and delegate.
  std::vector<std::pair<std::string, std::string>> entries;
  entries.reserve(chordSymbols.size());
  for (const auto& chord : chordSymbols)
    entries.push_back({keyStr, chord});

  // Display with (key=...) for single-key tests.
  std::string displayName = name + " (key=" + keyStr + ")";
  return runModulatingTest(displayName, entries, analyzer);
}

/// Run a modulating progression test where each chord has its own expected key.
/// Format: vector of {key, chord} pairs, e.g. {{"C","Dm7"}, {"G","D7"}}.
/// Returns (passed, failed) counts.
std::pair<int, int> runModulatingTest(
    const std::string& name,
    const std::vector<std::pair<std::string, std::string>>& entries,
    HarmonicAnalyzer& analyzer) {

  int p = 0, f = 0;

  analyzer.reset();
  std::cerr << "  " << name << ":";

  for (size_t i = 0; i < entries.size(); ++i) {
    const auto& [keyStr, chordStr] = entries[i];
    auto [keyRoot, keyMinor] = parseKey(keyStr);
    ParsedChord parsed = parseChord(chordStr);
    Observation obs = chordToObs(parsed, i * 500.0);
    analyzer.observeEvent(obs);

    HarmonicState best = analyzer.currentBestState();

    bool chordOk = (best.chordRoot == parsed.root &&
                    best.quality == parsed.quality);
    bool keyOk   = (best.keyRoot == keyRoot &&
                    best.keyMinor == keyMinor);

    if (chordOk && keyOk) {
      ++p;
    } else {
      ++f;
      std::string gotKey = std::string(pitchClassName(best.keyRoot)) +
                           (best.keyMinor ? "m" : "");
      std::string gotChord = std::string(pitchClassName(best.chordRoot)) +
                             qualityName(best.quality);
      std::string expKey = std::string(pitchClassName(keyRoot)) +
                           (keyMinor ? "m" : "");
      std::string expChord = std::string(pitchClassName(parsed.root)) +
                             qualityName(parsed.quality);
      std::cerr << std::endl
                << "    FAIL event " << (i + 1) << " (" << chordStr
                << "): got " << gotKey << ":" << gotChord
                << ", expected " << expKey << ":" << expChord;
    }
  }

  if (f == 0)
    std::cerr << " OK (" << p << "/" << p << ")" << std::endl;
  else
    std::cerr << std::endl;

  return {p, f};
}

// ── Test Progressions ────────────────────────────────────────────────────────

void runAllProgressionTests(int& totalPassed, int& totalFailed) {
  std::cerr << "=== Chord Progression Tests ===" << std::endl;

  // Load trained models.
  auto transData = loadFile("resources/hmm/cpe_transitions.bin");
  auto priorsData = loadFile("resources/hmm/degree_priors.bin");

  HarmonicAnalyzer analyzer;
  analyzer.buildDefaultEmissionTemplates();
  analyzer.setLag(1);

  bool hasTrans = false, hasPriors = false;
  if (!transData.empty())
    hasTrans = analyzer.loadTransitionMatrix(transData.data(), transData.size());
  if (!priorsData.empty())
    hasPriors = analyzer.loadDegreePriors(priorsData.data(), priorsData.size());

  std::cerr << "  Transitions: " << (hasTrans ? "loaded" : "NOT FOUND")
            << ", Priors: " << (hasPriors ? "loaded" : "NOT FOUND")
            << std::endl;

  // Shorthand for single-key tests.
  auto run = [&](const std::string& name, const std::string& key,
                 std::initializer_list<std::string> chords) {
    auto [p, f] = runChordTest(name, key, chords, analyzer);
    totalPassed += p; totalFailed += f;
  };

  // Shorthand for modulating tests.
  auto runMod = [&](const std::string& name,
                    std::initializer_list<std::pair<std::string, std::string>> entries) {
    auto [p, f] = runModulatingTest(name, entries, analyzer);
    totalPassed += p; totalFailed += f;
  };

  // ── Major key progressions ─────────────────────────────────────────────
  run("I-IV-V-I",       "C",  {"C",  "F",  "G",  "C"});
  run("I-ii-V-I",       "C",  {"C",  "Dm", "G",  "C"});
  run("I-vi-IV-V",      "G",  {"G",  "Em", "C",  "D"});
  run("I-V-vi-IV",      "C",  {"C",  "G",  "Am", "F"});

  // ── Seventh chords ─────────────────────────────────────────────────────
  run("I-ii7-V7-I",     "C",  {"C",  "Dm7", "G7",   "C"});
  run("I-IV-V7-I",      "G",  {"G",  "C",   "D7",   "G"});

  // ── Minor key progressions ─────────────────────────────────────────────
  run("i-iv-V-i",       "Am", {"Am", "Dm",  "E",    "Am"});
  run("i-iv-V7-i",      "Am", {"Am", "Dm",  "E7",   "Am"});

  // ── Inversions ─────────────────────────────────────────────────────────
  run("I-IV/C-V-I",     "C",  {"C",  "F/C",   "G",  "C"});
  run("I-ii7/C-V7/B-I", "C",  {"C",  "Dm7/C", "G7/B", "C"});

  // ── User progressions ─────────────────────────────────────────────────
  run("I-vi-ii7-V7-I",      "C", {"C", "Am", "Dm7", "G7/B", "C"});
  run("I-iii-vi-IV-V7-I",   "C", {"C", "Em", "Am/E", "F", "G7", "C"});
  run("I-Imaj7-IV-ii-V-I",  "C", {"C", "Cmaj7", "F", "Dm", "G", "C"});

  // ── Modulating progressions ────────────────────────────────────────────

  // Bach WTC I, Prelude in C (mm. 1-11, arpeggiated chords)
  // Modulates from C major to G major via secondary dominant D7.
  runMod("Bach WTC I Prelude (C→G)", {
    {"C", "C"},        // I
    {"C", "Dm7/C"},    // ii7
    {"C", "G/B"},      // V
    {"C", "C"},        // I
    {"C", "Am/C"},     // vi
    {"C", "D7/C"},     // V7/V (secondary dominant)
    {"G", "G/B"},      // I  (now in G)
    {"G", "Cmaj7/B"},  // IV
    {"G", "Am"},       // ii
    {"G", "D7"},       // V7
    {"G", "G"},        // I
  });
}

} // namespace progression_tests

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

  // Progression integration tests (load real binaries if available).
  progression_tests::runAllProgressionTests(passed, failed);

  std::cerr << "\n──────────────────────────────────────────" << std::endl;
  std::cerr << "Results: " << passed << " passed, " << failed << " failed"
            << std::endl;
  std::cerr << "──────────────────────────────────────────\n" << std::endl;

  return failed > 0 ? 1 : 0;
}
