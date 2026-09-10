#pragma once

#include "RealtimeSpscQueue.h"
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace fiddle {

// Single audio-thread writer. The UI only reads packets from the bounded queue.
// Times are monotonic milliseconds, supplied by the caller for deterministic tests.
class AudioRenderDiagnostics {
  static_assert(std::atomic<size_t>::is_always_lock_free);
public:
  struct Snapshot {
    double sampleRate = 0, load = 0, peakLoad = 0, maxLoad = 0;
    double maxRenderMs = 0, maxGapMs = 0, lastOverrunMs = 0, timestampMs = 0;
    double pluginLoad = 0, otherLoad = 0, recentMaxGapMs = 0;
    double lastLongGapMs = 0, renderBeforeLongGapMs = 0;
    uint64_t callbacks = 0, overruns = 0, longGaps = 0;
    uint64_t ringOverflows = 0, unavailableBlocks = 0, droppedReports = 0;
    int blockSize = 0, minBlockSize = 0, maxBlockSize = 0;
  };

  // Device lifecycle calls are serialized with the audio callback by JUCE.
  void prepare(double rate) noexcept {
    state_.sampleRate = rate;
    previousStart_ = -1;
    workMs_ = budgetMs_ = peak_ = pluginMs_ = recentGap_ = 0;
    previousRenderMs_ = 0;
  }

  void record(double startMs, double endMs, int frames,
              bool overflow = false, bool unavailable = false,
              double pluginWorkMs = 0) noexcept {
    if (frames <= 0 || state_.sampleRate <= 0) return;
    const double budget = 1000.0 * frames / state_.sampleRate;
    const double elapsed = std::max(0.0, endMs - startMs);
    const double load = elapsed / budget;
    if (previousStart_ >= 0) {
      const double gap = startMs - previousStart_;
      state_.maxGapMs = std::max(state_.maxGapMs, gap);
      recentGap_ = std::max(recentGap_, gap);
      if (gap > previousBudget_ * 1.5) {
        ++state_.longGaps;
        state_.lastLongGapMs = gap;
        state_.renderBeforeLongGapMs = previousRenderMs_;
      }
    }
    previousStart_ = startMs;
    previousBudget_ = budget;
    previousRenderMs_ = elapsed;
    ++state_.callbacks;
    if (elapsed > budget) {
      ++state_.overruns;
      state_.lastOverrunMs = endMs;
    }
    state_.ringOverflows += overflow;
    state_.unavailableBlocks += unavailable;
    state_.blockSize = frames;
    state_.minBlockSize = state_.minBlockSize == 0 ? frames : std::min(state_.minBlockSize, frames);
    state_.maxBlockSize = std::max(state_.maxBlockSize, frames);
    state_.maxLoad = std::max(state_.maxLoad, load);
    state_.maxRenderMs = std::max(state_.maxRenderMs, elapsed);
    state_.timestampMs = endMs;
    workMs_ += elapsed;
    pluginMs_ += std::clamp(pluginWorkMs, 0.0, elapsed);
    budgetMs_ += budget;
    peak_ = std::max(peak_, load);
    if (budgetMs_ >= 250.0) publish();
  }

  void publish() noexcept {
    if (budgetMs_ <= 0) return;
    state_.load = workMs_ / budgetMs_;
    state_.peakLoad = peak_;
    state_.pluginLoad = pluginMs_ / budgetMs_;
    state_.otherLoad = (workMs_ - pluginMs_) / budgetMs_;
    state_.recentMaxGapMs = recentGap_;
    if (!reports_.tryPush(state_)) ++state_.droppedReports;
    workMs_ = budgetMs_ = peak_ = pluginMs_ = recentGap_ = 0;
  }

  bool takeLatest(Snapshot &result) noexcept {
    bool found = false;
    // Bounded even if the producer continues to run during this read.
    for (size_t i = 0; i < reports_.usableCapacity(); ++i) {
      if (!reports_.tryPop(result)) break;
      found = true;
    }
    return found;
  }

private:
  Snapshot state_;
  double previousStart_ = -1, previousBudget_ = 0;
  double workMs_ = 0, budgetMs_ = 0, peak_ = 0;
  double pluginMs_ = 0, recentGap_ = 0, previousRenderMs_ = 0;
  RealtimeSpscQueue<Snapshot, 257> reports_;
};

// One recorder per hosted runtime, one audio writer and one message-thread reader.
// No plugin calls, allocations or locks. Windows are measured in processed audio
// time, not UI time; missing/bypassed processors therefore become stale naturally.
class PluginRenderDiagnostics {
public:
  struct Snapshot {
    double averageMs = 0, peakMs = 0, maxMs = 0, load = 0, timestampMs = 0;
    double sampleRate = 0;
    int blockSize = 0;
    uint64_t calls = 0, droppedReports = 0;
  };
  void prepare(double rate) noexcept {
    rate_ = rate;
    work_ = budget_ = peak_ = 0;
    windowCalls_ = 0;
  }
  void record(double start, double end, int frames) noexcept {
    if (rate_ <= 0 || frames <= 0) return;
    const auto elapsed = std::max(0.0, end - start);
    work_ += elapsed;
    budget_ += 1000.0 * frames / rate_;
    peak_ = std::max(peak_, elapsed);
    state_.maxMs = std::max(state_.maxMs, elapsed);
    state_.timestampMs = end;
    state_.sampleRate = rate_;
    state_.blockSize = frames;
    ++state_.calls;
    ++windowCalls_;
    if (budget_ >= 250.0) publish();
  }
  void publish() noexcept {
    if (windowCalls_ == 0) return;
    state_.averageMs = work_ / windowCalls_;
    state_.peakMs = peak_;
    state_.load = work_ / budget_;
    if (!reports_.tryPush(state_)) ++state_.droppedReports;
    work_ = budget_ = peak_ = 0;
    windowCalls_ = 0;
  }
  bool takeLatest(Snapshot &result) noexcept {
    bool found = false;
    for (size_t i = 0; i < reports_.usableCapacity(); ++i) {
      if (!reports_.tryPop(result)) break;
      found = true;
    }
    return found;
  }
  // Callback-local sum for separating plugin calls from the rest of Fiddle.
  // The top-level render resets this before invoking any hosted processors.
  static void beginBlock() noexcept { blockWorkMs_ = 0; }
  static void addBlockWork(double ms) noexcept { blockWorkMs_ += std::max(0.0, ms); }
  static double blockWorkMs() noexcept { return blockWorkMs_; }
private:
  inline static thread_local double blockWorkMs_ = 0;
  double rate_ = 0, work_ = 0, budget_ = 0, peak_ = 0;
  uint64_t windowCalls_ = 0;
  Snapshot state_;
  RealtimeSpscQueue<Snapshot, 33> reports_;
};

// Independent relaxed counters: snapshots are approximate across fields, but
// each counter is monotonic. No locks, allocation, clocks or serialization here.
class AudioReturnDiagnostics {
public:
  enum class Result { rendered, buffering, underrun, unavailable };
  struct Snapshot {
    uint64_t callbacks, underruns, bufferingFrames, unavailableFrames;
  };
  void record(Result result, int frames) noexcept {
    if (frames <= 0) return;
    callbacks_.fetch_add(1, std::memory_order_relaxed);
    if (result == Result::underrun) underruns_.fetch_add(1, std::memory_order_relaxed);
    if (result == Result::buffering || result == Result::underrun)
      bufferingFrames_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);
    if (result == Result::unavailable)
      unavailableFrames_.fetch_add(static_cast<uint64_t>(frames), std::memory_order_relaxed);
  }
  Snapshot snapshot() const noexcept {
    return {callbacks_.load(std::memory_order_relaxed), underruns_.load(std::memory_order_relaxed),
            bufferingFrames_.load(std::memory_order_relaxed), unavailableFrames_.load(std::memory_order_relaxed)};
  }
private:
  static_assert(std::atomic<uint64_t>::is_always_lock_free);
  std::atomic<uint64_t> callbacks_{0}, underruns_{0}, bufferingFrames_{0}, unavailableFrames_{0};
};
} // namespace fiddle
