#pragma once

#include "../AudioSharedMemory.h"
#include "DebugWindow.h"
#include "DoricoInstrumentBrowser.h"
#include "ExpressionMapLibrary.h"
#include "FiddleDatabase.h"
#include "HistoryWindow.h"

#include "InstrumentMapper.h"
#include "JsTestBridge.h"
#include "LibraryManagerWindow.h"
#include "LuaPlugin.h"
#include "MasterInstrumentList.h"
#include "MidiTcpServer.h"
#include "MixerModel.h"
#include "NoteStreamTracker.h"
#include "PluginScanner.h"

#include "HarmonicAnalysisService.h"
#include "MessageRouter.h"
#include "MetronomeTempoTracker.h"
#include "StateManager.h"
#include "SubnoteGenerator.h"
#include "UndoActions.h"
#include "WebViewBridge.h"
#include "midi_event.pb.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <optional>

namespace fiddle {

class ExpressionMapCommandService;
class ExpressionMapJsHandlers;
class MixerCommandService;
class MixerJsHandlers;
class MasterAudioCommandService;
class MasterAudioJsHandlers;
class PluginCommandService;
class PluginJsHandlers;

class MainComponent : public juce::Component,
                      private juce::Timer,
                      public juce::AudioIODeviceCallback {
public:
  MainComponent();
  ~MainComponent() override;

  /// Save current state (commit to current branch)
  void saveConfig();

  /// Toggle debug window visibility.
  void toggleDebugWindow();

  /// Check if debug window is visible.
  bool isDebugWindowVisible() const;

  /// Toggle history window visibility (called from View menu).
  void toggleHistoryWindow();

  /// Check if history window is visible.
  bool isHistoryWindowVisible() const;

  /// Toggle library manager window visibility (called from View menu).
  void toggleLibraryManagerWindow();

  /// Check if library manager window is visible.
  bool isLibraryManagerWindowVisible() const;

  /// Save main window position/size to database.
  void saveMainWindowGeometry(int x, int y, int w, int h);

  /// Save debug window geometry + visibility to database.
  void saveDebugWindowGeometry();

  /// Save library manager window geometry + visibility to database.
  void saveLibraryManagerWindowGeometry();

  /// Restore main window geometry from database.
  /// Returns the stored bounds (or defaults if not found).
  juce::Rectangle<int> restoreMainWindowGeometry();

  /// Get the MIDI TCP server for disconnect control
  MidiTcpServer *getMidiServer() { return server.get(); }

  /// Push current status (branch name, dirty state) to connected plugin.
  void pushConfigStatus();

  void paint(juce::Graphics &) override;
  void resized() override;

  void audioDeviceIOCallbackWithContext(
      const float *const *inputChannelData, int numInputChannels,
      float *const *outputChannelData, int numOutputChannels, int numSamples,
      const juce::AudioIODeviceCallbackContext &context) override;
  void audioDeviceAboutToStart(juce::AudioIODevice *device) override;
  void audioDeviceStopped() override;

private:
  MessageRouter jsRouter_;
  WebViewBridge webViewBridge_;

  juce::AudioDeviceManager deviceManager;
  DoricoInstrumentBrowser instrumentBrowser_;
  MasterInstrumentList masterList_;
  std::unique_ptr<fiddle::MidiTcpServer> server;
  ExpressionMap expressionMap;
  NoteStreamTracker noteTracker;
  SubnoteGenerator subnoteGenerator;
  InstrumentMapper instrumentMapper_;
  PluginScanner pluginScanner_;
  MixerModel mixer_;
  ExpressionMapLibrary xmapLibrary_;
  UndoManager undoManager_;
  std::unique_ptr<MixerCommandService> mixerCommandService_;
  std::unique_ptr<MixerJsHandlers> mixerJsHandlers_;
  std::unique_ptr<MasterAudioCommandService> masterAudioCommandService_;
  std::unique_ptr<MasterAudioJsHandlers> masterAudioJsHandlers_;
  FiddleDatabase db_;
  std::unique_ptr<PluginCommandService> pluginCommandService_;
  std::unique_ptr<PluginJsHandlers> pluginJsHandlers_;
  std::unique_ptr<ExpressionMapCommandService> expressionMapCommandService_;
  std::unique_ptr<ExpressionMapJsHandlers> expressionMapJsHandlers_;
  std::unique_ptr<versioning::VersionStore> versionStore_;
  StateManager stateManager_;
  LuaPluginCatalog luaCatalog_;

  std::atomic<bool> isTransportStarted_{false};

  /// Global harmonic analysis service (joint key/chord HMM).
  /// Owned here; MixerModel holds a raw ptr.
  HarmonicAnalysisService harmonicService_;

  double getDelayedTriggerTimeMs() {
    // If transport is stopped, trigger immediately (no delay)
    // so any flushed NoteOffs or CCs trigger immediately instead of ghosting.
    if (!isTransportStarted_.load(std::memory_order_relaxed)) {
      return 0.0;
    }
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    const double compensatedDelayMs =
        juce::jmax(0.0, static_cast<double>(mixer_.getPlaybackDelayMs()) -
                            40.0 - mixer_.masterLatencyMs());
    return nowMs + compensatedDelayMs;
  }

  /// Live BPM from FiddleNative ProcessContext (arrives via TempoEvent).
  /// Default 120.0 before first TempoEvent is received.
  std::atomic<double> currentBpm_{120.0};

  /// Metronome-based tempo tracker.  Derives BPM and time signature from
  /// Dorico's click track routed to the reserved channel (port 0, channel 0).
  MetronomeTempoTracker metronomeTracker_;

  /// The reserved MIDI slot for the metronome click (flatIndex 0).
  /// Port/channel are 0-based; Dorico's protobuf channel field is 1-based,
  /// so incoming events on channel==1 correspond to kMetronomeChannel==0.
  static constexpr int kMetronomePort = 0;
  static constexpr int kMetronomeChannel = 0;

  std::unique_ptr<fiddle::JsTestBridge> jsTestBridge_;
  AudioSharedMemory audioSharedMemory_{true}; // True = Producer

  uint64_t lastSampleTime = 0;
  uint32_t lastSystemTime = 0;

  /// The branch ID (UUID) of the currently checked-out branch.
  /// Set when VersionStore is initialized; updated on checkoutBranch.
  std::string currentBranchId_;

  /// The version ID (UUID) of the currently loaded version.
  /// Tracks which exact version the mixer reflects at any moment.
  std::string currentVersionId_;

  /// True when the mixer is displaying a historical (non-HEAD) version.
  /// While detached: Dorico blob updates are suppressed; Save is disabled.
  bool isDetached_ = false;

  struct DoricoProjectRequest {
    std::string legacyBranchName;
    versioning::BranchId branchId;
    versioning::VersionId versionId;
  };
  std::optional<DoricoProjectRequest> pendingDoricoProject_;
  bool projectRestoreReady_ = false;

  /// Debug window (created eagerly, visibility toggled from View menu)
  std::unique_ptr<DebugWindow> debugWindow_;

  /// History window (lazy instantiated)
  std::unique_ptr<HistoryWindow> historyWindow_;
  bool historyWindowLoaded_ = false;

  /// Library Manager window (lazy instantiated)
  std::unique_ptr<LibraryManagerWindow> libraryManagerWindow_;
  bool libraryManagerWindowLoaded_ = false;

  /// Throttle state for scheduleStateRebuild() — max once per second.
  uint32_t lastStateRebuildMs_ = 0;
  bool stateRebuildPending_ = false;

  /// Initialization splash screen state
  bool initComplete_ = false;
  bool migratedToDag_ = false;
  juce::StringArray initMessages_;
  void addInitMessage(const juce::String &msg);
  void initializeApp();
  void initExpressionMaps();
  void initInstrumentBrowser();
  void initPlaceholder();
  void initLuaPlugins();
  void initExpressionMapLibrary();
  void initMidiServer();
  void initAudioDevice();
  void initDatabase();
  void migrateLegacyLibraryPatches();
  void initPluginsAndStrips();
  void timerCallback() override;

  void setupJsHandlers();

  /// Send a typed message to all ready WebViews via window.__dispatchFromCpp.
  /// This is the preferred API — avoid raw broadcastJavascript where possible.
  void broadcastMessage(const juce::String &type,
                        const juce::var &data = juce::var());
  void pushLogMessage(const juce::String &msg, bool isError = false);
  void pushMixerState(bool markDirty = true);
  void pushMasterAudioState();
  void pushChairState();
  void pushLayerCatalog();
  void syncMixerToLayers();
  bool instantiateLayer(const LayerRow &layer, const ChairRow &chair,
                        const LibraryPatchRow *patch,
                        const juce::String &libraryName);
  void installChairPlaybackTemplate();
  void masterAudioChanged();
  void pushToDebugWindow(const juce::String &js);

  void pushEventToWebView(const fiddle::MidiEvent &event);
  void pushSubnoteToWebView(const fiddle::Subnote &subnote);
  void pushBranches();
  void pushDagHistory();
  /// Broadcast the current version ID to all ready WebViews.
  void pushCurrentVersion();

  /// Save all strips to SQLite (called after every mutation).
  void saveAllStripsToDB();
  void saveMasterAudioToDB();

  /// Load strips from SQLite into the mixer.
  void loadStripsFromDB();
  void restoreMasterAudio(const MasterAudioSnapshot &snapshot,
                          bool publishWhenLoaded = true);
  void restoreMasterAudio(const versioning::GlobalState &state,
                          bool publishWhenLoaded = true);
  void restoreMasterAudio(const RestoredProjectState &state,
                          bool publishWhenLoaded = true);

  /// Restore a versioned mixer snapshot and its plug-in state.
  void applyVersionState(const versioning::FiddleState &state);

  /// Load one stored version. Dorico may select an older version and still
  /// associate it with its saved branch; History checkouts remain detached.
  bool loadStoredVersion(const versioning::VersionId &versionId,
                         const versioning::BranchId &branchId,
                         bool selectBranchWhenDetached);

  /// Select the current head of an existing branch.
  bool checkoutBranchById(const versioning::BranchId &branchId);

  /// Resolve and load branch/version identity received from a Dorico project.
  void restoreDoricoProject(const std::string &legacyBranchName,
                            const versioning::BranchId &branchId,
                            const versioning::VersionId &versionId);

  /// Commit dirty state requested by Dorico's VST3 getState callback and send
  /// a correlated response containing the exact persisted identity.
  void handleDoricoSaveRequest(uint64_t requestId);

  /// Save a single strip to the database by index.
  void saveStripToDB(const juce::String &stripId);

  /// Schedule a background rebuild of the shadow state blob.
  void scheduleStateRebuild();

  /// Poll MixerStrip plug-in parameters for persistent changes (called from
  /// the timer). Opaque serialized state is intentionally not compared because
  /// some instruments include volatile playback data in it.
  void pollPluginStateChanges();

  /// Consume allocation-free AudioProcessorListener flags on the message
  /// thread and update dirty state/state caches.
  void processPluginChangeNotifications(bool suppressPlaybackChanges);

  /// Combine transport state, post-stop settling, and recent raw MIDI activity
  /// (needed because Dorico note audition has no transport start/stop).
  bool shouldSuppressPluginChanges(uint32_t now);

  /// Record the current persistent plug-in parameters. Returns true only when
  /// a previously observed instance has changed.
  bool observeStripPluginFingerprint(MixerStrip &strip);

  /// Establish clean baselines for every currently loaded strip plug-in.
  void captureStripPluginFingerprints();

  /// Refresh the stable hosted-slot ID after assigning/restoring strip.id.
  void setupStripPluginSlot(MixerStrip &strip);

  /// Restore an instrument or retain an explicit missing-slot placeholder.
  void restoreStripPlugin(MixerStrip &strip, int pluginUid,
                          const juce::MemoryBlock &state);

  /// Counter for throttling plugin state polls (20ms timer × 100 = 2s).
  int pluginPollCounter_ = 0;

  /// MIDI-mapped VST3 parameters can continue settling briefly after Dorico
  /// stops. Ordinary callbacks are ignored through this timestamp.
  uint32_t pluginPlaybackSettleUntilMs_ = 0;
  bool pluginPlaybackSettling_ = false;
  bool pluginChangesWereSuppressed_ = false;
  std::atomic<uint32_t> lastPluginPerformanceActivityMs_{0};

  struct StripPluginFingerprint {
    int pluginUid = 0;
    uint64_t parameters = 0;
  };

  /// Persistent-parameter fingerprints for each hosted instrument instance.
  std::map<juce::String, StripPluginFingerprint> pluginFingerprints_;

  /// Plugin UIDs that fire AudioProcessorListener callbacks.
  /// Retained as compatibility telemetry; polling still verifies all plug-ins
  /// because callback behavior can vary between versions and operations.
  std::set<int> listenerCapableUids_;

  /// Fingerprint of chairs when the template was last installed.
  /// Used to determine if "Install Playback Template" should be enabled.
  juce::String lastInstalledFingerprint_;

  /// Compute a fingerprint from the explicit Dorico chairs in the DB.
  juce::String computeChairFingerprint();

  /// Broadcast whether the playback template is out-of-date to all UIs.
  void broadcastTemplateDirty();

  std::mutex logMutex;
  std::vector<std::pair<juce::String, bool>> logQueue;

  /// Post a callback to the message thread, guarded against destruction.
  /// If this MainComponent is destroyed before the callback fires, it is
  /// silently skipped. This prevents use-after-free crashes during config
  /// switches where the old MainComponent is destroyed while async callbacks
  /// are still pending.
  template <typename Fn> void safeCallAsync(Fn &&fn) {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync(
        [safeThis, f = std::forward<Fn>(fn)]() mutable {
          if (safeThis != nullptr)
            f();
        });
  }

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace fiddle
