#pragma once

#include "../RealtimeReadGuard.h"
#include "../AudioDiagnostics.h"
#include "../AudioStreamRing.h"
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
      // Output silence
      for (int c = 0; c < numChannels; ++c)
        if (outputChannels[c])
          std::memset(outputChannels[c], 0, numSamples * sizeof(float));
      return;
    }

    if (resetBuffering_.exchange(false, std::memory_order_acquire)) inUnderrun_ = false;
    const auto result = state->pull(outputChannels, numChannels, numSamples, hostRate, audioStreamTimeMs());
    using Result = AudioReturnDiagnostics::Result;
    if (result == AudioStreamRing::PullResult::audio) {
      inUnderrun_ = false;
      diagnostics_.record(Result::rendered, numSamples);
    } else if (result == AudioStreamRing::PullResult::unavailable) {
      diagnostics_.record(Result::unavailable, numSamples);
    } else {
      diagnostics_.record(inUnderrun_ ? Result::buffering : Result::underrun, numSamples);
      inUnderrun_ = true;
    }
  }

private:
  const std::string mapPath_;
  AudioReturnDiagnostics diagnostics_;
  bool inUnderrun_ = false; // consumer audio thread only
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
