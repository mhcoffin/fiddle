#pragma once

#include "HostedPluginSlot.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

inline constexpr const char *kMasterAudioId = "master";

struct MasterInsertSnapshot {
  juce::String slotId;
  juce::PluginDescription description;
  bool bypassed = false;
  juce::MemoryBlock pluginState;
};

struct MasterAudioSnapshot {
  float gainDb = 0.0f;
  std::vector<MasterInsertSnapshot> inserts;
};

/// Owns Fiddle's permanent stereo Master stage. The ordered slot model is the
/// source of truth; AudioProcessorGraph node IDs are rebuilt runtime details.
class MasterAudioEngine {
public:
  using ChangeCallback = std::function<void()>;
  using LoadCompletion =
      std::function<void(bool success, const juce::String &error)>;

  MasterAudioEngine();
  ~MasterAudioEngine();

  MasterAudioEngine(const MasterAudioEngine &) = delete;
  MasterAudioEngine &operator=(const MasterAudioEngine &) = delete;

  void prepareToPlay(double sampleRate, int blockSize);
  void releaseResources();
  void processBlock(juce::AudioBuffer<float> &audio);

  void setOnChanged(ChangeCallback callback);

  [[nodiscard]] int insertCount() const noexcept;
  [[nodiscard]] int indexOf(const juce::String &slotId) const noexcept;
  [[nodiscard]] std::optional<MasterInsertSnapshot>
  snapshot(const juce::String &slotId) const;
  [[nodiscard]] MasterAudioSnapshot snapshotAll() const;

  /// Inserts the slot immediately in loading/pass-through state, then creates
  /// the processor asynchronously. The stable slot ID is preserved on undo,
  /// restore, and retry.
  bool insert(const MasterInsertSnapshot &snapshot, int index,
              juce::AudioPluginFormatManager &formatManager,
              LoadCompletion completion = nullptr, bool notify = true);
  /// Deterministic construction seam used by graph and command tests.
  bool insertProcessor(const MasterInsertSnapshot &snapshot, int index,
                       std::unique_ptr<juce::AudioProcessor> processor,
                       bool notify = true);
  bool remove(const juce::String &slotId, bool notify = true);
  void clear(bool notify = true);
  bool move(const juce::String &slotId, int newIndex, bool notify = true);
  bool setBypassed(const juce::String &slotId, bool bypassed,
                   bool notify = true);
  bool showEditor(const juce::String &slotId);

  void setGainDb(float gainDb, bool notify = true);
  [[nodiscard]] float gainDb() const noexcept;
  [[nodiscard]] float peakDb() const noexcept;
  [[nodiscard]] int latencySamples() const noexcept;
  [[nodiscard]] double latencyMs() const noexcept;

  /// Message-thread maintenance for state listeners and dynamic latency.
  bool consumePluginChanges(bool suppressPlaybackChanges = false);
  bool refreshPluginStateCaches();

  /// Record current public parameter values as the clean baseline without
  /// serializing state or notifying the owner.
  void captureParameterFingerprints();

  [[nodiscard]] juce::var toJson() const;

private:
  struct Entry {
    juce::String id;
    juce::PluginDescription description;
    std::shared_ptr<HostedPluginSlot> hosted;
    int graphLatencySamples = 0;
    std::optional<uint64_t> parameterFingerprint;
  };

  std::shared_ptr<Entry> find(const juce::String &slotId) const;
  void rebuildGraph();
  void notifyChanged(bool notify);

  std::vector<std::shared_ptr<Entry>> inserts_;
  juce::AudioProcessorGraph graph_;
  std::shared_ptr<std::atomic<float>> gainLinear_;
  std::atomic<float> gainDb_{0.0f};
  std::atomic<float> peakDb_{-120.0f};
  std::atomic<int> latencySamples_{0};
  double sampleRate_ = 44100.0;
  int blockSize_ = 512;
  std::atomic<bool> prepared_{false};
  juce::MidiBuffer midiScratch_;
  ChangeCallback onChanged_;
  std::shared_ptr<std::atomic<bool>> alive_ =
      std::make_shared<std::atomic<bool>>(true);
};

} // namespace fiddle
