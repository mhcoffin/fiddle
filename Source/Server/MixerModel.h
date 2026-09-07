#pragma once

#include "../RealtimeObjectPublisher.h"
#include "MasterAudioEngine.h"
#include "GroupBus.h"
#include "MixerStrip.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <mutex>
#include <set>
#include <vector>

namespace fiddle {

class HarmonicAnalysisService;
class MasterInstrumentList;

/// Lightweight snapshot of strip state for undo/redo.
struct StripSnapshot {
  juce::String id, library, family, directOutputBusId;
  bool isSolo = true;
  bool muted = false;
  bool soloed = false;
  int inputPort = -1, inputChannel = -1;
  int pluginUid = 0;
  float gainDb = 0.0f;
  std::string expressionMapEntityID;
  int index = 0; // position in strips_ vector
};

/// Manages an ordered list of MixerStrips. Owns a shared
/// AudioPluginFormatManager for plugin instantiation.
class MixerModel : public juce::Timer {
  struct StripRoute {
    MixerStrip *strip = nullptr;
    GroupBus *destination = nullptr; // nullptr means Master
  };

  struct ActiveAudioGraph {
    // Flat strip list serves MIDI routing; stripRoutes adds resolved audio
    // destinations without making the audio thread inspect mutable strings.
    std::vector<MixerStrip *> strips;
    std::vector<StripRoute> stripRoutes;
    std::vector<GroupBus *> groupBuses;
  };

public:
  MixerModel();

  ~MixerModel() override;

  void clear();

  /// Add a new empty strip. Returns its ID.
  juce::String addStrip();

  /// Remove a strip by ID.
  bool removeStrip(const juce::String &id);

  /// Remove a strip by ID, returning its ownership (for undo).
  /// Does NOT unload the plugin. Caller is responsible.
  std::unique_ptr<MixerStrip> removeStripKeepAlive(const juce::String &id);

  /// Insert a strip at a specific index (for undo of remove).
  void insertStripAt(std::unique_ptr<MixerStrip> strip, int index);

  /// Get the index of a strip by ID (-1 if not found).
  [[nodiscard]] int stripIndex(const juce::String &id) const;

  /// Take a snapshot of a strip's restorable state.
  [[nodiscard]] StripSnapshot snapshotStrip(const juce::String &id) const;

  /// Insert a new strip right after the strip with the given ID,
  /// copying its input port/channel/family but with no plugin.
  juce::String duplicateStripAfter(const juce::String &afterId);

  /// Find a strip by ID (nullptr if not found).
  MixerStrip *getStrip(const juce::String &id);

  /// Get raw pointers to all strips (caller must not hold them long).
  std::vector<MixerStrip *> getAllStrips();

  /// Add a stereo group bus routed to Master. Returns its stable ID.
  juce::String addGroupBus(const juce::String &name);

  /// Restore/undo a fully constructed bus at a stable order position.
  void insertGroupBusAt(std::unique_ptr<GroupBus> bus, int index);

  /// Remove a group bus and repair every affected strip route to Master.
  bool removeGroupBus(const juce::String &id);

  /// Remove a bus without destroying it, for exact undo restoration. Routes
  /// targeting it are repaired to Master.
  std::unique_ptr<GroupBus>
  removeGroupBusKeepAlive(const juce::String &id);

  [[nodiscard]] int groupBusIndex(const juce::String &id) const;
  bool renameGroupBus(const juce::String &id, const juce::String &name);
  bool moveGroupBus(const juce::String &id, int newIndex);
  bool setGroupBusGain(const juce::String &id, float gainDb);
  bool setGroupBusMute(const juce::String &id, bool muted);
  bool setGroupBusSolo(const juce::String &id, bool soloed);

  GroupBus *getGroupBus(const juce::String &id);
  std::vector<GroupBus *> getAllGroupBuses();

  /// Route a strip to a group bus, or to Master when busId is empty.
  /// Missing bus IDs are rejected without changing the existing route.
  bool setStripDirectOutput(const juce::String &stripId,
                            const juce::String &busId);

  /// Republish routing and downstream latency after a bus rack changes.
  void refreshAudioRouting();

  [[nodiscard]] juce::String groupBusesToJson() const;

  /// Get the shared format manager (for plugin loading).
  juce::AudioPluginFormatManager &getFormatManager();

  MasterAudioEngine &masterAudio() noexcept { return masterAudio_; }
  const MasterAudioEngine &masterAudio() const noexcept { return masterAudio_; }

  [[nodiscard]] int masterLatencySamples() const noexcept {
    return masterAudio_.latencySamples();
  }
  [[nodiscard]] double masterLatencyMs() const noexcept {
    return masterAudio_.latencyMs();
  }

  /// Get all strips count.
  [[nodiscard]] int size() const;

  /// Serialize all strips to JSON array.
  [[nodiscard]] juce::String toJson() const;

  void processBlock(juce::AudioBuffer<float> &audioBuffer, double currentTime);

  void prepareToPlay(double sampleRate, int blockSize);

  /// Route incoming MIDI note event to matching strips (raw, no annotation).
  void routeNoteEvent(int port, int channel, const juce::MidiMessage &msg,
                      double triggerTime);

  /// Route incoming CC event to matching strips (immediate, no delay).
  void routeCCEvent(int port, int channel, const juce::MidiMessage &msg);

  // ── Annotator-aware routing ────────────────────────────────────────

  /// Convert a MidiInstruction proto to a juce::MidiMessage.
  /// @param noteChannel  1-based fallback channel when instruction channel==0.
  static juce::MidiMessage
  instructionToMidi(const fiddle::MidiInstruction &instr, int noteChannel);

  /// Record a MidiInstruction into a capture log.
  static void captureInstruction(MidiCaptureLog &log, double timeMs,
                                 const fiddle::MidiInstruction &instr,
                                 int noteChannel,
                                 const juce::String &label = {});

  /// Route a noteOn through each matching strip's annotator, scheduling
  /// pre_note instructions before the noteOn.
  void routeAnnotatedNoteOn(int port, int channel, fiddle::Note &note,
                            double triggerTimeMs);

  /// Route a noteOff through each matching strip's annotator, scheduling
  /// the noteOff followed by post_note instructions.
  void routeAnnotatedNoteOff(int port, int channel, fiddle::Note &note,
                             double triggerTimeMs);

  /// Release all latched keyswitches on every strip (called on transport stop).
  void releaseAllKeyswitches(double triggerTimeMs);

  /// Gracefully stop all sounding notes on transport stop.
  ///
  /// 1. Clears pending delayed messages (prevents stale note-ons from firing).
  /// 2. Releases latched keyswitches immediately.
  /// 3. Ramps CC1 (expression) from current value → 0 over @p releaseMs.
  /// 4. Sends note-OFF for each active note after the ramp completes.
  /// 5. Schedules All Sound Off (CC 120) shortly after to hard-kill any
  ///    lingering release tails.
  ///
  /// @param channelCC1  Map from Dorico channel (1-based) → current CC1 value.
  ///                    Channels not in the map default to 127.
  void gracefulStop(const std::vector<fiddle::Note> &activeNotes,
                    double releaseMs,
                    const std::map<int, uint8_t> &channelCC1 = {});

  /// Route a CC event through matching strip annotators.
  /// If the annotator's onCC returns false, the CC is not forwarded.
  void routeAnnotatedCC(int port, int channel, const fiddle::MidiEvent &event,
                        const juce::MidiMessage &msg, double triggerTimeMs);

  /// Record raw incoming MIDI from Dorico to matching strips.
  void recordIncomingMidi(int port, int channel, CapturedMidiEvent::Type type,
                          int p1, int p2, double timeMs);

  /// Panic: send All Notes Off to every strip.
  void allNotesOff();

  /// Panic: send All Notes Off only to strips matching port and channel.
  void panicStrips(int port, int channel);

  /// Set mute state on a strip by ID.
  bool setStripMute(const juce::String &id, bool mute);

  /// Set solo state on a strip by ID.
  bool setStripSolo(const juce::String &id, bool solo);

  /// Sync mixer strips to match the ensemble instrument list.
  /// Creates new strips for new instruments, removes strips whose
  /// port/channel no longer appears. Preserves existing plugin assignments.
  void syncStripsToInstruments(const MasterInstrumentList &masterList);

  /// Set the harmonic analysis service (owned by MainComponent, not
  /// MixerModel).
  void setHarmonicService(HarmonicAnalysisService *service);

private:
  // Only the main thread accesses these UI representations
  mutable std::mutex stripsMutex_;
  std::vector<std::unique_ptr<MixerStrip>> strips_;
  std::vector<std::unique_ptr<GroupBus>> groupBuses_;

  RealtimeObjectPublisher<ActiveAudioGraph> audioGraph_;

  // Garbage bin emptied by timer
  mutable std::mutex retiredStripMutex_;
  std::vector<std::unique_ptr<MixerStrip>> trashStrips_;
  std::vector<std::unique_ptr<GroupBus>> trashGroupBuses_;

  void commitAudioGraph();

  void timerCallback() override;
  juce::AudioPluginFormatManager formatManager_;
  MasterAudioEngine masterAudio_;
  double currentSampleRate_ = 44100.0;
  int currentBlockSize_ = 512;
  int playbackDelayMs_ = 1000;

  // Harmonic analysis service (optional). Owned by MainComponent.
  HarmonicAnalysisService *harmonicService_ = nullptr;

public:
  int getPlaybackDelayMs() const;
  void setPlaybackDelayMs(int ms);
};

} // namespace fiddle
