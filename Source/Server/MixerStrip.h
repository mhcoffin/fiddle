#pragma once

#include "../RealtimeObjectPublisher.h"
#include "AnnotationRecord.h"
#include "AnnotatorChain.h"
#include "ExpressionMapData.h"
#include "IncomingSwitchTracker.h"
#include "MidiCaptureLog.h"
#include "RealtimeMidiScheduler.h"
#include <atomic>
#include <deque>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <set>
#include <vector>

namespace fiddle {

class LuaPlugin;
class PluginEditorWindow;

/// A single mixer channel strip. Owns a plugin instance + optional editor
/// window. Identified by a unique string ID.
struct MixerStrip {

  MixerStrip();

  /// Lifetime sentinel: shared_ptr set to false when the strip is destroyed.
  /// The async loadPlugin callback captures a weak_ptr to this and checks
  /// liveness before touching any member — prevents use-after-free when a
  /// strip is removed from the mixer while a plugin load is still in flight.
  std::shared_ptr<bool> lifetimeToken_ = std::make_shared<bool>(true);

  ~MixerStrip();

  /// Listener that detects plugin parameter changes via the standard VST3
  /// notification path. When it fires, we mark the strip dirty and record
  /// the pluginUid as "listener-capable" so polling can skip it.
  struct PluginChangeListener : juce::AudioProcessorListener {
    juce::String stripId;
    int pluginUid = 0;
    /// Shared set of pluginUids known to fire listener callbacks.
    /// Populated by the listener, read by the polling timer.
    std::set<int> *listenerCapableUids = nullptr;
    /// Callback to mark Fiddle dirty (set by MainComponent).
    std::function<void()> onDirty;

    void audioProcessorParameterChanged(juce::AudioProcessor *, int,
                                        float) override;
    void audioProcessorChanged(
        juce::AudioProcessor *,
        const juce::AudioProcessorListener::ChangeDetails &d) override;
  };
  PluginChangeListener pluginChangeListener_;

  juce::String id;
  juce::String library; // User-supplied VST library label (e.g. "SSP")
  juce::String family;  // Instrument family (e.g. "Strings", "Brass")
  bool isSolo = true;   // true = solo player, false = section

  /// Immutable copy of the values shared with the audio thread.
  struct RealtimeState {
    bool active = true;
    bool muted = false;
    bool soloed = false;
    int inputPort = -1;
    int inputChannel = -1;
    float gainDb = 0.0f;
    float peakDb = -120.0f;
    float peakHoldDb = -120.0f;
  };

  [[nodiscard]] RealtimeState realtimeState() const noexcept;
  [[nodiscard]] bool isActive() const noexcept;
  void setActive(bool shouldBeActive) noexcept;
  [[nodiscard]] bool isMuted() const noexcept;
  void setMuted(bool shouldBeMuted) noexcept;
  [[nodiscard]] bool isSoloed() const noexcept;
  void setSoloed(bool shouldBeSoloed) noexcept;
  [[nodiscard]] int inputPort() const noexcept;
  [[nodiscard]] int inputChannel() const noexcept;
  [[nodiscard]] bool matchesInput(int port, int channel) const noexcept;
  void setInputAssignment(int port, int channel) noexcept;

  [[nodiscard]] float gainDb() const noexcept;
  void setGainDb(float gain) noexcept;
  [[nodiscard]] float peakDb() const noexcept;
  [[nodiscard]] float peakHoldDb() const noexcept;

  // Plugin
  int pluginUid = 0; // scanned plugin uniqueId (0 = none)

  /// Cached serialized plugin state. Updated on load, restore, and change
  /// detection. Used by buildStateBlob() to avoid calling getStateInformation()
  /// on every plugin for every blob rebuild.
  [[nodiscard]] juce::MemoryBlock cachedPluginState() const;
  [[nodiscard]] bool hasPlugin() const noexcept;
  bool capturePluginState(juce::MemoryBlock &destination) const;
  bool applyPluginState(const void *data, int sizeInBytes);
  bool setPluginProgram(int programIndex);

  /// Re-serialize the live plugin state into cachedPluginState_.
  void refreshPluginStateCache();

  // Expression map
  std::shared_ptr<ExpressionMapData> expressionMap;
  juce::String expressionMapPath; // file path for persistence

  /// Set (or clear) the expression map for this strip.
  /// Automatically creates/destroys the ExpressionMapAnnotator
  /// and rebuilds the full annotator chain.
  void setExpressionMap(std::shared_ptr<ExpressionMapData> em);

  // Lua plugins installed on this strip (ordered chain).
  std::vector<std::shared_ptr<LuaPlugin>> luaPlugins;

  /// Add a Lua plugin to this strip and rebuild the chain.
  void addLuaPlugin(std::shared_ptr<LuaPlugin> plugin);

  /// Remove a Lua plugin by index and rebuild the chain.
  void removeLuaPlugin(size_t index);

  /// Insert a Lua plugin at a specific index (for undo restore).
  void insertLuaPlugin(size_t index, std::shared_ptr<LuaPlugin> plugin);

  /// Get just the filenames of installed Lua plugins (for serialization).
  /// Returns basenames only (e.g. "force_staccato.lua"), not full paths.
  std::vector<std::string> getLuaPluginFileNames() const;

  /// Rebuild the annotator chain from the Lua plugins + expression map.
  /// Lua plugins run first (in order), then the ExpressionMapAnnotator.
  void rebuildAnnotatorChain();

  // Annotator chain: Lua plugins → ExpressionMapAnnotator.
  // Defaults to a chain with just a PassthroughAnnotator.
  std::unique_ptr<AnnotatorChain> annotator;

  // Incoming switch tracker: reverse-matches Dorico MIDI to switch names.
  std::unique_ptr<IncomingSwitchTracker> incomingTracker;

  // Ring buffer of recent annotation decision records for Note Inspector UI.
  std::deque<AnnotationRecord> recentAnnotations_;
  static constexpr size_t kMaxAnnotations = 64;

  void pushAnnotation(const AnnotationRecord &rec);

  void clearAnnotations();

  // MIDI capture logs for expression map comparison.
  // incomingCapture: raw MIDI from Dorico (ground truth when EM is in Dorico).
  // emittedCapture:  MIDI emitted to the hosted VST by FiddleServer.
  MidiCaptureLog incomingCapture;
  MidiCaptureLog emittedCapture;

  /// Currently held (latching) keyswitch notes per channel.
  /// Key: {channel, noteNumber}. Keyswitches are held until articulation
  /// changes, matching Dorico's latching behavior for VSTs.
  struct HeldKS {
    int channel, noteNumber;
    bool operator<(const HeldKS &o) const {
      return std::tie(channel, noteNumber) < std::tie(o.channel, o.noteNumber);
    }
    bool operator==(const HeldKS &o) const {
      return channel == o.channel && noteNumber == o.noteNumber;
    }
  };
  std::set<HeldKS> heldKeyswitchNotes;

  /// Serialize one AnnotationRecord to a juce::var (DynamicObject).
  static juce::var annotationRecordToVar(const AnnotationRecord &r);

  /// Serialize the entire ring buffer to a JSON string.
  juce::String getAnnotationRecordsAsJson() const;

  void prepareToPlay(double sampleRate, int blockSize);

  void addDelayedMessage(double triggerTime, const juce::MidiMessage &msg);

  /// Clear all pending delayed messages (used during graceful stop).
  void clearDelayedMessages();

  /// Send All Notes Off + All Sound Off + Reset All Controllers to the plugin.
  /// Also clears pending delayed messages to prevent stale notes from firing.
  void allNotesOff();

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime,
                    bool anySoloed = false);

  /// Load a plugin from a description. Must be called on the message thread.
  void loadPlugin(const juce::PluginDescription &desc,
                  juce::AudioPluginFormatManager &formatManager,
                  std::function<void(bool)> onComplete = nullptr);

  /// Unload the plugin and close editor.
  void unloadPlugin();

  /// Destroy unpublished plugin runtimes once no audio callback can reference
  /// them. Call periodically from the message thread.
  void reclaimRetiredPluginRuntimes();

  [[nodiscard]] uint64_t droppedMidiMessageCount() const noexcept;

  /// Show the editor window (create if needed).
  void showEditor();

  /// Serialize to JSON var.
  juce::var toJson() const;

private:
  struct PluginRuntime {
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::AudioBuffer<float> tempBuffer;
  };

  static constexpr uint64_t packInputAssignment(int port,
                                                int channel) noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(port)) << 32) |
           static_cast<uint32_t>(channel);
  }

  std::atomic<bool> active_{true};
  std::atomic<bool> muted_{false};
  std::atomic<bool> soloed_{false};
  std::atomic<uint64_t> inputAssignment_{packInputAssignment(-1, -1)};

  /// Message-thread alias for the currently published runtime's plugin.
  /// Ownership remains in PluginRuntime and reclamation waits for audio
  /// readers.
  juce::AudioPluginInstance *pluginInstance_ = nullptr;
  std::unique_ptr<PluginEditorWindow> editorWindow_;
  juce::MemoryBlock cachedPluginState_;

  RealtimeMidiScheduler midiScheduler_;
  juce::MidiBuffer processMidiBuffer_; // audio thread only, preallocated

  std::atomic<double> currentSampleRate_{44100.0};
  std::atomic<int> currentBlockSize_{512};
  std::atomic<float> gainDb_{0.0f};
  std::atomic<float> peakDb_{-120.0f};
  std::atomic<float> peakHoldDb_{-120.0f};

  RealtimeObjectPublisher<PluginRuntime> pluginRuntime_;

  void publishPluginRuntime(std::unique_ptr<PluginRuntime> runtime);
};

} // namespace fiddle
