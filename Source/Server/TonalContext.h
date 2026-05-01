#pragma once

#include <atomic>
#include <cstdint>
#include <cstring>

namespace fiddle {

// ─────────────────────────────────────────────────────────────────────────────
// TonalContext
//
// Tonal metadata computed by TonalCenterClassifier and passed to Lua Fiddles
// via the `ctx.tonal_context` table.
//
// Fields are set in two stages:
//   1. "Key-level" fields (current_key_root, is_minor, confidence) are
//      updated by the classifier's background timer thread every ~50ms.
//   2. "Note-level" fields (is_diatonic, scale_degree) are computed per-note
//      by TonalCenterClassifier::annotateNote() in routeAnnotatedNoteOn().
// ─────────────────────────────────────────────────────────────────────────────
struct TonalContext {
  int   current_key_root = 0;   // 0=C … 11=B
  bool  is_minor         = false;
  float confidence       = 0.f; // Pearson r ∈ [−1, 1] (legacy) or HMM log-prob

  // Chord-level fields — populated by HarmonicAnalysisService.
  int   chord_root       = 0;   // 0–11
  int   chord_quality    = 0;   // ChordQuality enum value (0–8)
  int   bass_pitch_class = 0;   // 0–11 (lowest sounding pitch class)

  // Per-note fields — populated by annotateNote() before Lua sees the note.
  bool is_diatonic  = false;
  int  scale_degree = 0; // 1–7 if diatonic, else 0
};

// ─────────────────────────────────────────────────────────────────────────────
// AtomicTonalContext
//
// Lock-free seqlock wrapper for TonalContext.
//
// WRITE (classifier timer thread):
//   1. Increment sequence (odd → writer active).
//   2. Copy payload.
//   3. Increment sequence again (even → stable).
//
// READ (note routing / audio path, once per note):
//   Loop:
//     1. Read sequence — retry if odd (write in progress).
//     2. Copy payload.
//     3. Re-read sequence — retry if changed.
//
// The payload is small (~32 bytes with chord fields),
// so the copy is a handful of stores/loads — no allocation.
// ─────────────────────────────────────────────────────────────────────────────
class AtomicTonalContext {
public:
  AtomicTonalContext() = default;

  /// Write a new snapshot (classifier thread only).
  void store(const TonalContext &ctx) noexcept {
    // Begin write: make sequence odd.
    uint64_t seq = seq_.load(std::memory_order_relaxed);
    seq_.store(seq + 1, std::memory_order_release);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    std::memcpy(&payload_, &ctx, sizeof(TonalContext));

    std::atomic_thread_fence(std::memory_order_seq_cst);
    seq_.store(seq + 2, std::memory_order_release);
  }

  /// Read the current snapshot (any thread, including audio path).
  TonalContext load() const noexcept {
    TonalContext ctx;
    uint64_t seq0, seq1;
    do {
      seq0 = seq_.load(std::memory_order_acquire);
      if (seq0 & 1U) continue; // writer active — spin
      std::memcpy(&ctx, &payload_, sizeof(TonalContext));
      std::atomic_thread_fence(std::memory_order_acquire);
      seq1 = seq_.load(std::memory_order_acquire);
    } while (seq0 != seq1);
    return ctx;
  }

private:
  std::atomic<uint64_t> seq_{0};
  // Payload is NOT declared atomic — raw memcpy protected by the seqlock.
  // Alignment matches TonalContext's natural alignment (int = 4).
  alignas(TonalContext) char payload_[sizeof(TonalContext)] = {};
};

} // namespace fiddle
