#pragma once

#include "../StateSharedMemory.h"
#include "FiddleDatabase.h"
#include "MixerModel.h"
#include "PluginScanner.h"
#include "VersionStore.h"
#include <atomic>
#include <juce_core/juce_core.h>

namespace fiddle {

/**
 * Manages a pre-built binary "shadow state" blob that is always ready
 * for Dorico to read via shared memory (getStateInformation).
 *
 * Architecture:
 *   - markDirty() called after every strip mutation
 *   - Blob rebuild is coalesced via callAsync on the message thread
 *   - State file lets VST plugin read without blocking
 *
 * Blob format: see serializeState()
 */
class StateManager {
public:
  // Magic bytes at the start of every state blob
  static constexpr uint32_t kBlobMagic = 0x46444C53; // "FDLS"
  static constexpr uint32_t kBlobVersion = 3;

  StateManager();
  ~StateManager();

  /// Initialize: open shared memory as producer
  void initialize();

  /// Mark state as dirty. Will schedule a background rebuild.
  void markDirty();

  /// Is there unsaved state?
  bool isDirty() const { return dirty_.load(std::memory_order_acquire); }

  /// Clear dirty flag (e.g. after explicit user save)
  void clearDirty() { dirty_.store(false, std::memory_order_release); }

  /// Current config name
  juce::String getConfigName() const {
    juce::SpinLock::ScopedLockType lock(nameLock_);
    return configName_;
  }
  void setConfigName(const juce::String &name) {
    juce::SpinLock::ScopedLockType lock(nameLock_);
    configName_ = name;
  }

  /// Current config version (ISO 8601 timestamp) - v2 legacy
  juce::String getConfigVersion() const {
    juce::SpinLock::ScopedLockType lock(nameLock_);
    return configVersion_;
  }
  void setConfigVersion(const juce::String &version) {
    juce::SpinLock::ScopedLockType lock(nameLock_);
    configVersion_ = version;
  }

  void setVersionStore(versioning::VersionStore *store) {
    versionStore_ = store;
  }

  /// Set the current branch UUID so buildStateBlob can look up ancestors.
  void setCurrentBranchId(const std::string &branchId) {
    juce::SpinLock::ScopedLockType lock(nameLock_);
    currentBranchId_ = branchId;
  }
  std::string getCurrentBranchId() const {
    juce::SpinLock::ScopedLockType lock(nameLock_);
    return currentBranchId_;
  }

  /// Build the state blob synchronously. Normally called on background thread.
  /// Builds a state blob for Dorico synchronization. Constantly called on
  /// changes.
  juce::MemoryBlock buildStateBlob(MixerModel &mixer);

  /// Commits the current live mixer state as a permanent snapshot node in the
  /// DAG.
  versioning::Hash commitCurrentState(MixerModel &mixer,
                                      const std::string &branchId);

  /// Push a pre-built blob to shared memory.
  void publishBlob(const juce::MemoryBlock &blob);

  /// Schedule a rebuild on the message thread (coalesced).
  void scheduleRebuild(std::function<juce::MemoryBlock()> buildFn);

  // ── Restore (from Dorico setStateInformation) ───────────────────────
  struct RestoredStrip {
    juce::String id, library, family;
    bool isSolo = true;
    int inputPort = -1, inputChannel = -1;
    int pluginUid = 0;
    float gainDb = 0.0f;
    std::string expressionMapEntityID;
    juce::MemoryBlock pluginState;
  };

  struct RestoredState {
    juce::String configName;
    juce::String configVersion;
    bool dirty = false;
    std::string stateHash;
    std::vector<std::string> ancestorHashes;
    std::vector<RestoredStrip> strips;
  };

  /// Deserialize a blob received from Dorico into structured data.
  static std::optional<RestoredState> deserializeBlob(const void *data,
                                                      size_t size);

private:
  std::atomic<bool> dirty_{false};
  juce::String configName_;
  juce::String configVersion_;
  mutable juce::SpinLock nameLock_;

  std::unique_ptr<StateSharedMemory> sharedMemory_;
  std::atomic<bool> rebuildPending_{false};
  versioning::VersionStore *versionStore_ = nullptr;
  std::string currentBranchId_;
};

} // namespace fiddle
