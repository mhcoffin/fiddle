#pragma once

#include <atomic>
#include <cstring>
#include <juce_core/juce_core.h>

namespace fiddle {

/**
 * Lock-free state exchange between FiddleServer (producer) and
 * the Fiddle VST plugin (consumer) via a memory-mapped file.
 *
 * Uses a triple-buffer pattern:
 *   - Producer writes to back buffer, then atomically publishes it as "ready"
 *   - Consumer atomically swaps "ready" → "front" and reads front
 *   - Always lock-free; neither side ever blocks
 *
 * File: ~/Library/Caches/Fiddle/fiddle_state.mmap
 */
class StateSharedMemory {
public:
  // Maximum state blob size: 64 MB should be plenty for orchestral projects
  static constexpr size_t kMaxBlobSize = 64 * 1024 * 1024;
  static constexpr uint64_t kMagic = 0xF1DD1E57A7E00000ULL;

  struct Header {
    std::atomic<uint64_t> magic;
    std::atomic<uint32_t> readyIndex; // buffer index that has the latest data
    std::atomic<uint32_t> sequence;   // incremented on each publish

    // Per-buffer metadata
    struct BufferMeta {
      std::atomic<uint32_t> size; // actual data size in this buffer
    };
    BufferMeta buffers[3];
  };

  StateSharedMemory(bool isProducer) : isProducer_(isProducer) {
    auto mapFile = getMapFile();

    if (isProducer) {
      mapFile.getParentDirectory().createDirectory();

      // Calculate total file size: header + 3 buffers
      size_t totalSize = sizeof(Header) + 3 * kMaxBlobSize;

      // Create/truncate the file
      {
        juce::FileOutputStream fos(mapFile);
        if (fos.openedOk()) {
          fos.setPosition((juce::int64)totalSize - 1);
          fos.writeByte(0);
        }
      }

      mmapFile_ = std::make_unique<juce::MemoryMappedFile>(
          mapFile, juce::MemoryMappedFile::readWrite);

      if (mmapFile_->getData()) {
        auto *header = getHeader();
        header->magic.store(kMagic, std::memory_order_relaxed);
        header->readyIndex.store(0, std::memory_order_relaxed);
        header->sequence.store(0, std::memory_order_relaxed);
        for (int i = 0; i < 3; ++i)
          header->buffers[i].size.store(0, std::memory_order_relaxed);

        std::cerr << "[StateSharedMemory] Producer created: "
                  << mapFile.getFullPathName() << " ("
                  << totalSize / (1024 * 1024) << " MB)" << std::endl;
      }
    } else {
      // Consumer: open read-only
      if (mapFile.existsAsFile()) {
        mmapFile_ = std::make_unique<juce::MemoryMappedFile>(
            mapFile, juce::MemoryMappedFile::readOnly);
      }
    }
  }

  bool isReady() const {
    if (!mmapFile_ || !mmapFile_->getData())
      return false;
    auto *header = static_cast<const Header *>(mmapFile_->getData());
    return header->magic.load(std::memory_order_relaxed) == kMagic;
  }

  /// Producer: write a new state blob. Lock-free.
  void pushState(const juce::MemoryBlock &data) {
    if (!isProducer_ || !isReady())
      return;
    if (data.getSize() > kMaxBlobSize) {
      std::cerr << "[StateSharedMemory] Blob too large: " << data.getSize()
                << " bytes (max " << kMaxBlobSize << ")" << std::endl;
      return;
    }

    auto *header = getHeader();

    // Pick a buffer that isn't the current ready buffer
    uint32_t readyIdx = header->readyIndex.load(std::memory_order_acquire);
    uint32_t writeIdx = (readyIdx + 1) % 3;

    // Write data to the chosen buffer
    uint8_t *bufPtr = getBufferPtr(writeIdx);
    std::memcpy(bufPtr, data.getData(), data.getSize());
    header->buffers[writeIdx].size.store((uint32_t)data.getSize(),
                                         std::memory_order_release);

    // Publish: make this the new ready buffer
    header->readyIndex.store(writeIdx, std::memory_order_release);
    header->sequence.fetch_add(1, std::memory_order_release);
  }

  /// Consumer: read the latest state blob. Lock-free.
  juce::MemoryBlock pullState() const {
    if (!isReady())
      return {};

    auto *header = static_cast<const Header *>(mmapFile_->getData());

    uint32_t readyIdx = header->readyIndex.load(std::memory_order_acquire);
    uint32_t size =
        header->buffers[readyIdx].size.load(std::memory_order_acquire);

    if (size == 0)
      return {};

    const uint8_t *bufPtr = getBufferPtrConst(readyIdx);
    return juce::MemoryBlock(bufPtr, size);
  }

  /// Consumer: get the current sequence number (to detect changes)
  uint32_t getSequence() const {
    if (!isReady())
      return 0;
    auto *header = static_cast<const Header *>(mmapFile_->getData());
    return header->sequence.load(std::memory_order_acquire);
  }

  /// Re-open the memory-mapped file (consumer side, after server restart)
  void remap() {
    auto mapFile = getMapFile();
    if (mapFile.existsAsFile()) {
      mmapFile_ = std::make_unique<juce::MemoryMappedFile>(
          mapFile, isProducer_ ? juce::MemoryMappedFile::readWrite
                               : juce::MemoryMappedFile::readOnly);
    }
  }

private:
  Header *getHeader() { return static_cast<Header *>(mmapFile_->getData()); }

  uint8_t *getBufferPtr(uint32_t index) {
    auto *base = static_cast<uint8_t *>(mmapFile_->getData());
    return base + sizeof(Header) + (size_t)index * kMaxBlobSize;
  }

  const uint8_t *getBufferPtrConst(uint32_t index) const {
    auto *base = static_cast<const uint8_t *>(mmapFile_->getData());
    return base + sizeof(Header) + (size_t)index * kMaxBlobSize;
  }

  static juce::File getMapFile() {
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("Caches")
        .getChildFile("Fiddle")
        .getChildFile("fiddle_state.mmap");
  }

  bool isProducer_;
  std::unique_ptr<juce::MemoryMappedFile> mmapFile_;
};

} // namespace fiddle
