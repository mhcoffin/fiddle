#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <utility>

namespace fiddle {
namespace hmm {

// ─────────────────────────────────────────────────────────────────────────────
// Chord Quality Enumeration
//
// 9 qualities covering the essential chord types for Common Practice harmony.
// Augmented 6th chords are modeled as enharmonic respellings (e.g., Ger+6 =
// Dom7 on ♭VI).
// ─────────────────────────────────────────────────────────────────────────────
enum class ChordQuality : uint8_t {
  // Triads
  Major      = 0,
  Minor      = 1,
  Diminished = 2,
  Augmented  = 3,
  // Seventh chords
  Dom7       = 4,
  Maj7       = 5,
  Min7       = 6,
  Dim7       = 7,
  HalfDim7   = 8
};

constexpr int kNumQualities = 9;
constexpr int kNumRoots     = 12;
constexpr int kNumChords    = kNumRoots * kNumQualities;  // 108
constexpr int kNumKeys      = 24;                          // 12 major + 12 minor
constexpr int kNumStates    = kNumKeys * kNumChords;       // 2592

/// Maximum number of outgoing transitions stored per state (sparse top-K).
constexpr int kDefaultTopK  = 50;

/// Default Viterbi lookahead depth (MIDI events).
constexpr int kDefaultLag   = 2;

/// Default beam width.
constexpr int kDefaultBeamWidth = 128;

// ─────────────────────────────────────────────────────────────────────────────
// Index Conversion
//
// The flat state index encodes (key, chord) as:
//   stateIdx = keyIdx * kNumChords + chordIdx
//
// Key index encodes 24 keys as:
//   keyIdx = root * 2 + (minor ? 1 : 0)
//   → 0=C major, 1=C minor, 2=C# major, 3=C# minor, …, 23=B minor
//
// Chord index encodes 108 chords as:
//   chordIdx = root * kNumQualities + (int)quality
//   → 0=C Major, 1=C Minor, …, 8=C HalfDim7, 9=C# Major, …
// ─────────────────────────────────────────────────────────────────────────────

/// Key root (0–11) + mode → key index (0–23).
inline int keyIndex(int root, bool minor) {
  return root * 2 + (minor ? 1 : 0);
}

/// Key index (0–23) → (root, minor).
inline std::pair<int, bool> keyFromIndex(int keyIdx) {
  return {keyIdx / 2, (keyIdx % 2) == 1};
}

/// Chord root (0–11) + quality → chord index (0–107).
inline int chordIndex(int root, ChordQuality quality) {
  return root * kNumQualities + static_cast<int>(quality);
}

/// Chord index (0–107) → (root, quality).
inline std::pair<int, ChordQuality> chordFromIndex(int chordIdx) {
  return {chordIdx / kNumQualities,
          static_cast<ChordQuality>(chordIdx % kNumQualities)};
}

/// (keyIdx, chordIdx) → flat state index (0–2591).
inline int stateIndex(int keyIdx, int chordIdx) {
  return keyIdx * kNumChords + chordIdx;
}

/// Flat state index (0–2591) → (keyIdx, chordIdx).
inline std::pair<int, int> stateFromIndex(int idx) {
  return {idx / kNumChords, idx % kNumChords};
}

// ─────────────────────────────────────────────────────────────────────────────
// HarmonicState — decoded output of the Viterbi engine
// ─────────────────────────────────────────────────────────────────────────────
struct HarmonicState {
  int keyRoot          = 0;      // 0–11 (0=C, …, 11=B)
  bool keyMinor        = false;  // false=major, true=minor
  int chordRoot        = 0;      // 0–11
  ChordQuality quality = ChordQuality::Major;
  int bassPitchClass   = 0;      // 0–11 (from observation, not state)
  float logProbability = -1e30f; // best-path log-prob (for confidence)

  /// Convert to a flat state index.
  int toStateIndex() const {
    return stateIndex(keyIndex(keyRoot, keyMinor),
                      chordIndex(chordRoot, quality));
  }

  /// Construct from a flat state index.
  static HarmonicState fromStateIndex(int idx) {
    auto [keyIdx, chordIdx] = stateFromIndex(idx);
    auto [root, minor]      = keyFromIndex(keyIdx);
    auto [cRoot, qual]      = chordFromIndex(chordIdx);
    HarmonicState s;
    s.keyRoot  = root;
    s.keyMinor = minor;
    s.chordRoot = cRoot;
    s.quality   = qual;
    return s;
  }
};

// ─────────────────────────────────────────────────────────────────────────────
// Observation — figured-bass decomposition of active MIDI notes
// ─────────────────────────────────────────────────────────────────────────────
struct Observation {
  int bassPitchClass = 0;                     // lowest sounding note % 12
  std::array<float, 12> upperChroma = {};     // weighted pitch-class profile above bass
  double timestampMs = 0.0;                   // wall-clock time of this event
};

// ─────────────────────────────────────────────────────────────────────────────
// Sparse Transition Entry
// ─────────────────────────────────────────────────────────────────────────────
struct TransitionEntry {
  uint16_t targetState;  // target state index
  float logProb;         // log-probability of this transition
};

// ─────────────────────────────────────────────────────────────────────────────
// Human-readable chord quality names
// ─────────────────────────────────────────────────────────────────────────────
inline const char* qualityName(ChordQuality q) {
  switch (q) {
    case ChordQuality::Major:      return "maj";
    case ChordQuality::Minor:      return "min";
    case ChordQuality::Diminished: return "dim";
    case ChordQuality::Augmented:  return "aug";
    case ChordQuality::Dom7:       return "dom7";
    case ChordQuality::Maj7:       return "maj7";
    case ChordQuality::Min7:       return "min7";
    case ChordQuality::Dim7:       return "dim7";
    case ChordQuality::HalfDim7:   return "hdim7";
  }
  return "?";
}

/// Pitch class name (sharps only).
inline const char* pitchClassName(int pc) {
  static constexpr const char* kNames[12] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
  };
  return (pc >= 0 && pc < 12) ? kNames[pc] : "?";
}

} // namespace hmm
} // namespace fiddle
