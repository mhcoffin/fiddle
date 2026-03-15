#pragma once

#include "../AudioSharedMemory.h"
#include "DebugWindow.h"
#include "DoricoInstrumentBrowser.h"
#include "ExpressionMapLibrary.h"
#include "FiddleDatabase.h"
#include "HistoryWindow.h"
#include "SetupWindow.h"
#include "InstrumentMapper.h"
#include "JsTestBridge.h"
#include "MasterInstrumentList.h"
#include "MidiTcpServer.h"
#include "MixerModel.h"
#include "NoteStreamTracker.h"
#include "PluginScanner.h"
#include "ScriptEngine.h"
#include "StateManager.h"
#include "SubnoteGenerator.h"
#include "UndoActions.h"
#include "midi_event.pb.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace fiddle {

class MainComponent : public juce::Component,
                      private juce::Timer,
                      public juce::AudioIODeviceCallback {
public:
  MainComponent(const juce::String &configName);
  ~MainComponent() override;

  /// Save current state to the active config file
  void saveConfig();

  /// Save current state under a new name, switch to it
  void saveConfigAs(const juce::String &newName);

  /// Clear all strips and plugins for a fresh new config
  void clearForNewConfig();

  /// Whether a config has been loaded (false in "waiting" state)
  bool isConfigLoaded() const { return configName_.isNotEmpty(); }

  /// Get the current config name
  juce::String getConfigName() const { return configName_; }

  /// Get the current config version
  juce::String getConfigVersion() const { return configVersion_; }

  /// Toggle debug window visibility.
  void toggleDebugWindow();

  /// Check if debug window is visible.
  bool isDebugWindowVisible() const;

  /// Toggle history window visibility (called from View menu).
  void toggleHistoryWindow();

  /// Check if history window is visible.
  bool isHistoryWindowVisible() const;

  /// Toggle setup window visibility (called from View menu or mixer).
  void toggleSetupWindow();

  /// Check if setup window is visible.
  bool isSetupWindowVisible() const;

  /// Save main window position/size to database.
  void saveMainWindowGeometry(int x, int y, int w, int h);

  /// Save debug window geometry + visibility to database.
  void saveDebugWindowGeometry();

  /// Save setup window geometry + visibility to database.
  void saveSetupWindowGeometry();

  /// Restore main window geometry from database.
  /// Returns the stored bounds (or defaults if not found).
  juce::Rectangle<int> restoreMainWindowGeometry();

  /// Get the MIDI TCP server for disconnect control
  MidiTcpServer *getMidiServer() { return server.get(); }

  /// Push current config status (name, version, dirty) to connected plugin.
  void pushConfigStatus();

  /// Callback fired when the active config changes
  std::function<void(const juce::String &name, const juce::String &version)>
      onConfigChanged;

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
  std::unique_ptr<ScriptEngine> scriptEngine;
  std::unique_ptr<fiddle::JsTestBridge> jsTestBridge_;
  AudioSharedMemory audioSharedMemory_{true}; // True = Producer

  uint64_t lastSampleTime = 0;
  uint32_t lastSystemTime = 0;

  juce::String configName_;
  juce::String configVersion_;

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

  /// Setup window (lazy instantiated)
  std::unique_ptr<SetupWindow> setupWindow_;
  bool setupWindowLoaded_ = false;

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
  void loadConfigByName(const juce::String &name);

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
