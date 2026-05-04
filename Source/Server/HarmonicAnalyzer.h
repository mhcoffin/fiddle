#pragma once

#include "HarmonicState.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace fiddle {
namespace hmm {

// ─────────────────────────────────────────────────────────────────────────────
// HarmonicAnalyzer
//
// Beam-pruned truncated Viterbi engine for joint key/chord detection.
//
// Usage:
//   1. Load transition matrix and emission templates from binary assets.
//   2. On each MIDI note-on/note-off, call observeEvent() with the current
//      figured-bass observation.
//   3. After each observeEvent(), call commitDecision() to get the decided
//      harmonic state (lagged by N events).
//   4. Call reset() on transport stop.
//
// Threading: NOT thread-safe. The caller (HarmonicAnalysisService) is
// responsible for serializing access via a worker thread.
// ─────────────────────────────────────────────────────────────────────────────
class HarmonicAnalyzer {
public:
  HarmonicAnalyzer();
  ~HarmonicAnalyzer() = default;

  // ── Model Loading ────────────────────────────────────────────────────────

  /// Load sparse transition matrix from binary data.
  ///
  /// Format: [uint32 numStates]
  ///         Per state: [uint16 K] [K × (uint16 targetState, float32 logProb)]
  ///
  /// Returns true on success.
  bool loadTransitionMatrix(const void* data, size_t bytes);

  /// Load emission templates from binary data.
  ///
  /// Format: row-major float32, kNumChords × 13.
  /// Each row: [bass_pc_weight_0..11, upper_chroma_weight_0..11]
  /// Wait — that's 24, not 13. Let me reconsider.
  ///
  /// Actually the emission template per chord is:
  ///   [12 bass-pc log-probabilities] + [12 upper-chroma expected weights]
  /// = 24 floats per chord.
  ///
  /// Format: row-major float32, kNumChords × 24.
  bool loadEmissionTemplates(const void* data, size_t bytes);

  /// Load key-degree emission priors: log P(degree, quality | mode).
  /// Binary format: uint32×3 header (modes, degrees, qualities) +
  /// modes×degrees×qualities × float32 log-priors.
  bool loadDegreePriors(const void* data, size_t bytes);

  /// Build default emission templates from music theory (no training data).
  /// Useful for testing and as a bootstrapping step.
  void buildDefaultEmissionTemplates();

  // ── Runtime ──────────────────────────────────────────────────────────────

  /// Process a new observation (note event). Performs one Viterbi forward step.
  void observeEvent(const Observation& obs);

  /// Retrieve the decided harmonic state (lagged by N events from the current).
  /// Returns a default state if not enough events have been observed yet.
  HarmonicState commitDecision() const;

  /// Get the most recent (non-lagged) best state. Useful for UI display
  /// where lag tolerance is acceptable.
  HarmonicState currentBestState() const;

  /// Reset all state (transport stop). Clears ring buffer and beam.
  void reset();

  /// Check whether the time bound requires an early commit.
  /// Called by the service layer with the playback delay budget.
  bool shouldForceCommit(double currentTimeMs, double playbackDelayMs) const;

  // ── Configuration ────────────────────────────────────────────────────────

  void setBeamWidth(int b)   { beamWidth_ = std::clamp(b, 1, kNumStates); }
  void setLag(int n)         { lag_ = std::max(1, n); }
  int  beamWidth() const     { return beamWidth_; }
  int  lag() const           { return lag_; }
  int  eventCount() const    { return eventCount_; }
  bool hasTransitions() const { return transitionsLoaded_; }

private:
  // ── Beam state at one time step ──────────────────────────────────────────
  struct BeamEntry {
    int stateIdx   = 0;
    float logProb  = -1e30f;
    int backpointer = -1;  // index into previous step's beam
  };

  // ── Ring buffer step ─────────────────────────────────────────────────────
  struct Step {
    std::vector<BeamEntry> beam;
    Observation obs;
    double timestampMs = 0.0;
  };

  // ── Forward step ─────────────────────────────────────────────────────────

  /// Compute log-emission probability for a given chord index and observation.
  float computeEmission(int chordIdx, const Observation& obs) const;

  /// Look up log P(chord_degree, quality | key_mode) for key disambiguation.
  float degreePrior(int keyIdx, int chordIdx) const;

  /// Perform one Viterbi forward step: expand, score, prune.
  void forwardStep(const Observation& obs);

  /// Select the top-B entries from candidates.
  static void pruneBeam(std::vector<BeamEntry>& candidates, int maxBeam);

  // ── State ────────────────────────────────────────────────────────────────

  int beamWidth_ = kDefaultBeamWidth;
  int lag_       = kDefaultLag;
  int eventCount_ = 0;

  // Ring buffer of steps. Size = lag_ + 1 (current + N history).
  std::vector<Step> ringBuffer_;
  int ringHead_ = 0;  // index of most recent step

  // Sparse transition matrix: transitions_[srcState] = vector of (target, logP).
  std::vector<std::vector<TransitionEntry>> transitions_;

  // Emission templates: emissionBass_[chordIdx][pc] = log P(bass=pc | chord).
  //                     emissionUpper_[chordIdx][pc] = expected chroma weight.
  std::array<std::array<float, 12>, kNumChords> emissionBass_;
  std::array<std::array<float, 12>, kNumChords> emissionUpper_;

  // Degree priors: degreePriors_[mode][degree * kNumQualities + quality]
  // mode: 0=major, 1=minor. degree: 0-11. quality: 0-8.
  std::array<std::array<float, 12 * kNumQualities>, 2> degreePriors_;

  bool transitionsLoaded_ = false;
  bool emissionsLoaded_   = false;
  bool degreePriorsLoaded_ = false;

  // Workspace to avoid per-event allocation.
  std::vector<BeamEntry> candidates_;
};

} // namespace hmm
} // namespace fiddle
