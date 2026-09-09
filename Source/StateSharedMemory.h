#pragma once

#include <juce_core/juce_core.h>
#include <iostream>
#include <mutex>

namespace fiddle {

/**
 * Simple file-based state exchange between FiddleServer (producer) and
 * the Fiddle VST plugin (consumer).
 *
 * Producer writes the state blob to ~/Library/Caches/Fiddle/fiddle_state.bin
 * Consumer reads from the same file.
 *
 * Thread-safe on the producer side via mutex. The consumer does a simple
 * file read — if the file is being written, it may get a partial read,
 * but getStateInformation is called rarely (only on Dorico save) so this
 * is acceptable. A .tmp + rename pattern ensures atomicity.
 */
class StateSharedMemory {
public:
  explicit StateSharedMemory(bool isProducer)
      : StateSharedMemory(isProducer, defaultStateFile()) {}

  /// Explicit endpoint for isolated hosts/tests. Never falls back to the live
  /// user's cache when a caller supplies an endpoint.
  StateSharedMemory(bool isProducer, juce::File stateFile)
      : stateFile_(std::move(stateFile)), isProducer_(isProducer) {
    if (isProducer) {
      getStateFile().getParentDirectory().createDirectory();
      std::cerr << "[StateSharedMemory] Producer: "
                << getStateFile().getFullPathName() << std::endl;
    }
  }

  bool isReady() const { return getStateFile().existsAsFile(); }

  /// Producer: write a new state blob atomically (write to .tmp, then rename).
  void pushState(const juce::MemoryBlock &data) {
    if (!isProducer_)
      return;

    std::lock_guard<std::mutex> lock(writeMutex_);

    auto stateFile = getStateFile();
    auto tmpFile = stateFile.getSiblingFile(stateFile.getFileName() + ".tmp");

    // Write to temp file
    {
      juce::FileOutputStream fos(tmpFile);
      if (!fos.openedOk())
        return;
      fos.setPosition(0);
      fos.truncate();
      fos.write(data.getData(), data.getSize());
      fos.flush();
    }

    // Atomic rename
    tmpFile.moveFileTo(stateFile);
  }

  /// Consumer: read the latest state blob.
  juce::MemoryBlock pullState() const {
    auto stateFile = getStateFile();
    if (!stateFile.existsAsFile())
      return {};

    juce::MemoryBlock result;
    juce::FileInputStream fis(stateFile);
    if (fis.openedOk()) {
      auto size = fis.getTotalLength();
      if (size > 0 && size < 256 * 1024 * 1024) { // sanity check: < 256 MB
        result.setSize((size_t)size);
        fis.read(result.getData(), (int)size);
      }
    }
    return result;
  }

  /// Re-open (no-op for file-based approach)
  void remap() {}

private:
  const juce::File &getStateFile() const { return stateFile_; }

  static juce::File defaultStateFile() {
    return juce::File::getSpecialLocation(
               juce::File::userApplicationDataDirectory)
        .getChildFile("Caches")
        .getChildFile("Fiddle")
        .getChildFile("fiddle_state.bin");
  }

  juce::File stateFile_;
  bool isProducer_;
  std::mutex writeMutex_;
};

} // namespace fiddle
