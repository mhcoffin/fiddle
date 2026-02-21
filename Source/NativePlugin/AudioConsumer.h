#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <pwd.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

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

  AudioConsumer() { openMapping(); }

  ~AudioConsumer() { closeMapping(); }

  /// Re-open the memory-mapped file (e.g. after server restart).
  void remap() {
    closeMapping();
    openMapping();
  }

  bool isReady() const {
    return state_ != nullptr &&
           state_->magic.load(std::memory_order_acquire) == kMagic;
  }

  /// Pull audio from the ring buffer into an interleaved output.
  /// numSamples = number of sample frames (not total floats).
  /// output must hold at least numSamples * kNumChannels floats.
  void pullAudio(float **outputChannels, int numChannels, int numSamples) {
    if (!isReady()) {
      // Output silence
      for (int c = 0; c < numChannels; ++c)
        if (outputChannels[c])
          std::memset(outputChannels[c], 0, numSamples * sizeof(float));
      return;
    }

    uint64_t writePos = state_->writeIndex.load(std::memory_order_acquire);
    uint64_t readPos = state_->readIndex.load(std::memory_order_relaxed);
    uint64_t available = writePos - readPos;
    int samplesToRead = static_cast<int>(
        available < static_cast<uint64_t>(numSamples) ? available : numSamples);

    int outCh = numChannels < static_cast<int>(kNumChannels)
                    ? numChannels
                    : static_cast<int>(kNumChannels);

    // De-interleave from shared memory into separate channel buffers
    for (int i = 0; i < samplesToRead; ++i) {
      size_t index = (readPos + i) % kBufferCapacity;
      for (int c = 0; c < outCh; ++c) {
        outputChannels[c][i] = state_->audioData[index * kNumChannels + c];
      }
    }

    // Pad remaining samples with silence
    if (samplesToRead < numSamples) {
      for (int c = 0; c < numChannels; ++c) {
        if (outputChannels[c])
          std::memset(outputChannels[c] + samplesToRead, 0,
                      (numSamples - samplesToRead) * sizeof(float));
      }
    }

    state_->readIndex.store(readPos + samplesToRead, std::memory_order_release);
  }

  /// Read the playback delay (ms) from active_config.txt line 2.
  /// Returns 1000 if not found.
  static int readActiveDelay() {
    std::string path =
        getHomeDir() + "/Library/Application Support/Fiddle/active_config.txt";
    std::ifstream f(path);
    if (!f.is_open())
      return 1000;
    std::string line1, line2;
    std::getline(f, line1); // config path
    std::getline(f, line2); // delay ms
    if (line2.empty())
      return 1000;
    try {
      return std::stoi(line2);
    } catch (...) {
      return 1000;
    }
  }

private:
  SharedState *state_ = nullptr;
  void *mappedMem_ = nullptr;
  size_t mappedSize_ = 0;
  int fd_ = -1;

  void openMapping() {
    std::string path =
        getHomeDir() + "/Library/Caches/Fiddle/fiddle_audio.mmap";

    fd_ = ::open(path.c_str(), O_RDWR);
    if (fd_ < 0)
      return;

    mappedSize_ = sizeof(SharedState);

    mappedMem_ = ::mmap(nullptr, mappedSize_, PROT_READ | PROT_WRITE,
                        MAP_SHARED, fd_, 0);
    if (mappedMem_ == MAP_FAILED) {
      mappedMem_ = nullptr;
      ::close(fd_);
      fd_ = -1;
      return;
    }

    state_ = reinterpret_cast<SharedState *>(mappedMem_);
  }

  void closeMapping() {
    state_ = nullptr;
    if (mappedMem_ && mappedMem_ != MAP_FAILED) {
      ::munmap(mappedMem_, mappedSize_);
      mappedMem_ = nullptr;
    }
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
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
