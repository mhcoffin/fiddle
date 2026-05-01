#pragma once

#include "KeyProfile.h"
#include "TonalContext.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_events/juce_events.h>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// TonalCenterClassifier
//
// Real-time tonal center detector using the Krumhansl-Schmuckler algorithm
// with Temperley's European Classical profiles.
//
// Architecture
// ────────────
// • Notes are fed in via enqueueNote() as they enter the delay pipeline
//   (in routeAnnotatedNoteOn). Each note carries a pitch class, a duration
//   weight (beats), and its scheduled play time (now + playbackDelay).
// • A juce::Timer (50ms interval) runs Pearson correlation across the 24
//   key templates using an EWMA-weighted pitch-class histogram. The winning
//   key is atomically written to AtomicTonalContext.
// • Note routing reads the cached context lock-free and stamps per-note
//   fields (is_diatonic, scale_degree) via annotateNote().
//
// Thread Safety
// ─────────────
// • enqueueNote / removeNote: protected by bufferMutex_ (called from the
//   MIDI-in thread / message thread via routeAnnotatedNoteOn).
// • timerCallback (classifier thread / JUCE timer): acquires bufferMutex_
//   briefly to snapshot the buffer, then does all heavy math outside the lock.
// • getContext / annotateNote: lock-free via AtomicTonalContext seqlock.
// ─────────────────────────────────────────────────────────────────────────────
class TonalCenterClassifier : private juce::Timer {
public:
  /// Construct with optional profile (defaults to TemperleyProfile).
  explicit TonalCenterClassifier(
      std::unique_ptr<IKeyProfile> profile = std::make_unique<TemperleyProfile>());

  ~TonalCenterClassifier() override;

  // ── Lifecycle ─────────────────────────────────────────────────────────────

  /// Start the background correlation timer (50ms interval).
  void start();

  /// Stop the timer. Safe to call from any thread.
  void stop();

  // ── Note feed (MIDI-in / message thread) ──────────────────────────────────

  /// Register a note entering the delay buffer.
  /// @param pitchClass  MIDI pitch class 0–11 (note_number % 12)
  /// @param durationBeats  Note duration in quarter-note beats (may be
  ///                       estimated as 0.5 if unknown at note-on time)
  /// @param scheduledTimeMs  Absolute wall-clock ms when the note will play
  ///                         (= now + playbackDelayMs)
  void enqueueNote(int pitchClass, float durationBeats, double scheduledTimeMs);

  /// Remove a note from the histogram when a note-off is processed.
  /// Uses the same pitchClass + durationBeats as the matching enqueueNote call.
  void removeNote(int pitchClass, float durationBeats);

  // ── BPM (set from MainComponent when TempoEvent arrives) ──────────────────

  /// Update the live BPM used for duration-beat weighting.
  void setBpm(double bpm) {
    currentBpm_.store(bpm > 0.0 ? bpm : 120.0, std::memory_order_relaxed);
  }

  /// Read the current BPM (lock-free, for use in routeAnnotatedNoteOn).
  double currentBpmSnapshot() const {
    return currentBpm_.load(std::memory_order_relaxed);
  }

  // ── Read path (lock-free, called from routeAnnotatedNoteOn) ───────────────

  /// Get the current cached tonal context (key-level fields only).
  TonalContext getContext() const { return cached_.load(); }

  /// Compute per-note fields (is_diatonic, scale_degree) for a given MIDI
  /// note number and key context, returning a fully-populated TonalContext.
  static TonalContext annotateNote(TonalContext key, int noteNumber);

  // ── Config ────────────────────────────────────────────────────────────────

  /// EWMA time constant (ms). Older notes decay with e^(-dt/tau).
  /// Default 4000ms means a note played 4 seconds ago has ~37% weight.
  void setDecayTauMs(double tau) { decayTauMs_ = tau; }

  /// Hysteresis window (ms). New key must 'win' for this long before
  /// the cached context commits to the new key.
  void setHysteresisMs(double ms) { hysteresisMs_ = ms; }

  /// Hysteresis confidence margin. A new key immediately wins if its
  /// Pearson r exceeds the current key's r by at least this margin.
  void setHysteresisMargin(float margin) { hysteresisMargin_ = margin; }

private:
  // ── juce::Timer ───────────────────────────────────────────────────────────
  void timerCallback() override;

  // ── Internal helpers ──────────────────────────────────────────────────────

  /// Snapshot the EWMA histogram under lock, apply decay, return copy.
  std::array<float, 12> snapshotHistogram();

  /// Run Pearson correlation against all 24 templates.
  /// Returns (winnerRoot, winnerIsMinor, winnerR).
  struct KeyCandidate { int root; bool minor; float r; };
  KeyCandidate runCorrelation(const std::array<float, 12> &histogram) const;

  /// Check hysteresis and commit the new winner if appropriate.
  void updateHysteresis(KeyCandidate winner);

  // ── Pre-normalized templates (built at construction) ─────────────────────
  // 24 templates (12 major + 12 minor), each zero-mean and unit-variance.
  struct Template {
    int root;
    bool minor;
    std::array<float, 12> weights; // Pearson-normalized
  };
  std::vector<Template> templates_;

  void buildTemplates();

  // ── State ─────────────────────────────────────────────────────────────────

  std::unique_ptr<IKeyProfile> profile_;

  // EWMA pitch-class histogram. Protected by bufferMutex_.
  std::array<float, 12> histogram_{};
  double lastUpdateTimeMs_ = -1.0; // wall-clock ms of last histogram update
  double decayTauMs_ = 4000.0;

  mutable std::mutex bufferMutex_;

  // Cached output (lock-free read).
  AtomicTonalContext cached_;

  // Hysteresis state.
  KeyCandidate currentWinner_{0, false, 0.f};
  KeyCandidate pendingWinner_{0, false, 0.f};
  double pendingFirstSeenMs_ = -1.0;
  double hysteresisMs_       = 800.0;
  float  hysteresisMargin_   = 0.15f;

  // Live BPM (written by MainComponent, read by enqueueNote for duration weight).
  std::atomic<double> currentBpm_{120.0};
};

} // namespace fiddle
