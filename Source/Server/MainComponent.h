#pragma once

#include "../AudioSharedMemory.h"
#include "DebugWindow.h"
#include "DoricoInstrumentBrowser.h"
#include "ExpressionMapLibrary.h"
#include "FiddleDatabase.h"
#include "HistoryWindow.h"

#include "LibraryManagerWindow.h"
#include "InstrumentMapper.h"
#include "JsTestBridge.h"
#include "LuaPlugin.h"
#include "MasterInstrumentList.h"
#include "MidiTcpServer.h"
#include "MixerModel.h"
#include "NoteStreamTracker.h"
#include "PluginScanner.h"

#include "StateManager.h"
#include "SubnoteGenerator.h"
#include "HarmonicAnalysisService.h"
#include "MetronomeTempoTracker.h"
#include "UndoActions.h"
#include "midi_event.pb.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

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
  juce::File uiDir;
  juce::WebBrowserComponent webComponent;
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
  FiddleDatabase db_;
  std::unique_ptr<versioning::VersionStore> versionStore_;
  StateManager stateManager_;
  LuaPluginCatalog luaCatalog_;

  /// Global harmonic analysis service (joint key/chord HMM).
  /// Owned here; MixerModel holds a raw ptr.
  HarmonicAnalysisService harmonicService_;

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
  void runInitStep(int step);
  void timerCallback() override;
  void setupWebView();
  juce::WebBrowserComponent::Options createWebOptions();
  void broadcastJavascript(const juce::String &js);
  /// Send a typed message to all ready WebViews via window.__dispatchFromCpp.
  /// This is the preferred API — avoid raw broadcastJavascript where possible.
  void broadcastMessage(const juce::String &type,
                        const juce::var &data = juce::var());
  void handleJsMessage(const juce::String &type, const juce::var &payload);
  void pushLogMessage(const juce::String &msg, bool isError = false);
  void pushMixerState(bool markDirty = true);
  void pushToDebugWindow(const juce::String &js);
  static juce::String escapeForJS(const juce::String &str);

  void pushEventToWebView(const fiddle::MidiEvent &event);
  void pushSubnoteToWebView(const fiddle::Subnote &subnote);
  void pushBranches();
  void pushDagHistory();
  /// Broadcast the current version ID to all ready WebViews.
  void pushCurrentVersion();


  /// Save all strips to SQLite (called after every mutation).
  void saveAllStripsToDB();

  /// Load strips from SQLite into the mixer.
  void loadStripsFromDB();

  /// Save a single strip to the database by index.
  void saveStripToDB(const juce::String &stripId);

  /// Schedule a background rebuild of the shadow state blob.
  void scheduleStateRebuild();

  /// Poll MixerStrip plugin states for changes (called from timer).
  /// Skips plugins whose pluginUid is in listenerCapableUids_.
  void pollPluginStateChanges();

  /// Wire up a strip's PluginChangeListener to point at our shared state.
  void setupStripListener(MixerStrip &strip);

  /// Counter for throttling plugin state polls (20ms timer × 100 = 2s).
  int pluginPollCounter_ = 0;

  /// Cached state hashes for each MixerStrip's plugin instance.
  std::map<juce::String, uint64_t> pluginStateHashes_;

  /// Plugin UIDs that fire AudioProcessorListener callbacks.
  /// Populated by listeners, read by polling to skip those plugins.
  std::set<int> listenerCapableUids_;

  /// Atomic flag for debouncing listener callbacks (fire from any thread).
  std::atomic<bool> listenerDirtyPending_{false};

  /// Fingerprint of library instruments when the template was last installed.
  /// Used to determine if "Install Playback Template" should be enabled.
  juce::String lastInstalledFingerprint_;

  /// Compute a fingerprint from all library instruments in the DB.
  juce::String computeLibraryFingerprint();

  /// Broadcast whether the playback template is out-of-date to all UIs.
  void broadcastTemplateDirty();

  std::optional<juce::WebBrowserComponent::Resource>
  getResource(const juce::String &url);

  std::mutex logMutex;
  std::vector<std::pair<juce::String, bool>> logQueue;
  bool webViewLoaded = false;

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
