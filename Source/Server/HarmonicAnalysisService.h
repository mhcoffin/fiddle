#pragma once

#include "HarmonicAnalyzer.h"
#include "HarmonicState.h"
#include "TonalContext.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

#include <juce_core/juce_core.h>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// HarmonicAnalysisService
//
// Bridges the HarmonicAnalyzer (pure Viterbi engine) to the FiddleServer
// runtime. Owns the analyzer, maintains the merged note state across all
// input channels, and publishes results via AtomicTonalContext.
//
// Threading
// ─────────
// • onNoteEvent() is called from the message thread (NoteStreamTracker
//   callbacks). It updates the merged active-note list and signals the
//   worker thread that new data is available.
// • The worker thread waits for the debounce window to close (kDebounceMs),
//   then builds a single observation from all currently active notes and
//   runs one Viterbi forward step.
// • getContext() is lock-free (seqlock read), safe from any thread.
//
// Debouncing
// ──────────
// Dorico sends notes from different instruments sequentially over ~5ms.
// Without debouncing, each individual note-on produces an observation with
// an incomplete chord, causing garbage detection. The debounce window
// (default 30ms) lets all voices of a chord arrive before we observe.
// ─────────────────────────────────────────────────────────────────────────────
class HarmonicAnalysisService : private juce::Thread {
public:
  HarmonicAnalysisService();
  ~HarmonicAnalysisService() override;

  // ── Lifecycle ─────────────────────────────────────────────────────────────

  /// Start the worker thread.
  void start();

  /// Stop the worker thread. Safe to call from any thread.
  void stop();

  // ── Model Loading ─────────────────────────────────────────────────────────

  /// Load transition matrix from binary data.
  bool loadTransitionMatrix(const void* data, size_t bytes);

  /// Load emission templates from binary data.
  bool loadEmissionTemplates(const void* data, size_t bytes);

  /// Build default (music-theory) emission templates.
  void buildDefaultEmissions();

  // ── Note Feed (message thread) ────────────────────────────────────────────

  /// Called on note-on/note-off from NoteStreamTracker.
  /// Updates the active note list and signals the worker thread.
  void onNoteEvent(int noteNumber, bool isNoteOn, int velocity,
                   double timestampMs);

  /// Called on transport stop. Resets all state.
  void onTransportStop();

  // ── Read path (lock-free, any thread) ─────────────────────────────────────

  /// Get the current harmonic context. Lock-free via seqlock.
  TonalContext getContext() const { return cachedContext_.load(); }

  /// Compute per-note fields (is_diatonic, scale_degree) for a specific
  /// MIDI note number, given the current key context.
  static TonalContext annotateNote(TonalContext key, int noteNumber);

  // ── Configuration ─────────────────────────────────────────────────────────

  void setPlaybackDelayMs(double ms) { playbackDelayMs_ = ms; }
  void setBeamWidth(int b) { analyzer_.setBeamWidth(b); }
  void setLag(int n) { analyzer_.setLag(n); }

  /// Update the live BPM (for future duration-based weighting).
  void setBpm(double bpm) {
    currentBpm_.store(bpm > 0.0 ? bpm : 120.0, std::memory_order_relaxed);
  }

  double currentBpmSnapshot() const {
    return currentBpm_.load(std::memory_order_relaxed);
  }

  /// Debounce window in milliseconds. After the first note-on in a burst,
  /// the worker thread waits this long before building an observation.
  /// Default 30ms — enough for Dorico's sequential channel dispatch.
  static constexpr int kDebounceMs = 30;

private:
  // ── juce::Thread ──────────────────────────────────────────────────────────
  void run() override;

  // ── Observation building ──────────────────────────────────────────────────

  /// Rebuild the current Observation from activeNotes_.
  hmm::Observation buildObservation(double timestampMs) const;

  // ── State ─────────────────────────────────────────────────────────────────

  hmm::HarmonicAnalyzer analyzer_;

  // Cached output (lock-free seqlock read).
  AtomicTonalContext cachedContext_;

  // Merged active notes across all channels.
  // Protected by notesMutex_ — only written from message thread,
  // read by worker thread when building observations.
  struct ActiveNote {
    int noteNumber;
    int velocity;
    double startTimeMs;
  };
  std::vector<ActiveNote> activeNotes_;
  mutable std::mutex notesMutex_;

  // Signaling from message thread to worker thread.
  // dirty_ is set by onNoteEvent on note-on; cleared by the worker thread
  // after it builds and processes an observation.
  std::atomic<bool> dirty_{false};

  // Timestamp (hiRes ms) of the most recent note-on that set dirty_.
  std::atomic<double> lastNoteOnTimeMs_{0.0};

  juce::WaitableEvent wakeEvent_;

  double playbackDelayMs_ = 1000.0;
  std::atomic<double> currentBpm_{120.0};

  // For deduped console logging of chord changes.
  int lastLoggedStateIdx_ = -1;
};

} // namespace fiddle
