#pragma once

#include "../RealtimeReadGuard.h"
#include "../AudioDiagnostics.h"
#include "../AudioStreamRing.h"
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
#include <memory>
#include <mutex>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace fiddle {

/**
 * POSIX-based consumer for the shared memory audio ring buffer.
 * Reads the version-2 AudioStreamRing produced by RenderAheadEngine.
 * Each stream generation has a separate inode; remapping is control-thread
 * work and retired mappings survive any in-flight audio callback.
 */
class AudioConsumer {
public:
  static constexpr size_t kBufferCapacity = AudioStreamRing::capacity;
  static constexpr size_t kNumChannels = 2;
  static constexpr uint64_t kMagic = AudioStreamRing::magicValue;
  using SharedState = AudioStreamRing;

  explicit AudioConsumer(std::string path = getHomeDir() + "/Library/Caches/Fiddle/fiddle_audio_v2.mmap")
      : mapPath_(std::move(path)) { activeMapping_.store(openMapping()); }

  ~AudioConsumer() {
    delete activeMapping_.exchange(nullptr, std::memory_order_acq_rel);
    retiredMappings_.clear();
  }

  /// Re-open the memory-mapped file (e.g. after server restart).
  void remap() {
    auto *replacement = openMapping();
    {
      std::lock_guard<std::mutex> lock(retiredMutex_);
      auto *old =
          activeMapping_.exchange(replacement, std::memory_order_seq_cst);
      if (old)
        retiredMappings_.emplace_back(old);
    }
    resetBuffering_.store(true, std::memory_order_release);
    reclaimMappings();
  }

  bool isReady() const {
    RealtimeReadGuard<Mapping> mappingRead(activeMapping_, readers_);
    auto *mapping = mappingRead.get();
    return mapping && mapping->state &&
           mapping->state->magic.load(std::memory_order_acquire) == kMagic;
  }

  AudioReturnDiagnostics::Snapshot diagnostics() const noexcept { return diagnostics_.snapshot(); }

  /// Pull interleaved ring audio into planar output channels.
  /// numSamples = number of sample frames (not total floats).
  /// Each output channel must hold at least numSamples floats.
  void pullAudio(float *const *outputChannels, int numChannels, int numSamples,
                 double hostRate = 0, uint64_t expectedStreamId = 0) {
    if (numSamples <= 0) return;
    RealtimeReadGuard<Mapping> mappingRead(activeMapping_, readers_);
    auto *mapping = mappingRead.get();
    auto *state = mapping ? mapping->state : nullptr;
    if (!state || state->magic.load(std::memory_order_acquire) != kMagic ||
        (expectedStreamId != 0 && state->streamId != expectedStreamId)) {
      diagnostics_.record(AudioReturnDiagnostics::Result::unavailable, numSamples);
      safetyMuted_ = false;
      underrunSeen_ = false;
      healthyRecoveryFrames_ = 0;
      recoveryFadeRemaining_ = recoveryFadeLength_ = 0;
      silence(outputChannels, numChannels, numSamples);
      return;
    }

    if (resetBuffering_.exchange(false, std::memory_order_acquire)) {
      safetyMuted_ = false;
      underrunSeen_ = false;
      healthyRecoveryFrames_ = 0;
      recoveryFadeRemaining_ = recoveryFadeLength_ = 0;
    }

    // A full/healthy ring normally oscillates around targetFrames. If it drops
    // below two host callbacks (or one producer block), stop exposing isolated
    // fragments and wait for the completed-audio reserve to recover. We still
    // call pull() while muted so Dorico's timeline advances and late audio is
    // discarded rather than replayed after the interruption.
    const auto read = state->readFrame.load(std::memory_order_relaxed);
    const auto written = state->writeFrame.load(std::memory_order_acquire);
    const bool coherentWindow = written >= read && written - read <= AudioStreamRing::capacity;
    const uint64_t available = coherentWindow ? written - read : 0;
    diagnostics_.observeQueuedFrames(available);
    const uint64_t lowWaterFrames = std::min(
        AudioStreamRing::capacity,
        std::max(uint64_t(numSamples) * 2, uint64_t(state->blockSize)));
    // The producer writes complete blocks, so with a host block size that does
    // not divide blockSize it may be unable to reach targetFrames exactly. More
    // than target-block frames is the fullest state in which another producer
    // block cannot fit.
    const uint64_t producerFullFrames = state->targetFrames > state->blockSize
        ? uint64_t(state->targetFrames - state->blockSize) + 1
        : uint64_t(state->targetFrames);
    const uint64_t recoveryFrames = std::min(
        AudioStreamRing::capacity, std::max(lowWaterFrames, producerFullFrames));
    const bool usable = state->active.load(std::memory_order_acquire) &&
        uint64_t(numSamples) <= AudioStreamRing::capacity &&
        !(hostRate > 0 && std::abs(hostRate - state->sampleRate) > 1.0);
    bool recovering = false;
    if (!usable) {
      safetyMuted_ = false;
      underrunSeen_ = false;
      healthyRecoveryFrames_ = 0;
      recoveryFadeRemaining_ = recoveryFadeLength_ = 0;
    } else if (safetyMuted_) {
      // A producer can keep up indefinitely while its last block is still
      // in flight at each host callback. Requiring a full reserve would then
      // latch silence forever, even though every requested block is present.
      // Also accept 100 ms of continuously healthy callback-time occupancy.
      // A single low-water callback resets the probation period, so isolated
      // fragments during a stall still stay muted.
      const double rate = hostRate > 0 ? hostRate : state->sampleRate;
      const auto stableFrames = static_cast<uint64_t>(
          std::isfinite(rate) && rate > 0 ? std::ceil(rate * 0.100) : 4800);
      if (coherentWindow && available >= lowWaterFrames)
        healthyRecoveryFrames_ = std::min(stableFrames,
            healthyRecoveryFrames_ + static_cast<uint64_t>(numSamples));
      else
        healthyRecoveryFrames_ = 0;
      if (coherentWindow && (available >= recoveryFrames ||
                            healthyRecoveryFrames_ >= stableFrames)) {
        safetyMuted_ = false;
        healthyRecoveryFrames_ = 0;
        recovering = true;
      }
    } else if (!safetyMuted_ && state->targetFrames > 0 && available < lowWaterFrames) {
      beginSafetyMute();
    }

    const auto result = state->pull(outputChannels, numChannels, numSamples, hostRate, audioStreamTimeMs());
    using Result = AudioReturnDiagnostics::Result;
    if (result == AudioStreamRing::PullResult::unavailable) {
      safetyMuted_ = false;
      underrunSeen_ = false;
      healthyRecoveryFrames_ = 0;
      recoveryFadeRemaining_ = recoveryFadeLength_ = 0;
      diagnostics_.record(Result::unavailable, numSamples);
    } else if (result == AudioStreamRing::PullResult::underrun) {
      if (!safetyMuted_) beginSafetyMute();
      healthyRecoveryFrames_ = 0;
      silence(outputChannels, numChannels, numSamples);
      diagnostics_.record(underrunSeen_ ? Result::buffering : Result::underrun,
                          numSamples);
      diagnostics_.recordSafetyMutedFrames(numSamples);
      underrunSeen_ = true;
    } else if (safetyMuted_) {
      // The block was valid but the reserve has not recovered yet. Suppress it
      // to avoid alternating audio/silence while the producer catches up.
      silence(outputChannels, numChannels, numSamples);
      diagnostics_.record(Result::buffering, numSamples);
      diagnostics_.recordSafetyMutedFrames(numSamples);
    } else {
      if (recovering) {
        underrunSeen_ = false;
        beginRecoveryFade(hostRate > 0 ? hostRate : state->sampleRate);
      }
      applyRecoveryFade(outputChannels, numChannels, numSamples);
      diagnostics_.record(Result::rendered, numSamples);
    }
  }

private:
  const std::string mapPath_;
  AudioReturnDiagnostics diagnostics_;
  bool safetyMuted_ = false, underrunSeen_ = false; // consumer audio thread only
  uint64_t healthyRecoveryFrames_ = 0;
  int recoveryFadeRemaining_ = 0, recoveryFadeLength_ = 0;
  struct Mapping {
    SharedState *state = nullptr;
    void *memory = nullptr;
    size_t size = 0;
    int fd = -1;

    ~Mapping() {
      if (memory && memory != MAP_FAILED)
        ::munmap(memory, size);
      if (fd >= 0)
        ::close(fd);
    }
  };

  std::atomic<Mapping *> activeMapping_{nullptr};
  mutable std::atomic<uint32_t> readers_{0};
  std::atomic<bool> resetBuffering_{false};
  std::mutex retiredMutex_;
  std::vector<std::unique_ptr<Mapping>> retiredMappings_;

  void beginSafetyMute() noexcept {
    safetyMuted_ = true;
    underrunSeen_ = false;
    healthyRecoveryFrames_ = 0;
    recoveryFadeRemaining_ = recoveryFadeLength_ = 0;
    diagnostics_.beginSafetyMute();
  }

  static void silence(float *const *channels, int count, int frames) noexcept {
    for (int channel = 0; channel < count; ++channel)
      if (channels[channel])
        std::memset(channels[channel], 0, size_t(frames) * sizeof(float));
  }

  void beginRecoveryFade(double rate) noexcept {
    // Ten milliseconds hides the discontinuity without making recovery feel
    // sluggish. Bound the work/state for unusual host sample rates.
    const auto samples = std::isfinite(rate) ? rate * 0.010 : 480.0;
    recoveryFadeLength_ = int(std::clamp(samples, 1.0, 4096.0));
    recoveryFadeRemaining_ = recoveryFadeLength_;
  }

  void applyRecoveryFade(float *const *channels, int count, int frames) noexcept {
    if (recoveryFadeRemaining_ <= 0) return;
    const int faded = std::min(frames, recoveryFadeRemaining_);
    const int offset = recoveryFadeLength_ - recoveryFadeRemaining_;
    for (int channel = 0; channel < count; ++channel) if (channels[channel])
      for (int frame = 0; frame < faded; ++frame)
        channels[channel][frame] *= float(offset + frame + 1) / float(recoveryFadeLength_);
    recoveryFadeRemaining_ -= faded;
  }

  Mapping *openMapping() {
    auto mapping = std::make_unique<Mapping>();
    mapping->fd = ::open(mapPath_.c_str(), O_RDWR);
    if (mapping->fd < 0)
      return nullptr;

    mapping->size = sizeof(SharedState);
    struct stat fileInfo {};
    if (::fstat(mapping->fd, &fileInfo) != 0 || fileInfo.st_size < static_cast<off_t>(mapping->size))
      return nullptr;

    mapping->memory = ::mmap(nullptr, mapping->size, PROT_READ | PROT_WRITE,
                             MAP_SHARED, mapping->fd, 0);
    if (mapping->memory == MAP_FAILED) {
      mapping->memory = nullptr;
      return nullptr;
    }

    mapping->state = reinterpret_cast<SharedState *>(mapping->memory);
    return mapping.release();
  }

  void reclaimMappings() {
    std::lock_guard<std::mutex> lock(retiredMutex_);
    if (readers_.load(std::memory_order_seq_cst) != 0)
      return;
    retiredMappings_.clear();
  }

  static std::string getHomeDir() {
    const char *home = getenv("HOME");
    if (home)
      return home;
    struct passwd *pw = getpwuid(getuid());
    if (pw)
      return pw->pw_dir;
    return "/tmp";
  }
};

} // namespace fiddle
