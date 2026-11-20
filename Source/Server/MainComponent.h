#pragma once

#include "NoteStartSettler.h"
#include "../AudioDiagnostics.h"
#include "AgentControlServer.h"
#include "DebugWindow.h"
#include "DebugUiFeed.h"
#include "DoricoInstrumentBrowser.h"
#include "ExpressionMapLibrary.h"
#include "ExpressionMapCommandService.h"
#include "LibraryMaps.h"
#include "ProjectEnvironment.h"
#include "FiddleDatabase.h"
#include "PlaybackTemplateRecord.h"
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
#include "ScoreTest.h"
#include "ToolWindow.h"

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
class BridgeMapCommands;
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

  /// Show/focus a named destination from either the native or web View menu.
  void showView(const juce::String &viewId);

  /// Toggle debug window visibility (retained for existing callers).
  void toggleDebugWindow();

  /// Check if debug window is visible.
  bool isDebugWindowVisible() const;

  /// Toggle history window visibility (called from View menu).
  void toggleHistoryWindow();
  void showHistoryWindow();

  /// Check if history window is visible.
  bool isHistoryWindowVisible() const;

  /// Toggle library manager window visibility.
  void toggleLibraryManagerWindow();

  /// Create the Library Manager if needed, otherwise unminimize and focus it.
  void showLibraryManagerWindow();
  void showSettings();

  /// Check if library manager window is visible.
  bool isLibraryManagerWindowVisible() const;
  bool hasUnsavedLibraryDraft() const { return libraryDraftDirty_; }

  /// Save main window position/size to database.
  void saveMainWindowGeometry(int x, int y, int w, int h);

  /// Save debug window geometry + visibility to database.
  void saveDebugWindowGeometry();
  void saveHistoryWindowGeometry();

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
  NoteStartSettler noteStartSettler_{[this](fiddle::Note &note, double triggerTimeMs) {
    mixer_.routeAnnotatedNoteOn((int)note.port(), (int)note.channel() - 1, note, triggerTimeMs);
  }};
  // Score tests (tests/score): records decoded notes between Arm and Done.
  scoretest::Recorder scoreTestRecorder_;
  juce::String scoreTestArmedId_;
  juce::var scoreTestLastRun_;
  juce::String scoreTestError_;
  LibraryPatchPreviewHost libraryPatchPreviewHost_;
  // Player windows an agent opens for the user to choose a preset
  // (AgentControl.cpp). A host of their own, so Library Manager's preview
  // housekeeping never closes them.
  LibraryPatchPreviewHost agentPatchPlayers_;
  struct AgentPatchPlayer {
    int pluginUid = 0;
    int generation = 0;       // matches load completions to this window
    juce::String sessionId;   // random, so answers can't match across restarts
    juce::String title;
    juce::String status = "loading"; // loading, loaded or failed
    juce::String error;
    bool changed = false; // the player's state changed since it loaded
    std::string baseline; // agent::patchBaseline() when the window opened
    bool editorShown = false; // the window has been seen open
  };
  std::map<std::string, AgentPatchPlayer> agentPatchPlayerSessions_;
  // Session number of each patch's last capture, so a repeated answer is
  // recognised instead of capturing again.
  std::map<std::string, std::string> agentPatchPlayerCaptured_;
  int agentPatchPlayerGeneration_ = 0;
  void noteAgentPatchPlayerChanges();
  juce::var agentPatchPlayerJson(const std::string &patchId);
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
  std::unique_ptr<BridgeMapCommands> bridgeMapCommands_;
  std::unique_ptr<ExpressionMapJsHandlers> expressionMapJsHandlers_;
  std::unique_ptr<versioning::VersionStore> versionStore_;
  StateManager stateManager_;
  LuaPluginCatalog luaCatalog_;
  std::unique_ptr<ProjectRestoreService> projectRestoreService_;

  std::atomic<bool> isTransportStarted_{false};
  // Dorico's sample rate, from FiddleNative's diagnostics; 0 until reported.
  std::atomic<double> hostSampleRate_{0.0};
  juce::String connectionWarning_;
  bool doricoConnected_ = false; // Fiddle's Dorico plug-in is connected

  /// Global harmonic analysis service (joint key/chord HMM).
  /// Owned here; MixerModel holds a raw ptr.
  HarmonicAnalysisService harmonicService_;

  double getDelayedTriggerTimeMs();
  /// The presentation time a Dorico event arriving now plays at, whether or
  /// not the transport is running.
  double delayedTriggerTimeNowMs() const;
  double delayedOutputPresentationTimeMs() const;
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
  std::atomic<RenderAheadEngine *> renderAheadForMidi_{nullptr};
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
  void changePlaybackDelay(int milliseconds);
  void changeDoricoPlayback(const DoricoPlaybackSettings &dorico);
  void changeRenderWorkerCount(int count);
  bool canChangeRenderWorkerCount() const;
  bool meterUpdatePending_ = false; // Message-thread-only, one web evaluation in flight.
  bool chairBusStatePending_ = false; // A new chair bus awaits pushGroupBusState.
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
  // The connected project's setup isn't loaded (not found, or Fiddle
  // couldn't switch): Dorico keeps this identity until the user chooses.
  std::optional<DoricoProjectRequest> unresolvedProject_;
  juce::var restoreReport_; // ProjectRestoreReport.h; void when none
  void setRestoreReport(const juce::var &report);
  /// What the project's sound depends on now (ProjectEnvironment.h), saved
  /// with each version on every Fiddle or Dorico save.
  ProjectEnvironment captureEnvironment();
  void recordEnvironment(const versioning::VersionId &versionId);
  /// What changed since the version was last saved, for the restore banner;
  /// empty for a version saved before Fiddle recorded this.
  std::vector<juce::String> environmentChangesSince(const versioning::VersionId &versionId);
  static juce::String changesKey(const versioning::VersionId &versionId,
                                 const std::vector<juce::String> &changes);
  /// The report with its list of changes emptied (after a Dorico save).
  static juce::var withoutChanges(const juce::var &report);
  juce::String dismissedChanges_; // changesKey of a list the user dismissed
  /// The user chose a setup (Use current setup, a checkout, a new branch):
  /// a connected project whose own setup wasn't loaded now uses it.
  void bindUnresolvedProject();
  juce::String branchNameOf(const versioning::BranchId &branchId) const;
  bool projectRestoreReady_ = false;

  /// Debug window (created eagerly, visibility toggled from View menu)
  std::unique_ptr<DebugWindow> debugWindow_;
  std::map<juce::String, std::unique_ptr<ToolWindow>> toolWindows_;
  void refreshToolWindow(const juce::String &id);
  juce::String printErrorOverride_; // retain notifications while the pane is hidden

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
  // A coalesced live-state request takes precedence over balance-only edits.
  bool stateRebuildNeedsLiveCapture_ = false;

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
  void setupBridgeMapHandlers();
  void pushBridgeMapState();
  void setupScoreTestHandlers();
  void pushScoreTestState();
  void armScoreTest(const juce::String &testId);
  void finishScoreTest();
  // Agent control (AgentControl.cpp, docs/mcp-control.md). Every request
  // runs on the message thread; edits go through the same Undo histories as
  // the UI.
  using AgentResponse = AgentControlServer::Response;
  AgentResponse handleAgentControlRequest(const juce::String &method,
                                          const juce::var &params);
  juce::var agentStatus() const;
  juce::var agentLayerSnapshot(const MixerStrip &strip) const;
  juce::var agentMixerSnapshot();
  juce::var agentLayerResult(const juce::String &stripId,
                             bool changed);
  juce::var agentLibrarySetupSnapshot(const juce::String &libraryId);
  juce::var agentChairsSnapshot(const juce::var &filter = {});
  juce::var agentLibrarySnapshot(const juce::String &libraryId);
  AgentResponse createAgentGuidedLibrary(const juce::var &params);
  AgentResponse agentInspectLibrarySources(const juce::var &params);
  AgentResponse agentOpenGuidedLibrary(const juce::var &params);
  AgentResponse agentUndoRedo(const juce::var &params, bool redo);
  AgentResponse agentSetLayerControl(const juce::String &method,
                                     const juce::var &params);
  AgentResponse agentSearchDoricoInstruments(const juce::var &params);
  AgentResponse agentListPlugins(const juce::var &params);
  AgentResponse agentScanPlugins(const juce::var &params);
  AgentResponse agentGetMixer(const juce::var &params);
  AgentResponse agentFindPatches(const juce::var &params);
  AgentResponse agentListLibraries(const juce::var &params);
  AgentResponse agentGetLibrary(const juce::var &params);
  AgentResponse agentSaveLibrary(const juce::var &params);
  AgentResponse agentDeleteLibrary(const juce::var &params);
  AgentResponse agentListMaps(const juce::var &params);
  AgentResponse agentCreateChairs(const juce::var &params);
  AgentResponse agentUpdateChair(const juce::var &params);
  AgentResponse agentDeleteChair(const juce::var &params);
  AgentResponse agentCreateKit(const juce::var &params);
  AgentResponse agentRenameKit(const juce::var &params);
  AgentResponse agentDeleteKit(const juce::var &params);
  AgentResponse agentAddLayer(const juce::var &params);
  AgentResponse agentRemoveLayer(const juce::var &params);
  AgentResponse agentInstallTemplate(const juce::var &params);
  AgentResponse agentRefreshLayers(const juce::var &params);
  /// Each bridge-assigned endpoint's check, keyed by (0-based port,
  /// 1-based channel) as BridgeMapCommands reports it.
  std::map<std::pair<int, int>, juce::var> agentBridgeChecks() const;
  AgentResponse agentGetBridgeMaps(const juce::var &params);
  AgentResponse agentUpdateBridgeMaps(const juce::var &params);
  AgentResponse agentOpenPatchPlayer(const juce::var &params);
  AgentResponse agentPatchPlayerStatus(const juce::var &params);
  AgentResponse agentCapturePatchPlayer(const juce::var &params);
  AgentResponse agentClosePatchPlayer(const juce::var &params);
  /// Why a setup edit can't run now, or empty.
  juce::String agentSetupBlocker(bool catalog) const;
  /// Refresh every view after a catalog edit; an open, unedited Library
  /// Manager editor reloads libraryId (or any library, when empty).
  void catalogChangedByAgent(const juce::String &libraryId);
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
  void pushMixPrintState(const juce::String &errorOverride = {});
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
  // Fiddle's expression and percussion maps (docs/maps.md), in LibraryMapUi.cpp.
  /// The layers linked with this one: on the chairs of the same percussion
  /// kit, from the same library patch (docs/percussion-kits.md). The layer
  /// itself comes first; a layer outside a kit is linked only to itself.
  std::vector<LayerRow> kitLinkedLayers(const LayerRow &layer);
  struct LayerEditResult {
    bool ok = false;
    juce::String error;
    std::vector<std::string> layerIds;
  };
  /// Add patchId to chairId (with wholeKit, also to the chair's kit-mates that
  /// lack it) as one Undo step.
  LayerEditResult addChairLayers(const std::string &chairId,
                                 const std::string &patchId, bool wholeKit);
  /// Remove a layer (with wholeKit, its linked kit copies) as one Undo step.
  LayerEditResult removeChairLayers(const std::string &layerId, bool wholeKit);
  PercussionMapAssignment loadPercussionMap(const std::string &entityId);
  ExpressionMapAssignment loadStoredExpressionMap(const std::string &entityId);
  /// Give live layers the collection's current copy of their maps: every
  /// map, or only those in `only`.
  void refreshLiveLibraryMaps(const std::set<MapKey> *only = nullptr);
  /// After a change to the map collection, update the layers using the maps
  /// that change touched, and no others: a layer keeping an older copy of an
  /// untouched map (an old project's) keeps it.
  void syncLayersToChangedMaps();
  std::map<MapKey, std::string> catalogMapSnapshot_;
  void pushLibraryMaps();
  void pushMapUsage();
  void libraryMapResult(LibraryMapKind kind, const juce::String &action, bool success,
                        const juce::String &message, const juce::String &entityId = {},
                        const juce::String &requestId = {}, const juce::StringArray &imported = {});
  bool performLibraryMapEdit(LibraryMapKind kind, const std::string &entityId,
                             std::optional<LibraryMapRow> target, const juce::String &description);
  bool importLibraryMap(const LibraryMapCandidate &candidate);
  void offerLibraryMapImport(LibraryMapKind kind, const juce::Array<juce::File> &sources,
                             const juce::String &origin, const juce::String &requestId);
  void assignPercussionMap(const juce::String &stripId, PercussionMapAssignment assignment);
  /// Import a file's expression map into Fiddle's maps for a layer; its
  /// entity ID, or empty. Refuses to replace a different copy that other
  /// patches or layers use.
  std::string importExpressionMapFileForStrip(const juce::String &stripId, const juce::File &file);
  void migrateLibraryMaps();
  void checkLibraryMapSources();
  ExpressionMapSources libraryExpressionMapSources();
  void setupLibraryMapHandlers();
  void setupLibraryMapSourceHandlers();
  void installChairPlaybackTemplate();
  struct TemplateInstallResult {
    bool ok = false;
    juce::String message;
  };
  TemplateInstallResult installChairPlaybackTemplateNow();
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
  ProjectRestoreService &projectRestoreService();
  void restoreStartupBusRouting();
  void reportInexactChairRouting(const std::vector<std::string> &chairs);

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
  void scheduleStateRebuild(bool captureLiveState = true);

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

  /// The chairs as last installed in Dorico's playback template, saved in the
  /// app data folder (Dorico's template is global). Loaded on first use.
  PlaybackTemplateRecord installedTemplate_;
  bool installedTemplateLoaded_ = false;
  PlaybackTemplateRecord &installedTemplate();

  /// One line per chair: what the playback template holds for it (identity,
  /// MIDI destination, pinned bridge map).
  PlaybackTemplateRecord::Lines chairTemplateLines();
  /// The template a stored version was saved with (its chairs and pinned
  /// bridge maps); nullopt for a version from before chairs.
  std::optional<PlaybackTemplateRecord::Lines> templateLinesForVersion(const versioning::VersionId &versionId);
  /// The template of the version the open Dorico project was saved with,
  /// when Fiddle restored exactly that version for it.
  std::optional<PlaybackTemplateRecord::Lines> projectTemplate_;
  PlaybackTemplateRecord::Status templateStatus();

  /// Broadcast the playback template's status to all UIs: whether it is out
  /// of date, and which chairs are new or changed since it was installed.
  void broadcastTemplateDirty();

  DebugUiFeed debugUiFeed_;
  double lastDebugUiPushMs_ = 0;
  void pushDebugUiBatch();

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
