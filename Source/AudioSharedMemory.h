#pragma once

#include <algorithm>
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <memory>

namespace fiddle {

using namespace juce;

/**
 * A lock-free Single-Producer, Single-Consumer (SPSC) ring buffer
 * backed by a memory-mapped file for zero-latency Inter-Process Communication
 * (IPC). FiddleServer writes audio into this buffer, and the Dorico VST reads
 * it.
 */
class AudioSharedMemory {
public:
  // Using 8192 as capacity (power of 2 is great for bitwise masking)
  // This gives us roughly ~185ms of buffering at 44.1kHz if needed,
  // but we will keep the read/write heads tight.
  static constexpr size_t kBufferCapacity = 8192;
  static constexpr size_t kNumChannels = 2; // Stereo for now

  // The exact layout of the shared memory file
  struct SharedState {
    std::atomic<uint64_t> magic;      // 0xF1DD1E00A0D10000
    std::atomic<uint64_t> writeIndex; // Number of samples written
    std::atomic<uint64_t> readIndex;  // Number of samples read
    std::atomic<double> sampleRate;   // Currently active sample rate

    // Interleaved floating point audio data [L, R, L, R...]
    // Because std::atomic float operations aren't standard cross-process
    // without care, we rely on the strictly monotonically increasing
    // writeIndex/readIndex acting as memory barriers.
    float audioData[kBufferCapacity * kNumChannels];
  };

  /**
   * Initializes the shared memory.
   * @param isProducer If true, this instance will create/truncate the file and
   * initialize the state.
   */
  AudioSharedMemory(bool isProducer) : producer(isProducer) {
    // macOS App Sandbox aggressively blocks /tmp and /Users/Shared IPC.
    // However, ~/Library/Caches is generally accessible to both apps and
    // plugins.
    File cacheDir = File::getSpecialLocation(File::userApplicationDataDirectory)
                        .getChildFile("Caches")
                        .getChildFile("Fiddle");
    if (producer && !cacheDir.exists()) {
      cacheDir.createDirectory();
    }
    File mapFile = cacheDir.getChildFile("fiddle_audio.mmap");
    size_t fileSize = sizeof(SharedState);

    if (producer) {
      if (mapFile.existsAsFile())
        mapFile.deleteFile();
      mapFile.create();

      // Make sure Dorico Sandbox can access this file regardless of owner
      // (Using juce::File::setReadOnly)
      mapFile.setReadOnly(false);

      // Initialize the file to the correct physical size with zeros
      FileOutputStream out(mapFile);
      if (out.openedOk()) {
        out.setPosition(fileSize - 1);
        out.writeByte(0); // This forces the OS to allocate the bytes on disk
        out.flush();
      } else {
        Logger::writeToLog(
            "Fatal Error: Could not allocate shared memory file size");
      }
    } else {
      // Consumer might start before producer. Wait until it exists.
      if (!mapFile.existsAsFile()) {
        Logger::writeToLog(
            "Shared memory file does not exist yet for consumer.");
      }
    }

    auto mode = MemoryMappedFile::readWrite;

    memoryMap = std::make_unique<MemoryMappedFile>(
        mapFile, Range<juce::int64>(0, fileSize), mode, false);

    if (memoryMap->getData() != nullptr) {
      state = reinterpret_cast<SharedState *>(memoryMap->getData());

      if (producer) {
        state->writeIndex.store(0, std::memory_order_relaxed);
        state->readIndex.store(0, std::memory_order_relaxed);
        state->sampleRate.store(44100.0, std::memory_order_relaxed);
        // Set magic number to indicate initialization is complete
        state->magic.store(0xF1DD1E00A0D10000, std::memory_order_release);
      }
    }
  }

  bool isReady() const {
    return state != nullptr &&
           state->magic.load(std::memory_order_acquire) == 0xF1DD1E00A0D10000;
  }

  /// Re-open the memory-mapped file. Call this on the consumer side when the
  /// server restarts, since the old mapping becomes stale.
  void remap() {
    if (producer)
      return; // Only consumers need to remap

    state = nullptr;
    memoryMap.reset();

    File cacheDir = File::getSpecialLocation(File::userApplicationDataDirectory)
                        .getChildFile("Caches")
                        .getChildFile("Fiddle");
    File mapFile = cacheDir.getChildFile("fiddle_audio.mmap");
    size_t fileSize = sizeof(SharedState);

    if (!mapFile.existsAsFile())
      return;

    auto mode = MemoryMappedFile::readWrite;
    memoryMap = std::make_unique<MemoryMappedFile>(
        mapFile, Range<juce::int64>(0, fileSize), mode, false);

    if (memoryMap->getData() != nullptr) {
      state = reinterpret_cast<SharedState *>(memoryMap->getData());
    }
  }

  MemoryMappedFile *getMemoryMap() const { return memoryMap.get(); }

  File getMapFile() const {
    File cacheDir = File::getSpecialLocation(File::userApplicationDataDirectory)
                        .getChildFile("Caches")
                        .getChildFile("Fiddle");
    return cacheDir.getChildFile("fiddle_audio.mmap");
  }

  //------------------------------------------------------------------------------------------------
  // PRODUCER (FiddleServer System)
  //------------------------------------------------------------------------------------------------

  /**
   * Pushes an interleaved audio buffer into the ring.
   * Fails silently if there is not enough space (buffer full).
   */
  void pushAudio(const AudioBuffer<float> &buffer) {
    if (!isReady() || !producer)
      return;

    const int numSamples = buffer.getNumSamples();
    const int numChannels =
        std::min(buffer.getNumChannels(), (int)kNumChannels);

    uint64_t writePos = state->writeIndex.load(std::memory_order_relaxed);
    uint64_t readPos = state->readIndex.load(std::memory_order_acquire);

    // Check available space
    if (writePos - readPos + numSamples > kBufferCapacity) {
      // Buffer Overflow/Underrun. Consumer is too slow.
      // We could skip, but we'll aggressively jump the write head to force a
      // reset. state->writeIndex.store(readPos, std::memory_order_relaxed);
      return;
    }

    // Interleave the juices into our flat array
    for (int i = 0; i < numSamples; ++i) {
      size_t index = (writePos + i) % kBufferCapacity;
      for (int c = 0; c < numChannels; ++c) {
        state->audioData[(index * kNumChannels) + c] = buffer.getSample(c, i);
      }
    }

    // Publish the new write index
    state->writeIndex.store(writePos + numSamples, std::memory_order_release);
  }

  void setSampleRate(double sampleRate) {
    if (isReady() && producer) {
      state->sampleRate.store(sampleRate, std::memory_order_relaxed);
    }
  }

  //------------------------------------------------------------------------------------------------
  // CONSUMER (Dorico VST Plugin)
  //------------------------------------------------------------------------------------------------

  /**
   * Pulls audio from the ring into the provided buffer.
   * If not enough data is available (underrun), it pads with zeros.
   */
  void pullAudio(AudioBuffer<float> &buffer) {
    if (!isReady() || producer) {
      buffer.clear();
      return;
    }

    const int numSamples = buffer.getNumSamples();
    const int numChannels =
        std::min((int)kNumChannels, buffer.getNumChannels());

    uint64_t writePos = state->writeIndex.load(std::memory_order_acquire);
    uint64_t readPos = state->readIndex.load(std::memory_order_relaxed);

    uint64_t available = writePos - readPos;

    int samplesToRead = std::min((int)available, numSamples);

    // De-interleave from shared memory into JUCE buffer
    for (int i = 0; i < samplesToRead; ++i) {
      size_t index = (readPos + i) % kBufferCapacity;
      for (int c = 0; c < numChannels; ++c) {
        buffer.setSample(c, i, state->audioData[(index * kNumChannels) + c]);
      }
    }

    // Pad the rest with zeros if we underran
    if (samplesToRead < numSamples) {
      for (int c = 0; c < buffer.getNumChannels(); ++c) {
        FloatVectorOperations::clear(buffer.getWritePointer(c, samplesToRead),
                                     numSamples - samplesToRead);
      }
    }

    // Publish the new read index
    state->readIndex.store(readPos + samplesToRead, std::memory_order_release);
  }

  double getSampleRate() const {
    if (isReady()) {
      return state->sampleRate.load(std::memory_order_relaxed);
    }
    return 44100.0;
  }

private:
  bool producer;
  std::unique_ptr<MemoryMappedFile> memoryMap;
  SharedState *state = nullptr;
};

} // namespace fiddle
