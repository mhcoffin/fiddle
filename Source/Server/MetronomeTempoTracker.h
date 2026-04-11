#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <chrono>

namespace fiddle {

/**
 * Derives tempo (BPM) and time signature from Dorico's built-in metronome
 * click, which is routed to FiddleNative on a reserved MIDI channel.
 *
 * Dorico sends note 83 on the downbeat and note 79 on other beats (at least
 * in 4/4 time).  On every beat after the first, we compute instantaneous
 * BPM = round(60 / deltaSeconds).  A tempo-change marker is emitted only
 * when the rounded BPM differs from the previous one.
 *
 * All public accessors are safe to call from any thread.
 */
class MetronomeTempoTracker {
public:
  /// Note numbers that Dorico uses for the metronome click.
  int downbeatNote = 83;
  int otherBeatNote = 79;

  /// Callback fired (on the caller's thread) when the rounded BPM changes.
  /// Parameters: bpm, timeSigNumerator, timeSigDenominator, samplePosition
  std::function<void(double bpm, int tsNum, int tsDen, uint64_t samplePos)>
      onTempoChanged;

  // ── Beat history record ───────────────────────────────────────────────────

  struct BeatRecord {
    uint64_t samplePosition = 0;
    int noteNumber = 0;       // 83 (downbeat), 79 (beat), 71 (weak), etc.
  };

  // ── Prominence constants ──────────────────────────────────────────────────
  // Lower value = higher prominence.  Future subdivision levels can be
  // inserted above kOffBeat without changing existing logic.

  static constexpr int kProminenceStrong  = 0;  // downbeat (note 83)
  static constexpr int kProminenceMedium  = 1;  // beat (note 79)
  static constexpr int kProminenceWeak    = 2;  // subdivision (note 71, etc.)
  static constexpr int kProminenceOffBeat = 3;  // not near any click

  /// Snap tolerance in samples. A note whose start_sample falls within
  /// ±kBeatSnapSamples of a beat is assigned that beat's prominence.
  /// ~50 ms at 44100 Hz — generous enough for metronome offset + jitter.
  static constexpr uint64_t kBeatSnapSamples = 2205;

  // ── Feed data ────────────────────────────────────────────────────────────

  /**
   * Call this for every noteOn on the reserved metronome channel.
   * @param noteNumber  MIDI note number (83 = downbeat, 79 = other beat)
   * @param samplePos   Absolute sample position (host_sample_position)
   */
  void onBeat(int noteNumber, uint64_t samplePos) {
    bool isDownbeat = (noteNumber == downbeatNote);

    // Reject events that are too close together (< 50 ms at 44100).
    if (prevBeatSample_ > 0 && samplePos <= prevBeatSample_) return;
    if (prevBeatSample_ > 0 && (samplePos - prevBeatSample_) < kMinBeatSamples)
      return;

    ++beatCount_;

    // ── Record beat in history ring buffer ──────────────────────────────
    {
      beatHistory_[beatHistoryHead_] = { samplePos, noteNumber };
      beatHistoryHead_ = (beatHistoryHead_ + 1) % kBeatHistorySize;
      if (beatHistoryCount_ < kBeatHistorySize)
        ++beatHistoryCount_;
    }

    // Track downbeats for time signature detection
    if (isDownbeat) {
      if (lastDownbeatBeatNum_ > 0) {
        int beatsInBar =
            static_cast<int>(beatCount_ - lastDownbeatBeatNum_);
        if (beatsInBar >= 1 && beatsInBar <= 32) {
          currentTsNum_.store(beatsInBar, std::memory_order_relaxed);
        }
      }
      lastDownbeatBeatNum_ = beatCount_;
    }

    // std::cerr << "[Metronome] Beat #" << beatCount_
    //           << " note=" << noteNumber
    //           << " sample=" << samplePos
    //           << (isDownbeat ? " (DOWNBEAT)" : "")
    //           << std::endl;

    // Need a previous beat to compute BPM
    if (prevBeatSample_ > 0) {
      uint64_t delta = samplePos - prevBeatSample_;
      double deltaSec = static_cast<double>(delta) / sampleRate_;
      double instantBpm = 60.0 / deltaSec;
      int roundedBpm = static_cast<int>(std::round(instantBpm));

      // Update the live BPM
      currentBpm_.store(instantBpm, std::memory_order_relaxed);

      // std::cerr << "[Metronome] BPM=" << instantBpm
      //           << " rounded=" << roundedBpm
      //           << " prev_rounded=" << lastEmittedRoundedBpm_
      //           << std::endl;

      // Skip the first interval after reset.  Dorico frequently
      // shortens the initial beat on playback start, producing a
      // spurious BPM that would override the host's transport tempo.
      // Record the rounded value for deduplication but don't emit.
      if (beatCount_ == 2) {
        lastEmittedRoundedBpm_ = roundedBpm;
      }
      // Emit a tempo change only when the rounded BPM differs
      else if (roundedBpm != lastEmittedRoundedBpm_) {
        lastEmittedRoundedBpm_ = roundedBpm;

        int tsNum = currentTsNum_.load(std::memory_order_relaxed);
        int tsDen = currentTsDen_.load(std::memory_order_relaxed);

        // std::cerr << "[Metronome] Tempo change: " << roundedBpm
        //           << " BPM, " << tsNum << "/" << tsDen
        //           << " at sample " << prevBeatSample_ << std::endl;

        if (onTempoChanged) {
          // Place the marker at the START of this interval
          // (i.e., the previous beat's position).
          onTempoChanged(static_cast<double>(roundedBpm),
                         tsNum, tsDen, prevBeatSample_);
        }
      }
    }

    prevBeatSample_ = samplePos;

    // Track last beat time for hasRecentBeats()
    lastBeatTimeMs_.store(
        static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count()),
        std::memory_order_relaxed);
  }

  // ── Queries ──────────────────────────────────────────────────────────────

  /// Current computed BPM (0 if not yet determined).
  double getCurrentBpm() const {
    return currentBpm_.load(std::memory_order_relaxed);
  }

  /// Current time signature numerator (e.g. 4 for 4/4).
  int getTimeSigNumerator() const {
    return currentTsNum_.load(std::memory_order_relaxed);
  }

  /// Current time signature denominator (user-configurable, default 4).
  int getTimeSigDenominator() const {
    return currentTsDen_.load(std::memory_order_relaxed);
  }

  /// Set the denominator (not derivable from click alone).
  void setTimeSigDenominator(int den) {
    if (den > 0)
      currentTsDen_.store(den, std::memory_order_relaxed);
  }

  /// True if a beat was received within the last N milliseconds.
  bool hasRecentBeats(uint64_t thresholdMs = 3000) const {
    auto nowMs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
    uint64_t lastMs = lastBeatTimeMs_.load(std::memory_order_relaxed);
    return lastMs > 0 && (nowMs - lastMs) < thresholdMs;
  }

  /// Total beats received (monotonically increasing).
  uint64_t getBeatCount() const { return beatCount_; }

  /// Set the sample rate for BPM computation (default 44100).
  void setSampleRate(double sr) {
    if (sr > 0.0) sampleRate_ = sr;
  }

  /// Reset all state (e.g. on transport stop).
  void reset() {
    beatCount_ = 0;
    prevBeatSample_ = 0;
    lastEmittedRoundedBpm_ = 0;
    lastDownbeatBeatNum_ = 0;
    beatHistoryCount_ = 0;
    beatHistoryHead_ = 0;
    // Don't reset currentBpm_ — keep the last known value as a reference
  }

  /**
   * Return the rhythmic prominence for a given sample position by finding
   * the nearest beat in the history within ±kBeatSnapSamples.
   *
   * @return 0 = strong (downbeat), 1 = medium, 2 = weak, 3 = off-beat.
   */
  int getProminenceAt(uint64_t samplePos) const {
    if (beatHistoryCount_ == 0)
      return kProminenceOffBeat;

    uint64_t bestDist = UINT64_MAX;
    int bestNote = 0;

    for (size_t i = 0; i < beatHistoryCount_; ++i) {
      const auto& rec = beatHistory_[i];
      uint64_t dist = (samplePos >= rec.samplePosition)
                          ? (samplePos - rec.samplePosition)
                          : (rec.samplePosition - samplePos);
      if (dist < bestDist) {
        bestDist = dist;
        bestNote = rec.noteNumber;
      }
    }

    if (bestDist > kBeatSnapSamples)
      return kProminenceOffBeat;

    // Map Dorico note numbers to prominence levels
    if (bestNote == 83) return kProminenceStrong;
    if (bestNote == 79) return kProminenceMedium;
    return kProminenceWeak; // note 71 or any other subdivision
  }

private:
  /// Minimum inter-beat interval in samples (prevents double-triggers).
  /// ~50 ms at 44100 Hz.
  static constexpr uint64_t kMinBeatSamples = 2205;

  /// Beat history ring buffer for prominence lookups.
  static constexpr size_t kBeatHistorySize = 256;
  std::array<BeatRecord, kBeatHistorySize> beatHistory_{};
  size_t beatHistoryHead_ = 0;
  size_t beatHistoryCount_ = 0;

  double sampleRate_ = 44100.0;

  uint64_t beatCount_ = 0;
  uint64_t prevBeatSample_ = 0;
  int lastEmittedRoundedBpm_ = 0;
  uint64_t lastDownbeatBeatNum_ = 0;

  std::atomic<double> currentBpm_{0.0};
  std::atomic<int> currentTsNum_{4};
  std::atomic<int> currentTsDen_{4};
  std::atomic<uint64_t> lastBeatTimeMs_{0};
};

} // namespace fiddle

