#pragma once

#include "../RealtimeReadGuard.h"
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
 * Reads audio produced by FiddleServer (AudioSharedMemory producer).
 *
 * The memory layout MUST match AudioSharedMemory::SharedState exactly:
 *   - magic:      std::atomic<uint64_t>  (0xF1DD1E00A0D10000 when ready)
 *   - writeIndex: std::atomic<uint64_t>
 *   - readIndex:  std::atomic<uint64_t>
 *   - sampleRate: std::atomic<double>
 *   - audioData:  float[kBufferCapacity * kNumChannels]  (interleaved L,R)
 */
class AudioConsumer {
public:
  static constexpr size_t kBufferCapacity = 8192;
  static constexpr size_t kNumChannels = 2;
  static constexpr uint64_t kMagic = 0xF1DD1E00A0D10000;

  struct SharedState {
    std::atomic<uint64_t> magic;
    std::atomic<uint64_t> writeIndex;
    std::atomic<uint64_t> readIndex;
    std::atomic<double> sampleRate;
    float audioData[kBufferCapacity * kNumChannels];
  };

  AudioConsumer() { activeMapping_.store(openMapping()); }

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
          activeMapping_.exchange(replacement, std::memory_order_acq_rel);
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

  bool buffering_ = true;
  double jitterMs_ = 40.0;

  /// Pull audio from the ring buffer into an interleaved output.
  /// numSamples = number of sample frames (not total floats).
  /// output must hold at least numSamples * kNumChannels floats.
  void pullAudio(float **outputChannels, int numChannels, int numSamples) {
    RealtimeReadGuard<Mapping> mappingRead(activeMapping_, readers_);
    auto *mapping = mappingRead.get();
    auto *state = mapping ? mapping->state : nullptr;
    if (!state || state->magic.load(std::memory_order_acquire) != kMagic) {
      // Output silence
      for (int c = 0; c < numChannels; ++c)
        if (outputChannels[c])
          std::memset(outputChannels[c], 0, numSamples * sizeof(float));
      return;
    }

    if (resetBuffering_.exchange(false, std::memory_order_acquire))
      buffering_ = true;

    uint64_t writePos = state->writeIndex.load(std::memory_order_acquire);
    uint64_t readPos = state->readIndex.load(std::memory_order_relaxed);
    uint64_t available = writePos - readPos;

    uint64_t targetBuffer = static_cast<uint64_t>(
        state->sampleRate.load(std::memory_order_relaxed) * (jitterMs_ / 1000.0));
    if (targetBuffer == 0) targetBuffer = 1764; // Fallback ~40ms at 44.1kHz

    if (buffering_) {
        if (available >= targetBuffer) {
            buffering_ = false;
        } else {
            // Output silence while filling the buffer
            for (int c = 0; c < numChannels; ++c)
              if (outputChannels[c])
                std::memset(outputChannels[c], 0, numSamples * sizeof(float));
            return;
        }
    }

    // Check for underrun
    if (available < static_cast<uint64_t>(numSamples)) {
        buffering_ = true; // Enter buffering mode to re-sync
        for (int c = 0; c < numChannels; ++c)
          if (outputChannels[c])
            std::memset(outputChannels[c], 0, numSamples * sizeof(float));
        return;
    }

    int samplesToRead = numSamples;

    int outCh = numChannels < static_cast<int>(kNumChannels)
                    ? numChannels
                    : static_cast<int>(kNumChannels);

    // De-interleave from shared memory into separate channel buffers
    for (int i = 0; i < samplesToRead; ++i) {
      size_t index = (readPos + i) % kBufferCapacity;
      for (int c = 0; c < outCh; ++c) {
        outputChannels[c][i] = state->audioData[index * kNumChannels + c];
      }
    }

    state->readIndex.store(readPos + samplesToRead, std::memory_order_release);
  }

private:
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
    std::string path =
        getHomeDir() + "/Library/Caches/Fiddle/fiddle_audio.mmap";

    mapping->fd = ::open(path.c_str(), O_RDWR);
    if (mapping->fd < 0)
      return nullptr;

    mapping->size = sizeof(SharedState);

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
    if (readers_.load(std::memory_order_acquire) != 0)
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
