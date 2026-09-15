#pragma once

#include "../AudioDiagnostics.h"
#include "AgentControlServer.h"
#include "DebugWindow.h"
#include "DoricoInstrumentBrowser.h"
#include "ExpressionMapLibrary.h"
#include "FiddleDatabase.h"
#include "HistoryWindow.h"

#include "InstrumentMapper.h"
#include "JsTestBridge.h"
#include "LibraryPatchPreviewHost.h"
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
class GroupBusCommandService;
class GroupBusJsHandlers;
class GroupBus;
class MasterAudioCommandService;
class MasterAudioJsHandlers;
class StripAudioCommandService;
class StripAudioJsHandlers;
class PluginCommandService;
class PluginJsHandlers;
class ProjectRestoreService;
class AudioDeviceSettings;
class RenderAheadEngine;

class MainComponent : public juce::Component,
                      private juce::Timer,
                      public juce::AudioIODeviceCallback {
public:
  MainComponent();
  ~MainComponent() override;

  /// Save relative to the loaded version; historical edits fork automatically.
  bool saveConfig(const std::optional<std::string> &newBranchName = std::nullopt);

  /// Toggle debug window visibility.
  void toggleDebugWindow();

  /// Check if debug window is visible.
  bool isDebugWindowVisible() const;

  /// Toggle history window visibility (called from View menu).
  void toggleHistoryWindow();

  /// Check if history window is visible.
  bool isHistoryWindowVisible() const;

  /// Toggle library manager window visibility.
  void toggleLibraryManagerWindow();

  /// Create the Library Manager if needed, otherwise unminimize and focus it.
  void showLibraryManagerWindow();
  void showAudioSettings();

  /// Check if library manager window is visible.
  bool isLibraryManagerWindowVisible() const;
  bool hasUnsavedLibraryDraft() const { return libraryDraftDirty_; }

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
  std::unique_ptr<AudioDeviceSettings> audioSettings_;
  DoricoInstrumentBrowser instrumentBrowser_;
  MasterInstrumentList masterList_;
  std::unique_ptr<fiddle::MidiTcpServer> server;
  ExpressionMap expressionMap;
  NoteStreamTracker noteTracker;
  SubnoteGenerator subnoteGenerator;
  InstrumentMapper instrumentMapper_;
  PluginScanner pluginScanner_;
  MixerModel mixer_;
  LibraryPatchPreviewHost libraryPatchPreviewHost_;
  ExpressionMapLibrary xmapLibrary_;
  UndoManager undoManager_;
  UndoManager libraryUndoManager_;
  bool libraryDraftDirty_ = false;
  void pushLibraryCatalogHistory();
  void pushUndoState();
  void pushLibraryLayerStatus();
  void pushProjectSettings();
  std::unique_ptr<MixerCommandService> mixerCommandService_;
  std::unique_ptr<MixerJsHandlers> mixerJsHandlers_;
  std::unique_ptr<GroupBusCommandService> groupBusCommandService_;
  std::unique_ptr<GroupBusJsHandlers> groupBusJsHandlers_;
  std::unique_ptr<MasterAudioCommandService> masterAudioCommandService_;
  std::unique_ptr<MasterAudioJsHandlers> masterAudioJsHandlers_;
  std::unique_ptr<StripAudioCommandService> stripAudioCommandService_;
  std::unique_ptr<StripAudioJsHandlers> stripAudioJsHandlers_;
  FiddleDatabase db_;
  std::unique_ptr<PluginCommandService> pluginCommandService_;
  std::unique_ptr<PluginJsHandlers> pluginJsHandlers_;
  std::unique_ptr<ExpressionMapCommandService> expressionMapCommandService_;
  std::unique_ptr<ExpressionMapJsHandlers> expressionMapJsHandlers_;
  std::unique_ptr<versioning::VersionStore> versionStore_;
  StateManager stateManager_;
  LuaPluginCatalog luaCatalog_;
  std::unique_ptr<ProjectRestoreService> projectRestoreService_;

  std::atomic<bool> isTransportStarted_{false};
  juce::String connectionWarning_;

  /// Global harmonic analysis service (joint key/chord HMM).
  /// Owned here; MixerModel holds a raw ptr.
  HarmonicAnalysisService harmonicService_;

  double getDelayedTriggerTimeMs();
  int effectivePlaybackDelayMs() const;

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
  AudioRenderDiagnostics audioDiagnostics_;
  std::unique_ptr<RenderAheadEngine> renderAhead_;
  std::unique_ptr<AgentControlServer> agentControlServer_;
  std::atomic<int> reportedPlaybackDelayMs_{1000};
  juce::String renderAheadError_;
  AudioRenderDiagnostics::Snapshot latestAudioDiagnostics_;
  std::atomic<bool> audioDeviceRunning_{false};
  // Accessed only on the message thread; native reports arrive via safeCallAsync.
  MidiEvent::AudioDiagnosticsEvent returnDiagnostics_;
  double returnDiagnosticsReceivedMs_ = 0;
  double lastDiagnosticsPushMs_ = 0;
  void pushAudioDiagnostics();
  void pushMixerMeters();
  bool meterUpdatePending_ = false; // Message-thread-only, one web evaluation in flight.
  bool diagnosticsUpdatePending_ = false;

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
  juce::String pendingGuidedLibraryId_;

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
  void initAgentControl();
  void timerCallback() override;

  void setupJsHandlers();
  AgentControlServer::Response
  handleAgentControlRequest(const juce::String &method,
                            const juce::var &params);
  juce::var agentStatus() const;
  juce::var agentLayerSnapshot(const MixerStrip &strip) const;
  juce::var agentMixerSnapshot();
  juce::var agentLayerResult(const juce::String &stripId,
                             bool changed);
  juce::var agentLibrarySetupSnapshot(const juce::String &libraryId);
  AgentControlServer::Response
  createAgentGuidedLibrary(const juce::var &params);
  void scanPersistedExpressionMapSources();
  void rememberExpressionMapSourceDirectory(const juce::File &directory);
  void agentMixerChanged();
  bool applyAgentUndoRedo(bool redo);

  /// Send a typed message to all ready WebViews via window.__dispatchFromCpp.
  /// This is the preferred API — avoid raw broadcastJavascript where possible.
  void broadcastMessage(const juce::String &type,
                        const juce::var &data = juce::var());
  void pushLogMessage(const juce::String &msg, bool isError = false);
  void pushMixerState(bool markDirty = true);
  void pushMasterAudioState();
  void pushGroupBusState();
  void pushChairState();
  void chairEditChanged(bool success);
  void pushLayerCatalog();
  void syncMixerToLayers();
  bool instantiateLayer(const LayerRow &layer, const ChairRow &chair,
                        const LibraryPatchRow *patch,
                        const juce::String &libraryName);
  /// Apply only the library-owned portion of a saved layer row to its live
  /// mixer strip, leaving routing and mixer controls unchanged.
  bool refreshLayersFromPatch(const LibraryPatchRow &patch, const std::vector<LayerRow> &layers);
  void layerLibrarySetupSettled(const juce::String &stripId);
  void installChairPlaybackTemplate();
  void masterAudioChanged();
  void stripAudioChanged(const juce::String &stripId);
  void groupBusAudioChanged(const juce::String &busId);
  void pushToDebugWindow(const juce::String &js);

  void pushEventToWebView(const fiddle::MidiEvent &event);
  void pushSubnoteToWebView(const fiddle::Subnote &subnote);
  void pushBranches();
  void pushDagHistory();
  /// Broadcast the current version ID to all ready WebViews.
  void pushCurrentVersion();

  /// Save all strips to SQLite (called after every mutation).
  void saveAllStripsToDB(bool captureLiveState = true);
  void saveMasterAudioToDB(bool captureLiveState = true);

  /// Load strips from SQLite into the mixer.
  void loadStripsFromDB();
  void restoreMasterAudio(const MasterAudioSnapshot &snapshot,
                          bool publishWhenLoaded = true);
  void restoreMasterAudio(const RestoredProjectState &state,
                          bool publishWhenLoaded = true);
  void restoreStripAudio(MixerStrip &strip,
                         const StripAudioSnapshot &snapshot,
                         bool publishWhenLoaded = true);
  void restoreStripAudio(MixerStrip &strip, const juce::MemoryBlock &state,
                         bool publishWhenLoaded = true);

  /// Restore a versioned mixer snapshot and its plug-in state.
  bool applyVersionState(const versioning::FiddleState &state);

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

  /// Rebase notifications/fingerprints generated by an undoable program
  /// selection so the timer does not record the same command as an external
  /// vendor-editor edit.
  void pluginProgramApplied(const juce::String &stripId);

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
  void setupGroupBusAudioCallbacks(GroupBus &bus);

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

  /// State restoration can emit delayed parameter/non-parameter callbacks.
  /// Ignore those briefly unless the plug-in reports an explicit editor
  /// gesture, while continually rebasing its parameter fingerprint.
  std::map<juce::String, uint32_t> pluginStateSettleUntilMs_;

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
