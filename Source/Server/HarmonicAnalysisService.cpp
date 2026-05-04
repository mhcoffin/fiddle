#include "HarmonicAnalysisService.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

HarmonicAnalysisService::HarmonicAnalysisService()
    : juce::Thread("HarmonicAnalysis") {
  activeNotes_.reserve(64);
}

HarmonicAnalysisService::~HarmonicAnalysisService() {
  stop();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalysisService::start() {
  startThread(juce::Thread::Priority::normal);
}

void HarmonicAnalysisService::stop() {
  signalThreadShouldExit();
  wakeEvent_.signal();
  stopThread(2000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Model Loading
// ─────────────────────────────────────────────────────────────────────────────

bool HarmonicAnalysisService::loadTransitionMatrix(const void* data,
                                                    size_t bytes) {
  return analyzer_.loadTransitionMatrix(data, bytes);
}

bool HarmonicAnalysisService::loadEmissionTemplates(const void* data,
                                                     size_t bytes) {
  return analyzer_.loadEmissionTemplates(data, bytes);
}

bool HarmonicAnalysisService::loadDegreePriors(const void* data,
                                                size_t bytes) {
  return analyzer_.loadDegreePriors(data, bytes);
}

void HarmonicAnalysisService::buildDefaultEmissions() {
  analyzer_.buildDefaultEmissionTemplates();
}

// ─────────────────────────────────────────────────────────────────────────────
// Note Feed (message thread)
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalysisService::onNoteEvent(int noteNumber, bool isNoteOn,
                                           int velocity, double timestampMs) {
  // Update active notes regardless of on/off.
  {
    std::lock_guard<std::mutex> lock(notesMutex_);
    if (isNoteOn && velocity > 0) {
      activeNotes_.push_back({noteNumber, velocity, timestampMs});
    } else {
      // Remove the first matching note (FIFO).
      for (auto it = activeNotes_.begin(); it != activeNotes_.end(); ++it) {
        if (it->noteNumber == noteNumber) {
          activeNotes_.erase(it);
          break;
        }
      }
    }
  }

  // Only signal the worker thread on note-on events.
  // Note-offs update the active note set but don't trigger analysis —
  // the next note-on will capture the correct state.
  if (isNoteOn && velocity > 0) {
    lastNoteOnTimeMs_.store(timestampMs, std::memory_order_relaxed);
    dirty_.store(true, std::memory_order_release);
    wakeEvent_.signal();
  }
}

void HarmonicAnalysisService::onTransportStop() {
  {
    std::lock_guard<std::mutex> lock(notesMutex_);
    activeNotes_.clear();
  }

  dirty_.store(false, std::memory_order_relaxed);
  lastNoteOnTimeMs_.store(0.0, std::memory_order_relaxed);

  TonalContext emptyCtx;
  cachedContext_.store(emptyCtx);

  analyzer_.reset();
  lastLoggedStateIdx_ = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Observation Building
// ─────────────────────────────────────────────────────────────────────────────

hmm::Observation HarmonicAnalysisService::buildObservation(
    double timestampMs) const {
  hmm::Observation obs;
  obs.timestampMs = timestampMs;
  obs.upperChroma.fill(0.f);

  std::lock_guard<std::mutex> lock(notesMutex_);

  if (activeNotes_.empty()) {
    obs.bassPitchClass = 0;
    return obs;
  }

  // Find the lowest note (bass) and build upper chroma.
  int lowestNote = 128;
  for (const auto& n : activeNotes_) {
    if (n.noteNumber < lowestNote)
      lowestNote = n.noteNumber;
  }

  obs.bassPitchClass = lowestNote % 12;

  // Build chroma vector from all notes, weighted by velocity.
  for (const auto& n : activeNotes_) {
    int pc = n.noteNumber % 12;
    float weight = static_cast<float>(n.velocity) / 127.f;
    obs.upperChroma[pc] += weight;
  }

  return obs;
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker Thread
//
// Strategy: when dirty_ is set (at least one note-on arrived), wait for the
// debounce window to close (kDebounceMs since the last note-on), then build
// a single observation from all currently active notes and run one Viterbi
// step. This batches the 4–20 note-ons that arrive in a chord burst into
// one clean observation instead of 4–20 noisy partial observations.
// ─────────────────────────────────────────────────────────────────────────────

void HarmonicAnalysisService::run() {
  while (!threadShouldExit()) {
    // Sleep until signaled or timeout for periodic force-commit checks.
    wakeEvent_.wait(100);

    if (!dirty_.load(std::memory_order_acquire))
      continue;

    // ── Debounce: wait until kDebounceMs after the last note-on ──────────
    // This ensures all voices in a chord burst have arrived before we
    // build the observation.
    double lastNoteOn = lastNoteOnTimeMs_.load(std::memory_order_relaxed);
    double now = juce::Time::getMillisecondCounterHiRes();
    double elapsed = now - lastNoteOn;

    if (elapsed < kDebounceMs) {
      // Sleep for the remaining debounce time, then re-check.
      int remainMs = static_cast<int>(kDebounceMs - elapsed) + 1;
      juce::Thread::sleep(remainMs);

      // After sleeping, check if more notes arrived (extending the burst).
      // Re-read the timestamp — if it moved, another note arrived during
      // our sleep, so wait again.
      double updatedLastNoteOn = lastNoteOnTimeMs_.load(std::memory_order_relaxed);
      if (updatedLastNoteOn > lastNoteOn) {
        // More notes arrived — loop back and debounce again.
        continue;
      }
    }

    // ── Debounce window closed: build observation and process ─────────────
    dirty_.store(false, std::memory_order_relaxed);

    now = juce::Time::getMillisecondCounterHiRes();
    hmm::Observation obs = buildObservation(now);

    // Only process if there are actually active notes.
    int noteCount = 0;
    {
      std::lock_guard<std::mutex> lock(notesMutex_);
      noteCount = static_cast<int>(activeNotes_.size());
    }

    if (noteCount == 0)
      continue;

    // DEBUG: log the observation
    {
      std::ostringstream oss;
      oss << "[HMM-Obs] bass=" << hmm::pitchClassName(obs.bassPitchClass) << " chroma=[";
      for (int pc = 0; pc < 12; ++pc) {
        if (obs.upperChroma[pc] > 0.01f)
          oss << hmm::pitchClassName(pc) << ":" << obs.upperChroma[pc] << " ";
      }
      oss << "] notes=" << noteCount;
      std::cerr << oss.str() << std::endl;
    }

    analyzer_.observeEvent(obs);

    // Use currentBestState() — the emission model is key-agnostic (C:Cmaj
    // and Am:Cmaj score identically), so Viterbi backtrace picks wrong
    // ancestor keys. Without key-aware emissions, lag makes things worse.
    // TODO: add key-aware emission scoring, then re-enable lag.
    hmm::HarmonicState state = analyzer_.currentBestState();

    TonalContext ctx;
    ctx.current_key_root = state.keyRoot;
    ctx.is_minor         = state.keyMinor;
    ctx.confidence       = state.logProbability;
    ctx.chord_root       = state.chordRoot;
    ctx.chord_quality    = static_cast<int>(state.quality);
    ctx.bass_pitch_class = state.bassPitchClass;

    cachedContext_.store(ctx);

    // Log chord changes to console (debug aid).
    int newStateIdx = state.toStateIndex();
    if (newStateIdx != lastLoggedStateIdx_) {
      lastLoggedStateIdx_ = newStateIdx;
      std::cerr << "[Harmony] Key: " << hmm::pitchClassName(state.keyRoot)
                << (state.keyMinor ? "m" : "")
                << " | Chord: " << hmm::pitchClassName(state.chordRoot)
                << hmm::qualityName(state.quality)
                << " | Bass: " << hmm::pitchClassName(state.bassPitchClass)
                << " | notes=" << noteCount
                << " events=" << analyzer_.eventCount()
                << " logP=" << state.logProbability
                << std::endl;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-note annotation (static, lock-free)
// ─────────────────────────────────────────────────────────────────────────────

// static
TonalContext HarmonicAnalysisService::annotateNote(TonalContext key,
                                                    int noteNumber) {
  int pc = noteNumber % 12;
  int degree = ((pc - key.current_key_root) % 12 + 12) % 12;

  // Diatonic scale degrees (0-based semitones from tonic).
  // Major: W W H W W W H  → 0 2 4 5 7 9 11
  // Natural minor: W H W W H W W → 0 2 3 5 7 8 10
  static constexpr int kMajorDegrees[7] = {0, 2, 4, 5, 7, 9, 11};
  static constexpr int kMinorDegrees[7] = {0, 2, 3, 5, 7, 8, 10};

  const int* degrees = key.is_minor ? kMinorDegrees : kMajorDegrees;

  key.is_diatonic  = false;
  key.scale_degree = 0;

  for (int i = 0; i < 7; ++i) {
    if (degrees[i] == degree) {
      key.is_diatonic  = true;
      key.scale_degree = i + 1; // 1-based
      break;
    }
  }

  return key;
}

} // namespace fiddle
