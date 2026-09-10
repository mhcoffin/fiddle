#pragma once

#include "AudioInsertSnapshot.h"
#include "HostedPluginSlot.h"

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

namespace fiddle {

/// Owns the independent post-instrument audio path for one mixer strip.
///
/// Phase 4A starts with the existing strip fader as the only processing node.
/// Ordered insert nodes can be added between the graph input and fader without
/// changing MixerStrip's render contract or the shared Master path.
class StripAudioEngine {
public:
  using ChangeCallback = std::function<void()>;
  using LoadCompletion =
      std::function<void(bool success, const juce::String &error)>;

  StripAudioEngine();
  ~StripAudioEngine();

  StripAudioEngine(const StripAudioEngine &) = delete;
  StripAudioEngine &operator=(const StripAudioEngine &) = delete;

  void prepareToPlay(double sampleRate, int blockSize);
  void releaseResources();

  /// Process one strip's stereo output in place. effectiveGain already
  /// includes its fader and the current active/mute/solo decision.
  void processBlock(juce::AudioBuffer<float> &audio, float effectiveGain);

  void setOnChanged(ChangeCallback callback);
  void setOnEditorVisibilityChanged(ChangeCallback callback);

  [[nodiscard]] int insertCount(StripInsertPosition position) const noexcept;
  [[nodiscard]] int indexOf(const juce::String &slotId,
                            StripInsertPosition position) const noexcept;
  [[nodiscard]] std::optional<StripInsertPosition>
  positionOf(const juce::String &slotId) const;
  [[nodiscard]] std::optional<AudioInsertSnapshot>
  snapshot(const juce::String &slotId, bool captureLiveState = true) const;
  [[nodiscard]] StripAudioSnapshot snapshotAll(bool captureLiveState = true) const;

  bool insert(const AudioInsertSnapshot &snapshot, StripInsertPosition position,
              int index, juce::AudioPluginFormatManager &formatManager,
              LoadCompletion completion = nullptr, bool notify = true);
  bool insertProcessor(const AudioInsertSnapshot &snapshot,
                       StripInsertPosition position, int index,
                       std::unique_ptr<juce::AudioProcessor> processor,
                       bool notify = true);
  bool remove(const juce::String &slotId, bool notify = true);
  void clear(bool notify = true);
  bool move(const juce::String &slotId, StripInsertPosition newPosition,
            int newIndex, bool notify = true);
  bool setBypassed(const juce::String &slotId, bool bypassed,
                   bool notify = true);
  bool showEditor(const juce::String &slotId, const juce::String &stripName);
  bool toggleEditor(const juce::String &slotId, const juce::String &stripName);

  /// Message-thread maintenance for state listeners and dynamic latency.
  bool consumePluginChanges(bool suppressPlaybackChanges = false);
  /// Message-thread display notification, independent of persistent edits.
  bool consumeLatencyDisplayChange() noexcept {
    const bool changed = latencyDisplayChanged_;
    latencyDisplayChanged_ = false;
    return changed;
  }
  bool refreshPluginStateCaches();
  void captureParameterFingerprints();

  [[nodiscard]] juce::var toJson() const;
  void appendPluginTimings(juce::Array<juce::var> &rows,
                           const juce::String &owner, double now);

  [[nodiscard]] int latencySamples() const noexcept;
  [[nodiscard]] double latencyMs() const noexcept;

private:
  struct Entry {
    juce::String id;
    juce::PluginDescription description;
    std::shared_ptr<HostedPluginSlot> hosted;
    int graphLatencySamples = 0;
    std::optional<uint64_t> parameterFingerprint;
  };

  using Rack = std::vector<std::shared_ptr<Entry>>;

  [[nodiscard]] Rack &rack(StripInsertPosition position) noexcept;
  [[nodiscard]] const Rack &rack(StripInsertPosition position) const noexcept;
  [[nodiscard]] std::shared_ptr<Entry> find(const juce::String &slotId) const;
  void rebuildGraph();
  void notifyChanged(bool notify);

  Rack preFaderInserts_;
  Rack postFaderInserts_;
  juce::AudioProcessorGraph graph_;
  std::shared_ptr<std::atomic<float>> gainLinear_;
  std::atomic<int> latencySamples_{0};
  std::atomic<bool> prepared_{false};
  std::atomic<double> sampleRate_{44100.0};
  int blockSize_ = 512;
  juce::MidiBuffer midiScratch_;
  ChangeCallback onChanged_;
  ChangeCallback onEditorVisibilityChanged_;
  bool latencyDisplayChanged_ = false;
  std::shared_ptr<std::atomic<bool>> alive_ =
      std::make_shared<std::atomic<bool>>(true);
};

} // namespace fiddle
