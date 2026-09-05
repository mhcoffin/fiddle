#pragma once

#include "AnnotationRecord.h"
#include "AnnotatorChain.h"
#include "ExpressionMapData.h"
#include "HostedPluginSlot.h"
#include "IncomingSwitchTracker.h"
#include "MidiCaptureLog.h"
#include "RealtimeMidiScheduler.h"
#include "StripAudioEngine.h"
#include <atomic>
#include <deque>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <set>
#include <vector>

namespace fiddle {

class LuaPlugin;
/// A single mixer channel strip. Instrument plug-in lifecycle is delegated to
/// a reusable HostedPluginSlot. Identified by a unique string ID.
struct MixerStrip {

  MixerStrip();

  ~MixerStrip();

  juce::String id;
  juce::String library; // User-supplied VST library label (e.g. "SSP")
  juce::String family;  // Instrument family (e.g. "Strings", "Brass")
  bool isSolo = true;   // true = solo player, false = section

  // Explicit chair/layer identity. Empty for transitional free-standing
  // strips created by the legacy mixer workflow.
  juce::String chairId;
  juce::String patchId;
  juce::String layerName;

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
  void setPluginBypassed(bool bypassed) noexcept;
  [[nodiscard]] bool isPluginBypassed() const noexcept;
  [[nodiscard]] HostedPluginStatus pluginStatus() const noexcept;
  [[nodiscard]] const PluginCompatibility &pluginCompatibility() const noexcept;

  /// Update the stable instrument-slot ID after assigning/restoring strip.id.
  void updatePluginSlotId();

  /// Consume a listener notification on the message thread.
  [[nodiscard]] bool consumePluginChangeNotification() noexcept;

  /// Consume an explicit editor/needs-save notification separately from
  /// ordinary parameter callbacks, which may be caused by incoming MIDI.
  [[nodiscard]] bool consumePluginExplicitEditNotification() noexcept;

  /// Consume an ambiguous non-parameter state-change notification separately
  /// so playback can suppress it without losing stopped-state edits.
  [[nodiscard]] bool
  consumePluginNonParameterStateChangeNotification() noexcept;

  /// Stable fingerprint of the hosted instrument's persistent parameter
  /// values. This deliberately excludes volatile runtime data that some
  /// instruments include in getStateInformation() during playback.
  [[nodiscard]] uint64_t pluginParameterFingerprint() const;

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

  // Ring buffer of recent input and decision records for Note Inspector UI.
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

  /// Preserve an unavailable instrument's identity and serialized state.
  void markPluginMissing(int uid, const juce::MemoryBlock &state,
                         const juce::String &error);

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
  static constexpr uint64_t packInputAssignment(int port,
                                                int channel) noexcept {
    return (static_cast<uint64_t>(static_cast<uint32_t>(port)) << 32) |
           static_cast<uint32_t>(channel);
  }

  std::atomic<bool> active_{true};
  std::atomic<bool> muted_{false};
  std::atomic<bool> soloed_{false};
  std::atomic<uint64_t> inputAssignment_{packInputAssignment(-1, -1)};

  HostedPluginSlot instrumentSlot_{PluginSlotRole::instrument};
  StripAudioEngine audioEngine_;

  RealtimeMidiScheduler midiScheduler_;
  juce::MidiBuffer processMidiBuffer_; // audio thread only, preallocated

  std::atomic<double> currentSampleRate_{44100.0};
  std::atomic<int> currentBlockSize_{512};
  std::atomic<float> gainDb_{0.0f};
  std::atomic<float> peakDb_{-120.0f};
  std::atomic<float> peakHoldDb_{-120.0f};

};

} // namespace fiddle
