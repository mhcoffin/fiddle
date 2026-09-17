#include "MainComponent.h"
#include "AudioDeviceSettings.h"
#include "ChairLayerActions.h"
#include "LayerLibrarySetupAction.h"
#include "LibraryCatalogAction.h"
#include "ChairActions.h"
#include "ProjectSettingsAction.h"
#include "MixerControlActions.h"
#include "RenderAheadEngine.h"
#include "ProjectRestoreService.h"
#include "DoricoConfigGenerator.h"
#include "ExpressionMapCommandService.h"
#include "ExpressionMapJsHandlers.h"
#include "FiddleConfig.h"
#include "MasterAudioCommandService.h"
#include "MasterAudioJsHandlers.h"
#include "GroupBusCommandService.h"
#include "GroupBusActions.h"
#include "GroupBusJsHandlers.h"
#include "MixerCommandService.h"
#include "MixerJsHandlers.h"
#include "PluginCommandService.h"
#include "PluginChangeSuppression.h"
#include "PluginJsHandlers.h"
#include "StripAudioCommandService.h"
#include "StripAudioJsHandlers.h"

#include "midi_event.pb.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <google/protobuf/text_format.h>
#include <memory>
#include <set>
#include <string>

namespace fiddle {
namespace {

constexpr uint32_t kPluginStateSettleMs = 5000;

} // namespace

MainComponent::MainComponent()
    : webViewBridge_(jsRouter_,
                     [this](std::function<void()> f) { safeCallAsync(f); }),
      libraryPatchPreviewHost_(mixer_.getFormatManager()) {
  setupJsHandlers();
  webViewBridge_.setup();

  // Add webComponent with zero-size bounds so the native WKWebView peer
  // is created immediately and starts loading in parallel with C++ init.
  // Zero-size means the splash screen remains visible underneath.
  addAndMakeVisible(webViewBridge_.getMainWebComponent());
  webViewBridge_.getMainWebComponent().setBounds(0, 0, 0, 0);

  // Create debug window eagerly (hidden by default).
  // Native functions are registered inside DebugWindow's own constructor
  // to avoid JUCE Options move-semantics losing registrations.
  {
    juce::String root = juce::WebBrowserComponent::getResourceProviderRoot();

    DebugWindowCallbacks cbs;
    cbs.onSignalReady = [this]() {
      // Push cached state to the debug window so Timeline starts with data.
      double bpm = currentBpm_.load(std::memory_order_relaxed);
      if (bpm > 0.0) {
        juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
        tempoObj->setProperty("bpm", bpm);
        tempoObj->setProperty("samplePosition", (juce::int64)0);
        tempoObj->setProperty("timeSigNumerator", 4);
        tempoObj->setProperty("timeSigDenominator", 4);
        broadcastMessage("setTempo", juce::var(tempoObj.get()));
      }
      // Push instrument map
      juce::String mapJson = masterList_.getChannelMapAsJson();
      broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson));
    };
    cbs.onScanPlugins = [this]() {
      if (pluginScanner_.isScanning())
        return;
      pushLogMessage("<b>[Plugins]</b> Scanning for VST3 plugins "
                     "(incremental)...");
      pluginScanner_.scanIncrementalAsync(db_, [this]() {
        int count = pluginScanner_.getPluginCount();
        pushLogMessage("<b>[Plugins]</b> Scan complete: " +
                       juce::String(count) + " plugins found");
        juce::String json = pluginScanner_.getPluginListAsJson();
        broadcastMessage("setPluginList", juce::JSON::fromString(json));
      });
    };
    cbs.onRescanPlugins = [this]() {
      if (pluginScanner_.isScanning())
        return;
      pushLogMessage("<b>[Plugins]</b> Full rescan of VST3 plugins...");
      pluginScanner_.rescanAsync(db_, [this]() {
        int count = pluginScanner_.getPluginCount();
        pushLogMessage("<b>[Plugins]</b> Rescan complete: " +
                       juce::String(count) + " plugins found");
        juce::String json = pluginScanner_.getPluginListAsJson();
        broadcastMessage("setPluginList", juce::JSON::fromString(json));
      });
    };
    cbs.onRequestPluginsState = [this]() {
      safeCallAsync([this]() {
        if (pluginScanner_.getPluginCount() > 0) {
          juce::String json = pluginScanner_.getPluginListAsJson();
          juce::String call =
              "setPluginList('" + WebViewBridge::escapeForJS(json) + "')";
          pushToDebugWindow(call);
        }
      });
    };

    cbs.onForwardMessage = [this](const juce::String &type,
                                  const juce::var &payload) {
      safeCallAsync(
          [this, type, payload]() { jsRouter_.handleMessage(type, payload); });
    };

    debugWindow_ = std::make_unique<DebugWindow>(
        [this](const juce::String &url) {
          return webViewBridge_.getResource(url);
        },
        std::move(cbs));
    debugWindow_->loadDebugPage(root);

    // Wire geometry saving (actual restore happens in init step 7
    // after the database is opened).
    debugWindow_->setGeometrySaver([this]() { saveDebugWindowGeometry(); });
  }

  setSize(800, 600);

  // Kick off phased async initialization so the splash screen can paint
  // between steps.
  addInitMessage("Starting up...");
  safeCallAsync([this]() { initializeApp(); });

  // Initialize the bridge for UI testing on the main window using port 9223
  jsTestBridge_ = std::make_unique<JsTestBridge>(
      webViewBridge_.getMainWebComponent(), 9223);
}

void MainComponent::addInitMessage(const juce::String &msg) {
  initMessages_.add(msg);
  repaint();
}

void MainComponent::initializeApp() { initExpressionMaps(); }

void MainComponent::initExpressionMaps() {
  // Scan for expression maps in Dorico directories
  addInitMessage("Scanning expression maps...");
  xmapLibrary_.scanDefaultDirectories();
  safeCallAsync([this]() { initInstrumentBrowser(); });
}

void MainComponent::initInstrumentBrowser() {
  // Load Dorico instrument browser
  addInitMessage("Loading Dorico instruments...");
  if (instrumentBrowser_.loadFromDorico()) {
    pushLogMessage(
        "<b>[Setup]</b> Loaded " +
        juce::String((int)instrumentBrowser_.getInstruments().size()) +
        " instruments from Dorico");
  } else {
    pushLogMessage("<b>[Setup]</b> Could not load Dorico instruments", true);
  }
  safeCallAsync([this]() { initPlaceholder(); });
}

void MainComponent::initPlaceholder() {
  // Ensemble is now loaded from DB after db_.open() (init step 7+).
  // This step is kept as a placeholder for the splash screen message.
  addInitMessage("Loading instrument selections...");
  safeCallAsync([this]() { initLuaPlugins(); });
}

void MainComponent::initLuaPlugins() {
  addInitMessage("Initializing Lua plugin system...");
  // Scan bundled plugins (in app bundle next to executable)
  auto exeFile =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  auto bundledDir =
      exeFile.getParentDirectory().getChildFile("scripts").getChildFile(
          "plugins");
  if (bundledDir.isDirectory()) {
    luaCatalog_.scanDirectory(bundledDir.getFullPathName().toStdString());
  }
  // Scan user plugins directory
  luaCatalog_.scanDefaultDirectory();
  std::cerr << "[Init] Lua plugin system ready — "
            << luaCatalog_.plugins().size() << " plugins found" << std::endl;
  safeCallAsync([this]() { initExpressionMapLibrary(); });
}

void MainComponent::initExpressionMapLibrary() {
  // Load Expression Map from .doricolib
  addInitMessage("Loading expression map...");
  juce::File exeFile =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  juce::File doricolibFile =
      exeFile.getSiblingFile("Fiddle_Universal.doricolib");
  if (!doricolibFile.exists()) {
    doricolibFile = exeFile.getParentDirectory().getSiblingFile(
        "Resources/Fiddle_Universal.doricolib");
  }
  if (!doricolibFile.exists()) {
    juce::File projectRoot = exeFile;
    for (int i = 0; i < 10; ++i) {
      if (projectRoot.getChildFile("Source").isDirectory())
        break;
      projectRoot = projectRoot.getParentDirectory();
    }
    doricolibFile =
        projectRoot.getChildFile("resources/Fiddle_Universal.doricolib");
  }

  if (doricolibFile.exists()) {
    if (expressionMap.loadFromDoricolib(doricolibFile)) {
      pushLogMessage("<b>[ExpressionMap]</b> Loaded from " +
                     doricolibFile.getFileName());
      noteTracker.setExpressionMap(&expressionMap);
    } else {
      pushLogMessage("<b>[ExpressionMap]</b> Failed to parse " +
                         doricolibFile.getFileName(),
                     true);
    }
  } else {
    pushLogMessage("<b>[ExpressionMap]</b> Could not find "
                   "Fiddle_Universal.doricolib",
                   true);
  }
  // Wire metronome tracker unconditionally — prominence annotations
  // work even without an expression map.
  noteTracker.setMetronomeTracker(&metronomeTracker_);
  safeCallAsync([this]() { initMidiServer(); });
}

void MainComponent::initMidiServer() {
  // Set up note/MIDI callbacks, server, and start listening
  addInitMessage("Starting MIDI server...");
  {
    // Start the harmonic analysis service (HMM-based key/chord detection)
    // and wire it to the mixer. The service is owned by MainComponent;
    // MixerModel holds a raw ptr.
    harmonicService_.setBpm(currentBpm_.load(std::memory_order_relaxed));
    harmonicService_.setPlaybackDelayMs(mixer_.getPlaybackDelayMs());
    harmonicService_.buildDefaultEmissions();

    // Load trained transition matrix from bundle resources.
    {
      auto bundleDir =
          juce::File::getSpecialLocation(juce::File::currentExecutableFile)
              .getParentDirectory();
      auto transFile =
          bundleDir.getChildFile("resources/hmm/cpe_transitions.bin");
      if (!transFile.existsAsFile()) {
        // Also check project resources dir (for dev builds).
        // From MacOS dir, project root is 6 levels up:
        // MacOS → Contents → .app → Debug → FiddleServer_artefacts → build →
        // project
        auto projectRoot = bundleDir;
        for (int i = 0; i < 6; ++i)
          projectRoot = projectRoot.getParentDirectory();
        auto projectResources =
            projectRoot.getChildFile("resources/hmm/cpe_transitions.bin");
        if (projectResources.existsAsFile())
          transFile = projectResources;
      }

      if (transFile.existsAsFile()) {
        juce::MemoryBlock data;
        transFile.loadFileAsData(data);
        if (harmonicService_.loadTransitionMatrix(data.getData(),
                                                  data.getSize()))
          std::cerr << "[Init] Loaded transition matrix: "
                    << transFile.getFullPathName() << std::endl;
        else
          std::cerr << "[Init] WARNING: Failed to parse transition matrix"
                    << std::endl;

        // Load degree priors from the same directory.
        auto priorsFile = transFile.getSiblingFile("degree_priors.bin");
        if (priorsFile.existsAsFile()) {
          juce::MemoryBlock priorsData;
          priorsFile.loadFileAsData(priorsData);
          if (harmonicService_.loadDegreePriors(priorsData.getData(),
                                                priorsData.getSize()))
            std::cerr << "[Init] Loaded degree priors: "
                      << priorsFile.getFullPathName() << std::endl;
          else
            std::cerr << "[Init] WARNING: Failed to parse degree priors"
                      << std::endl;
        }
      } else {
        std::cerr
            << "[Init] No transition matrix found; using emission-only mode"
            << std::endl;
      }
    }

    mixer_.setHarmonicService(&harmonicService_);
    harmonicService_.start();
    std::cerr << "[Init] HarmonicAnalysisService started" << std::endl;

    // Wire metronome tempo tracker callback.
    // When the tracker detects a tempo change from the click track,
    // update the global BPM and broadcast to the UI.
    metronomeTracker_.onTempoChanged = [this](double bpm, int tsNum, int tsDen,
                                              uint64_t samplePos) {
      currentBpm_.store(bpm, std::memory_order_relaxed);
      harmonicService_.setBpm(bpm);

      juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
      tempoObj->setProperty("bpm", bpm);
      tempoObj->setProperty("timeSigNumerator", tsNum);
      tempoObj->setProperty("timeSigDenominator", tsDen);
      tempoObj->setProperty("samplePosition", (juce::int64)samplePos);
      tempoObj->setProperty("source", juce::String("metronome"));
      juce::var tempoVar(tempoObj.get());
      safeCallAsync(
          [this, tempoVar]() { broadcastMessage("setTempo", tempoVar); });
    };

    auto noteToJson = [](const fiddle::Note &n) {
      juce::DynamicObject::Ptr obj = new juce::DynamicObject();
      obj->setProperty("id", (juce::int64)n.id());
      obj->setProperty("noteNumber", (int)n.note_number());
      obj->setProperty("channel", (int)n.channel());
      obj->setProperty("port", (int)n.port());
      obj->setProperty("startVelocity", (int)n.start_velocity());
      obj->setProperty("startSample", (juce::int64)n.start_sample());
      obj->setProperty("durationSamples", (juce::int64)n.duration_samples());

      // Compute end velocity for UI display:
      // VELOCITY mode (short notes) → same as start velocity
      // CC mode (sustained notes) → last CC1 value from automation
      int endVel = (int)n.start_velocity();
      if (n.dynamics_mode() == fiddle::Note::CC) {
        auto it = n.cc_automation().find(1); // CC1
        if (it != n.cc_automation().end() && it->second.points_size() > 0) {
          endVel = (int)it->second.points(it->second.points_size() - 1).value();
        }
      }
      obj->setProperty("endVelocity", endVel);

      juce::DynamicObject::Ptr dims = new juce::DynamicObject();
      for (auto const &it : n.notation_dimensions()) {
        dims->setProperty(juce::String(it.first), it.second);
      }
      obj->setProperty("dimensions", dims.get());

      juce::DynamicObject::Ptr techs = new juce::DynamicObject();
      for (auto const &it : n.notation_techniques()) {
        techs->setProperty(juce::String(it.first), juce::String(it.second));
      }
      obj->setProperty("techniques", techs.get());

      juce::DynamicObject::Ptr defaults = new juce::DynamicObject();
      for (auto const &it : n.notation_is_default()) {
        defaults->setProperty(juce::String(it.first), it.second);
      }
      obj->setProperty("notation_is_default", defaults.get());

      obj->setProperty("beatProminence", (int)n.beat_prominence());

      return juce::JSON::toString(juce::var(obj.get()), true);
    };

    auto midiEventToJson = [](const fiddle::MidiEvent &event,
                              uint64_t absoluteSamples, int oldCCVal) {
      juce::DynamicObject::Ptr obj = new juce::DynamicObject();
      obj->setProperty("type", (int)event.event_case());
      obj->setProperty("channel", (int)event.channel());
      obj->setProperty("port", (int)event.port());
      obj->setProperty("timestamp", (juce::int64)absoluteSamples);

      if (event.has_note_on()) {
        obj->setProperty("note", (int)event.note_on().note_number());
        obj->setProperty("velocity", (int)event.note_on().velocity());
      } else if (event.has_note_off()) {
        obj->setProperty("note", (int)event.note_off().note_number());
        obj->setProperty("velocity", (int)event.note_off().velocity());
      } else if (event.has_cc()) {
        obj->setProperty("cc", (int)event.cc().controller_number());
        obj->setProperty("value", (int)event.cc().controller_value());
        if (oldCCVal >= 0)
          obj->setProperty("oldValue", oldCCVal);
      } else if (event.has_program_change()) {
        obj->setProperty("program",
                         (int)event.program_change().program_number());
      } else if (event.has_other()) {
        obj->setProperty("description",
                         juce::String(event.other().description()));
      } else if (event.has_transport()) {
        obj->setProperty("transportType", (int)event.transport().type());
      }

      return juce::JSON::toString(juce::var(obj.get()));
    };

    noteTracker.uiLogger = [this](const juce::String &msg) {
      pushLogMessage(msg);
    };

    noteTracker.setCallbacks(
        {[this, noteToJson](const fiddle::Note &n) {
           pushLogMessage("<b>[Tracker]</b> Note ON: " +
                          juce::String((juce::int64)n.id()) + " (Ch " +
                          juce::String((int)n.channel()) + ")");
           subnoteGenerator.onNoteStarted(n);

           double triggerTimeMs = getDelayedTriggerTimeMs();
           // Route through annotator-aware path.
           // n.port() from Dorico is 0-based (matches strip inputPort).
           // n.channel() from Dorico is 1-based; strip inputChannel is
           // 0-based, so subtract 1.

           // Feed the harmonic analysis service.
           harmonicService_.onNoteEvent(
               (int)n.note_number(), true, (int)n.start_velocity(),
               juce::Time::getMillisecondCounterHiRes());

           int notePort = (int)n.port();
           // std::cerr << "[MainComponent] Routing Note ON (port=" << notePort
           //           << ", ch=" << (int)n.channel() - 1 << ")" << std::endl;
           // Mutable copy so annotators can populate pre_note/post_note
           fiddle::Note mutableNote(n);
           mixer_.routeAnnotatedNoteOn(notePort, (int)n.channel() - 1,
                                       mutableNote, triggerTimeMs);

           juce::var noteVar = juce::JSON::fromString(noteToJson(n));
           juce::DynamicObject::Ptr obj = new juce::DynamicObject();
           obj->setProperty("noteData", noteVar);
           obj->setProperty("status", juce::String("started"));
           safeCallAsync([this, obj]() {
             broadcastMessage("updateNoteState", juce::var(obj.get()));
           });
         },
         [this, noteToJson](const fiddle::Note &n) {
           pushLogMessage("<b>[Tracker]</b> Note OFF: " +
                          juce::String((juce::int64)n.id()));
           subnoteGenerator.onNoteEnded(n);

           double triggerTimeMs = getDelayedTriggerTimeMs();
           // Route through annotator-aware path.

           // Feed the harmonic analysis service.
           harmonicService_.onNoteEvent(
               (int)n.note_number(), false, 0,
               juce::Time::getMillisecondCounterHiRes());

           int noteOffPort = (int)n.port();
           // Always route the note-off to the VST.
           // When transport is stopped (e.g. Dorico note audition),
           // getDelayedTriggerTimeMs() returns 0 so the note-off fires
           // immediately. The transport-stop panic (allNotesOff) handles
           // the started→stopped transition separately.
           {
             fiddle::Note mutableNote(n);
             mixer_.routeAnnotatedNoteOff(noteOffPort, (int)n.channel() - 1,
                                          mutableNote, triggerTimeMs);
           }

           juce::var noteVar = juce::JSON::fromString(noteToJson(n));
           juce::DynamicObject::Ptr obj = new juce::DynamicObject();
           obj->setProperty("noteData", noteVar);
           obj->setProperty("status", juce::String("ended"));
           safeCallAsync([this, obj]() {
             broadcastMessage("updateNoteState", juce::var(obj.get()));
           });
         },
         [this, noteToJson](const fiddle::Note &n) {
           juce::var noteVar = juce::JSON::fromString(noteToJson(n));
           juce::DynamicObject::Ptr obj = new juce::DynamicObject();
           obj->setProperty("noteData", noteVar);
           obj->setProperty("status", juce::String("updated"));
           safeCallAsync([this, obj]() {
             broadcastMessage("updateNoteState", juce::var(obj.get()));
           });
         },
         [this, midiEventToJson](const fiddle::MidiEvent &event,
                                 uint64_t absoluteSamples, int oldCCVal) {
           if (event.has_transport()) {
             if (event.transport().type() ==
                 fiddle::MidiEvent_TransportEvent_Type_STOP) {
               // Treat transport stop as a hard synchronization point. Reset
               // every hosted instrument independently of the tracker's note
               // set; missing Dorico NoteOffs must never leave voices sounding.
               mixer_.allNotesOff();
               harmonicService_.onTransportStop();
             }
           }

           // Live tempo from FiddleNative's ProcessContext.
           // Forward to the classifier so beat-duration weighting is accurate.
           if (event.has_tempo() && event.tempo().bpm() > 0.0) {
             double bpm = event.tempo().bpm();
             currentBpm_.store(bpm, std::memory_order_relaxed);
             harmonicService_.setBpm(bpm);
             std::cerr << "[Tempo] BPM=" << bpm
                       << " timeSig=" << event.tempo().time_sig_numerator()
                       << "/" << event.tempo().time_sig_denominator()
                       << std::endl;

             // Broadcast to the UI so the Timeline can align its beat grid.
             juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
             tempoObj->setProperty("bpm", bpm);
             tempoObj->setProperty("timeSigNumerator",
                                   event.tempo().time_sig_numerator());
             tempoObj->setProperty("timeSigDenominator",
                                   event.tempo().time_sig_denominator());
             // absoluteSamples gives the exact sample position so the UI can
             // draw the marker at the correct x on the timeline.
             tempoObj->setProperty("samplePosition",
                                   (juce::int64)absoluteSamples);
             juce::var tempoVar(tempoObj.get());
             safeCallAsync([this, tempoVar]() {
               broadcastMessage("setTempo", tempoVar);
             });
           }

           juce::var eventVar = juce::JSON::fromString(
               midiEventToJson(event, absoluteSamples, oldCCVal));
           safeCallAsync([this, eventVar]() {
             broadcastMessage("pushMidiEvent", eventVar);
           });
         }});

    subnoteGenerator.setCallbacks(
        {[this](const fiddle::Subnote &s) {
           pushSubnoteToWebView(s);
           safeCallAsync([this, id = s.id()]() {
             juce::DynamicObject::Ptr nd = new juce::DynamicObject();
             nd->setProperty("id", (juce::int64)id);
             juce::DynamicObject::Ptr obj = new juce::DynamicObject();
             obj->setProperty("noteData", juce::var(nd.get()));
             obj->setProperty("status", juce::String("subnote"));
             broadcastMessage("updateNoteState", juce::var(obj.get()));
           });
         },
         [this, noteToJson](const fiddle::Note &n) {
           pushLogMessage("<b>[Watchdog]</b> Note Timed Out: " +
                          juce::String((juce::int64)n.id()));
           juce::var noteVar = juce::JSON::fromString(noteToJson(n));
           juce::DynamicObject::Ptr obj = new juce::DynamicObject();
           obj->setProperty("noteData", noteVar);
           obj->setProperty("status", juce::String("ended"));
           safeCallAsync([this, obj]() {
             broadcastMessage("updateNoteState", juce::var(obj.get()));
           });
         }});

    server = std::make_unique<fiddle::MidiTcpServer>();
    server->onMessageReceived([this](const fiddle::MidiEvent &event) {
      if (event.has_audio_diagnostics()) {
        const auto receivedMs = juce::Time::getMillisecondCounterHiRes();
        safeCallAsync([this, data = event.audio_diagnostics(), receivedMs] {
          returnDiagnostics_ = data;
          returnDiagnosticsReceivedMs_ = receivedMs;
        });
        return; // Telemetry is not MIDI, performance activity, or project state.
      }
      if (isPluginPerformanceActivity(event)) {
        lastPluginPerformanceActivityMs_.store(
            juce::Time::getMillisecondCounter(), std::memory_order_release);
      }

      // ── Transport State Machine ───────────────────────────────────
      if (event.has_transport()) {
        if (event.transport().type() ==
            fiddle::MidiEvent_TransportEvent_Type_START) {
          bool expected = false;
          if (!isTransportStarted_.compare_exchange_strong(expected, true)) {
            return; // Already started, ignore redundant event
          }
          const auto printStartMs = delayedOutputPresentationTimeMs();
          safeCallAsync([this, printStartMs] {
            if (renderAhead_ && renderAhead_->beginMixPrintAt(printStartMs))
              pushMixPrintState();
          });
        } else if (event.transport().type() ==
                   fiddle::MidiEvent_TransportEvent_Type_STOP) {
          bool expected = true;
          if (!isTransportStarted_.compare_exchange_strong(expected, false)) {
            return; // Already stopped, ignore redundant event
          }
          const auto printStopMs = delayedOutputPresentationTimeMs();
          safeCallAsync([this, printStopMs] {
            if (renderAhead_ && renderAhead_->endMixPrintAt(printStopMs))
              pushMixPrintState();
          });
        }
      }

      // Force a log to the UI so we can see the flow
      pushLogMessage("<b>[Server]</b> Received Event Case: " +
                     juce::String((int)event.event_case()) +
                     " Ch: " + juce::String(event.channel()));

      // ── Metronome interception ────────────────────────────────────
      // The reserved metronome channel is port 0, channel 0 (0-based).
      // In the protobuf, channel is 1-based, so channel()==1 is ch 0.
      bool isMetronomeChannel = ((int)event.port() == kMetronomePort &&
                                 (int)event.channel() == kMetronomeChannel + 1);

      // ── Reset metronome tracker on transport stop ──────────────────
      // Transport events arrive on channel 0 (default), not on the
      // metronome channel, so this must be checked before the
      // channel-specific block.
      if (event.has_transport() &&
          event.transport().type() ==
              fiddle::MidiEvent_TransportEvent_Type_STOP) {
        metronomeTracker_.reset();
      }

      if (isMetronomeChannel) {
        // Feed note-on events to the tempo tracker
        if (event.has_note_on() && event.note_on().velocity() > 0) {
          uint64_t samplePos = event.has_host_sample_position()
                                   ? event.host_sample_position()
                                   : event.timestamp_samples();
          int noteNum = (int)event.note_on().note_number();
          metronomeTracker_.onBeat(noteNum, samplePos);

          // Broadcast each beat position to the UI so the Timeline
          // can use actual beat positions for its grid.
          bool isDownbeat = (noteNum == metronomeTracker_.downbeatNote);
          juce::DynamicObject::Ptr beatObj = new juce::DynamicObject();
          beatObj->setProperty("samplePosition", (juce::int64)samplePos);
          beatObj->setProperty("isDownbeat", isDownbeat);
          beatObj->setProperty("noteNumber", noteNum);
          beatObj->setProperty("velocity", (int)event.note_on().velocity());
          juce::var beatVar(beatObj.get());
          safeCallAsync([this, beatVar]() {
            broadcastMessage("metronomeBeat", beatVar);
          });
        }

        // Still push to WebView so click notes are visible in Timeline,
        // but do NOT route through noteTracker or mixer.
        pushEventToWebView(event);
        return; // Skip normal processing
      }

      noteTracker.processEvent(event);
      pushEventToWebView(event);

      // Record raw incoming MIDI for capture/comparison
      {
        double nowMs = juce::Time::getMillisecondCounterHiRes();
        int port = (int)event.port();
        int ch0 = (int)event.channel() - 1; // 0-based for strip matching
        if (event.has_note_on()) {
          mixer_.recordIncomingMidi(port, ch0, CapturedMidiEvent::NoteOn,
                                    (int)event.note_on().note_number(),
                                    (int)event.note_on().velocity(), nowMs);
        } else if (event.has_note_off()) {
          mixer_.recordIncomingMidi(port, ch0, CapturedMidiEvent::NoteOff,
                                    (int)event.note_off().note_number(),
                                    (int)event.note_off().velocity(), nowMs);
        } else if (event.has_cc()) {
          mixer_.recordIncomingMidi(port, ch0, CapturedMidiEvent::CC,
                                    (int)event.cc().controller_number(),
                                    (int)event.cc().controller_value(), nowMs);
        } else if (event.has_program_change()) {
          mixer_.recordIncomingMidi(
              port, ch0, CapturedMidiEvent::ProgramChange,
              (int)event.program_change().program_number(), 0, nowMs);
        }
      }

      // Forward CC events to VST plugins
      if (event.has_cc()) {
        int ch = event.channel();
        // event.port() from Dorico is 0-based (matches strip inputPort
        // directly). event.channel() from Dorico is 1-based; subtract 1 for
        // strip inputChannel.
        int port = (int)event.port();
        int ccNum = event.cc().controller_number();
        int ccVal = event.cc().controller_value();

        // CC 120 (All Sound Off) or CC 123 (All Notes Off) → panic matching
        // strips
        if (ccNum == 120 || ccNum == 123) {
          mixer_.panicStrips(port, ch - 1);
          // Do NOT update transport state here! Dorico sends CC 123
          // continually during playback as an emergency off, not just on
          // transport stop. If we stop transport here, it causes the delay
          // buffer to bypass and cuts off notes randomly, resulting in tremolo.
          std::cerr << "[MainComponent] Panic: CC " << ccNum
                    << " → panicStrips on port " << port << " ch " << ch - 1
                    << std::endl;
        } else {
          // Route CC through annotator-aware path.
          // ch from Dorico is 1-based; JUCE controllerEvent also uses 1-16.
          juce::MidiMessage ccMsg =
              juce::MidiMessage::controllerEvent(ch, ccNum, ccVal);
          double triggerTimeMs = getDelayedTriggerTimeMs();
          mixer_.routeAnnotatedCC(port, ch - 1, event, ccMsg, triggerTimeMs);
        }

        // ch is 1-based from protobuf — log it directly (no +1 needed)
        juce::String logMsg = "<b>[CC]</b> Ch " + juce::String(ch) + " CC" +
                              juce::String(ccNum) + " = " + juce::String(ccVal);
        auto *dim = expressionMap.getDimensionForCC(ccNum);
        if (dim) {
          logMsg += " (" + dim->name;
          auto techIt = dim->techniques.find(ccVal);
          if (techIt != dim->techniques.end())
            logMsg += ": " + techIt->second;
          else
            logMsg += ": unknown value";
          logMsg += ")";
        }
        pushLogMessage(logMsg);
      }

      // Track program changes → instrument names for the UI
      if (event.has_program_change()) {
        int channel = event.channel();
        int program = event.program_change().program_number();
        std::string name =
            instrumentMapper_.handleProgramChange(channel, program);
        if (!name.empty()) {
          juce::DynamicObject::Ptr ci = new juce::DynamicObject();
          ci->setProperty("channel", channel);
          ci->setProperty("name", juce::String(name));
          safeCallAsync([this, ci]() {
            broadcastMessage("setChannelInstrument", juce::var(ci.get()));
          });
        }
      }

      // Dynamic Server Load Command (from VST Host)
      if (event.has_load_config()) {
        const auto legacyBranchName = event.load_config().config_path();
        const auto branchId = event.load_config().branch_id();
        const auto versionId = event.load_config().version_id();
        safeCallAsync([this, legacyBranchName, branchId, versionId]() {
          pushLogMessage("<b>[Host]</b> Dorico project connected");
          if (!legacyBranchName.empty() || !branchId.empty() ||
              !versionId.empty()) {
            restoreDoricoProject(legacyBranchName, branchId, versionId);
          } else {
            pushConfigStatus();
          }
        });
      }

      if (event.has_save_config_request()) {
        const auto requestId = event.save_config_request().request_id();
        safeCallAsync(
            [this, requestId]() { handleDoricoSaveRequest(requestId); });
      }

      // Restore full state from Dorico (setStateInformation)
      if (event.has_restore_state()) {
        auto configName = juce::String(event.restore_state().config_name());
        auto &blobData = event.restore_state().state_blob();

        safeCallAsync([this, configName, blobData]() {
          auto restored =
              StateManager::deserializeBlob(blobData.data(), blobData.size());
          if (!restored) {
            pushLogMessage("<span style=\"color: red;\"><b>[Restore]</b> "
                           "Failed to deserialize state blob</span>");
            return;
          }

          pushLogMessage("<b>[Restore]</b> Restoring state from Dorico: '" +
                         configName + "' (" +
                         juce::String(restored->strips.size()) + " strips)");

          // Commit imported state to current branch
          // (strip-level data is committed below via saveAllStripsToDB)

          // Clear and rebuild mixer from blob
          mixer_.clear();
          mixer_.setProjectSettings(restored->projectSettings);
          undoManager_.clear();

          for (auto &rs : restored->strips) {
            juce::String newId = mixer_.addStrip();
            if (auto *strip = mixer_.getStrip(newId)) {
              strip->id = rs.id;
              strip->library = rs.library;
              strip->family = rs.family;
              strip->isSolo = rs.isSolo;
              strip->setActive(rs.active);
              strip->setMuted(rs.muted);
              strip->setSoloed(rs.soloed);
              strip->setInputAssignment(rs.inputPort, rs.inputChannel);
              strip->pluginUid = rs.pluginUid;
              strip->setGainDb(rs.gainDb);
              setupStripPluginSlot(*strip);

              // Restore expression map
              if (!rs.expressionMapEntityID.empty()) {
                auto xmapData = xmapLibrary_.loadPersisted(
                    rs.expressionMapEntityID, rs.expressionMapSourceXml);
                if (xmapData)
                  strip->setExpressionMapAssignment(
                      {std::move(xmapData), rs.expressionMapPath,
                       rs.expressionMapSourceXml});
              }

              restoreStripPlugin(*strip, rs.pluginUid, rs.pluginState);
              restoreStripAudio(*strip, rs.audioInsertState, false);

              // Restore Lua plugins
              for (const auto &fileName : rs.luaPluginFileNames) {
                auto resolved = luaCatalog_.resolvePluginPath(fileName);
                auto plugin = std::make_shared<LuaPlugin>(
                    resolved.empty() ? fileName : resolved);
                if (resolved.empty() || !plugin->load())
                  pushLogMessage("<b>[Restore]</b> Lua processor '" +
                                     juce::String(fileName) +
                                     "' is unavailable and remains unloaded.",
                                 true);
                strip->addLuaPlugin(std::move(plugin));
              }
            }
          }

          restoreMasterAudio(*restored);

          saveAllStripsToDB();
          pushMixerState(false);
          mixer_.syncStripsToInstruments(masterList_);
          pushMixerState(false);
          scheduleStateRebuild();

          pushLogMessage("<b>[Restore]</b> State restored successfully");
        });
      }

      lastSampleTime = event.timestamp_samples();
      lastSystemTime = juce::Time::getMillisecondCounter();
    });

    server->onConnectionChanged([this](bool connected, juce::String host) {
      safeCallAsync([this, connected, host]() {
        returnDiagnosticsReceivedMs_ = 0;
        returnDiagnostics_.Clear();
        broadcastMessage("setConnectionState", connected);
        if (connected) {
          pushConfigStatus();
          pushLogMessage("<span style=\"color: #03dac6\">[Connected: " + host +
                         "]</span>");
        } else {
          // A lost endpoint cannot send its final transport or NoteOff events.
          // Fail safe to silence and clear all transient performance state.
          isTransportStarted_.store(false, std::memory_order_release);
          noteTracker.resetForTransportStop();
          mixer_.allNotesOff();
          harmonicService_.onTransportStop();
          metronomeTracker_.reset();
          if (renderAhead_ && renderAhead_->mixPrintIsArmed()) {
            renderAhead_->stopMixPrint();
            pushMixPrintState("Dorico disconnected while the print was armed");
          }
          pushLogMessage(
              "<span style=\"color: #cf6679\">[Disconnected]</span>");
        }
      });
    });

    server->onAdditionalConnectionAttempt([this](juce::String host) {
      safeCallAsync([this, host]() {
        const juce::String message =
            "More than one Fiddle plug-in tried to connect. Fiddle supports "
            "one connection at a time. In Dorico, this usually means the "
            "playback template does not provide enough matching chairs, so "
            "Dorico loaded another Fiddle instance. Check Chair Manager, "
            "then reinstall and reapply the playback template.";
        connectionWarning_ = message;
        broadcastMessage("setConnectionWarning", message);
        pushLogMessage("<span style=\"color: #fbbf24\"><b>[Additional "
                       "connection rejected: " +
                           host + "]</b> " + message + "</span>",
                       true);
      });
    });

    server->onRawActivity([this](juce::String msg) {
      safeCallAsync(
          [this, msg]() { pushLogMessage("<small>" + msg + "</small>"); });
    });

    server->startThread();

    startTimer(20); // 20ms tick for subnotes
  }
  safeCallAsync([this]() { initAudioDevice(); });
}

void MainComponent::initAudioDevice() {
  // Initialize audio device for driving VST3 plugins
  addInitMessage("Initializing audio device...");
  {
    renderAhead_ = std::make_unique<RenderAheadEngine>(mixer_, audioDiagnostics_,
        juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Caches/Fiddle/fiddle_audio_v2.mmap"));
    audioSettings_ = std::make_unique<AudioDeviceSettings>(
        deviceManager, FiddleConfig::getAppDataDir().getChildFile("audio-device.xml"));
    juce::String err = audioSettings_->initialise();
    if (err.isNotEmpty()) {
      std::cerr << "[Audio] Failed to initialize device manager: " << err
                << std::endl;
    }
    // Also register after an initial device failure so Audio Settings can recover.
    deviceManager.addAudioCallback(this);
  }
  safeCallAsync([this]() { initDatabase(); });
}

void MainComponent::initDatabase() {
  addInitMessage("Opening database...");

  // Open SQLite database
  auto dbFile = FiddleConfig::getAppDataDir().getChildFile("fiddle.db");
  db_.open(dbFile);
  mixer_.setRenderWorkerCount(juce::String(db_.loadSetting("audio_render_workers", "1")).getIntValue());
  scanPersistedExpressionMapSources();
  migrateLegacyLibraryPatches();

  // Create the VersionStore backed by the database's SqliteVersionStorage.
  // This must happen immediately after db_.open() so that all downstream
  // code (pushDagHistory, pushBranches, stateManager) can use it.
  if (auto *storage = db_.getVersionStorage()) {
    const auto garbageCollection =
        storage->garbageCollectUnreferencedObjects(true);
    if (!garbageCollection.succeeded()) {
      std::cerr << "[MainComponent] Version-object garbage collection failed: "
                << garbageCollection.error << std::endl;
    } else if (garbageCollection.fiddleStatesRemoved > 0 ||
               garbageCollection.stripBlobsRemoved > 0) {
      std::cerr << "[MainComponent] Version-object garbage collection removed "
                << garbageCollection.fiddleStatesRemoved << " states and "
                << garbageCollection.stripBlobsRemoved << " strip blobs"
                << (garbageCollection.compacted ? "; database compacted" : "")
                << std::endl;
    }

    versionStore_ = std::make_unique<versioning::VersionStore>(*storage);

    // On first run, the branches table is empty — seed with an empty root.
    if (versionStore_->getStorage().listBranches().empty()) {
      versionStore_->initializeEmpty();
      std::cerr << "[MainComponent] VersionStore seeded with empty root"
                << std::endl;
    } else {
      std::cerr << "[MainComponent] VersionStore loaded ("
                << versionStore_->getStorage().listBranches().size()
                << " branches)" << std::endl;
    }

    // ── Dedup guard: remove stale duplicate-named branches ───────────────
    // This can accumulate during development if the DB is partially reset
    // without wiping the branches table. For each duplicate name, keep the
    // branch whose head version is most recent (highest created_at); delete
    // the others.
    {
      auto allBranches = versionStore_->getStorage().listBranches();
      // Map name → (branchId, headId, createdAt)
      std::map<std::string, std::tuple<versioning::BranchId,
                                       versioning::VersionId, std::string>>
          bestByName;

      for (const auto &[bid, name, headId] : allBranches) {
        std::string headCreatedAt;
        if (auto ver = versionStore_->getStorage().getVersion(headId))
          headCreatedAt = ver->createdAt;

        auto it = bestByName.find(name);
        if (it == bestByName.end()) {
          bestByName[name] = {bid, headId, headCreatedAt};
        } else {
          // Keep whichever head is newer
          const std::string &existingTs = std::get<2>(it->second);
          if (headCreatedAt > existingTs) {
            // Current entry is older — delete it
            std::cerr << "[MainComponent] Dedup: removing stale branch '"
                      << name << "' id=" << std::get<0>(it->second)
                      << std::endl;
            versionStore_->deleteBranch(std::get<0>(it->second));
            it->second = {bid, headId, headCreatedAt};
          } else {
            // New entry is older — delete it
            std::cerr << "[MainComponent] Dedup: removing stale branch '"
                      << name << "' id=" << bid << std::endl;
            versionStore_->deleteBranch(bid);
          }
        }
      }
    }

    // ── Orphan guard: remove branches whose head version is claimed by a
    // different branch. This catches the case where a stale branch record
    // points to a version whose branchId is already owned by another branch.
    {
      auto allBranches = versionStore_->getStorage().listBranches();
      for (const auto &[bid, name, headId] : allBranches) {
        auto ver = versionStore_->getStorage().getVersion(headId);
        if (ver && !ver->branchId.empty() && ver->branchId != bid) {
          std::cerr << "[MainComponent] Orphan branch '" << name
                    << "' (id=" << bid << "): head version belongs to branch "
                    << ver->branchId << " — removing record only." << std::endl;
          // Only delete the branch record; its versions belong to another
          // branch and must not be removed.
          versionStore_->getStorage().deleteBranch(bid);
        }
      }
    }

    // Wire the version store into the state manager so it embeds ancestor
    // hashes in the Dorico blob.
    stateManager_.setVersionStore(versionStore_.get());
    stateManager_.setRoutingRepository(db_.getLibraryRoutingRepository());

    // Track the current branch (default to first branch, i.e. "Main").
    auto allBranches = versionStore_->getStorage().listBranches();
    if (!allBranches.empty()) {
      currentBranchId_ = std::get<0>(allBranches[0]);
      stateManager_.setCurrentBranchId(currentBranchId_);

      // Initialise currentVersionId_ so the History window can highlight
      // the loaded version immediately on first open.
      auto headOpt = versionStore_->getBranchHead(currentBranchId_);
      if (headOpt)
        currentVersionId_ = *headOpt;
    }
  } else {
    std::cerr << "[MainComponent] WARNING: VersionStore not available — "
                 "DAG history will be disabled"
              << std::endl;
  }

  // Now that the DB is open, restore debug window geometry + visibility.
  if (debugWindow_) {
    auto dws = db_.loadWindowSettings("debug");
    std::cerr << "[DebugWindow] Loaded settings: visible="
              << (dws.visible ? "true" : "false") << std::endl;
    if (dws.width > 0 && dws.height > 0) {
      debugWindow_->restoreGeometry(dws.x, dws.y, dws.width, dws.height,
                                    dws.visible);
    }
    // If no saved geometry, window stays hidden (constructor default).
  }

  // Restore history window if it was previously visible.
  // Deferred to the message loop so the main WebView is fully initialised.
  {
    auto hws = db_.loadWindowSettings("history");
    if (hws.visible) {
      safeCallAsync([this, hws]() {
        if (!historyWindow_ && versionStore_) {
          historyWindow_ = std::make_unique<HistoryWindow>(
              webViewBridge_.createWebOptions());
          historyWindowLoaded_ = false;
          juce::String root =
              juce::WebBrowserComponent::getResourceProviderRoot();
          historyWindow_->getWebView().goToURL(root +
                                               "index.html?view=history");
          if (hws.width > 0 && hws.height > 0) {
            historyWindow_->restoreGeometry(hws.x, hws.y, hws.width, hws.height,
                                            true);
          } else {
            historyWindow_->setVisible(true);
          }
          std::cerr << "[HistoryWindow] Restored from saved settings"
                    << std::endl;
        }
      });
    }
  }

  // Restore library manager window if it was previously visible.
  {
    auto lws = db_.loadWindowSettings("library");
    if (lws.visible) {
      safeCallAsync([this, lws]() {
        if (!libraryManagerWindow_) {
          libraryManagerWindow_ = std::make_unique<LibraryManagerWindow>(
              webViewBridge_.createWebOptions());
          libraryManagerWindowLoaded_ = false;
          juce::String root =
              juce::WebBrowserComponent::getResourceProviderRoot();
          libraryManagerWindow_->getWebView().goToURL(
              root + "index.html?view=library");
          libraryManagerWindow_->setGeometrySaver(
              [this]() { saveLibraryManagerWindowGeometry(); });
          if (lws.width > 0 && lws.height > 0) {
            libraryManagerWindow_->restoreGeometry(lws.x, lws.y, lws.width,
                                                   lws.height, true);
          } else {
            libraryManagerWindow_->setVisible(true);
          }
          std::cerr << "[LibraryManager] Restored from saved settings"
                    << std::endl;
        }
      });
    }
  }

  // Now that the DB is open, restore main window geometry.
  {
    auto bounds = restoreMainWindowGeometry();
    if (auto *topLevel = getTopLevelComponent()) {
      topLevel->setBounds(bounds);
    }
  }

  // Load ensemble from DB (with one-time migration from legacy JSON).
  if (masterList_.loadFromDB(db_)) {
    // Load stable channel assignments (first run: assigns sequentially)
    masterList_.reconcileAssignments(db_);
    pushLogMessage("<b>[Setup]</b> Loaded " + juce::String(masterList_.size()) +
                   " saved instruments");
  }

  // Initialize shadow state manager (creates shared memory file)
  stateManager_.initialize();
  // Set config name to the current branch name for Dorico blob compat.
  if (versionStore_ && !currentBranchId_.empty()) {
    auto branchOpt = versionStore_->getStorage().getBranch(currentBranchId_);
    if (branchOpt)
      stateManager_.setConfigName(juce::String(branchOpt->first));
    else
      stateManager_.setConfigName("default");
  } else {
    stateManager_.setConfigName("default");
  }

  // Pre-cache config_status for immediate push to clients.
  if (server) {
    fiddle::MidiEvent msg;
    msg.set_timestamp_samples(0);
    auto *cs = msg.mutable_config_status();
    cs->set_config_name(stateManager_.getConfigName().toStdString());
    cs->set_config_version("");
    cs->set_dirty(false);
    cs->set_delay_ms(1000); // default, updated later
    cs->set_branch_id(currentBranchId_);
    cs->set_version_id(currentVersionId_);
    server->setCachedConfigStatus(msg);
  }
  safeCallAsync([this]() { initPluginsAndStrips(); });
}

void MainComponent::migrateLegacyLibraryPatches() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;

  const auto toRoman = [](int value) {
    static const std::pair<int, const char *> numerals[] = {
        {10, "X"}, {9, "IX"}, {5, "V"}, {4, "IV"}, {1, "I"}};
    juce::String result;
    for (const auto &[number, numeral] : numerals) {
      while (value >= number) {
        result += numeral;
        value -= number;
      }
    }
    return result;
  };

  int migratedLibraries = 0;
  int migratedPatches = 0;
  for (const auto &library : db_.listLibraries()) {
    const auto libraryId = library.id.toStdString();
    if (!repository->listPatches(libraryId).empty())
      continue;

    const auto legacyRows = db_.loadLibraryInstruments(library.id);
    if (legacyRows.empty())
      continue;

    std::map<std::pair<juce::String, bool>, int> duplicateCounts;
    for (const auto &row : legacyRows)
      ++duplicateCounts[{row.entityId, row.isSolo}];

    int position = 0;
    int importedForLibrary = 0;
    for (const auto &row : legacyRows) {
      LibraryPatchRow patch;
      patch.id = juce::Uuid().toString().toStdString();
      patch.libraryId = libraryId;
      patch.position = position++;
      patch.name = row.name.toStdString();
      if (duplicateCounts[{row.entityId, row.isSolo}] > 1) {
        const int instance =
            row.instanceNums.empty() ? 1 : row.instanceNums.front();
        const auto suffix =
            row.isSolo ? juce::String(instance) : toRoman(instance);
        patch.name += (" " + suffix).toStdString();
      }
      patch.instrumentEntityId = row.entityId.toStdString();
      patch.family = row.family.toStdString();
      patch.character = row.isSolo ? "solo" : "section";
      patch.pluginUid = row.pluginUid;
      if (patch.pluginUid == 0 && row.vstPlugin.isNotEmpty())
        patch.pluginUid = row.vstPlugin.getIntValue();
      if (!row.pluginState.isEmpty()) {
        const auto *bytes = static_cast<const std::uint8_t *>(
            row.pluginState.getData());
        patch.pluginState.assign(bytes,
                                 bytes + row.pluginState.getSize());
      }
      patch.expressionMapId = row.exprMap.toStdString();
      if (repository->upsertPatch(patch)) {
        ++importedForLibrary;
        ++migratedPatches;
      }
    }
    if (importedForLibrary > 0)
      ++migratedLibraries;
  }

  if (migratedPatches > 0) {
    std::cerr << "[LibraryMigration] Imported " << migratedPatches
              << " legacy rows as patches across " << migratedLibraries
              << " libraries" << std::endl;
  }
}

void MainComponent::initPluginsAndStrips() {
  // Load plugins and strips from database
  addInitMessage("Loading plugins...");

  // Load persisted plugin listener capabilities
  listenerCapableUids_ = db_.loadListenerCapableUids();
  if (!listenerCapableUids_.empty()) {
    std::cerr << "[MainComponent] Loaded " << listenerCapableUids_.size()
              << " listener-capable plugin UIDs from DB" << std::endl;
  }

  // Normal load from SQLite
  loadStripsFromDB();

  // Refresh the catalog only after the cached catalog has been restored and
  // used to start strip restoration.  Running these concurrently made both
  // paths mutate KnownPluginList at once during startup.
  std::cerr << "[Startup] Auto-scanning for VST3 plugins..." << std::endl;
  pluginScanner_.scanIncrementalAsync(db_, [this]() {
    int count = pluginScanner_.getPluginCount();
    std::cerr << "[Startup] Plugin scan complete: " << count << " plugins found"
              << std::endl;
    juce::String json = pluginScanner_.getPluginListAsJson();
    broadcastMessage("setPluginList", juce::JSON::fromString(json));
  });

  // Chairs/layers are the sole live routing model. An empty chair roster
  // intentionally produces an empty mixer rather than reviving legacy strips.
  syncMixerToLayers();

  // Existing branch heads predate routing snapshots. Make the one-time
  // upgrade visible as an unsaved change so the next Fiddle or Dorico save
  // records even an intentionally empty chair roster.
  if (versionStore_ && !currentVersionId_.empty()) {
    const auto version = versionStore_->getVersion(currentVersionId_);
    const auto savedState =
        version ? versionStore_->getState(version->stateHash) : std::nullopt;
    if (savedState && savedState->routingState.schemaVersion == 0) {
      stateManager_.markDirty();
      broadcastMessage("setDirtyState", true);
      pushLogMessage("<b>[Upgrade]</b> Save once to add chair/layer data to "
                     "this branch's version history.");
    }
  }

  pushMixerState(false);

  projectRestoreReady_ = true;
  if (pendingDoricoProject_) {
    const auto request = std::move(*pendingDoricoProject_);
    pendingDoricoProject_.reset();
    restoreDoricoProject(request.legacyBranchName, request.branchId,
                         request.versionId);
  }

  // Build initial shadow state blob
  scheduleStateRebuild();

  // Cache config_status so new client connections get it immediately
  pushConfigStatus();

  initAgentControl();

  // Wait for the WebView to signal it's ready before dismissing splash
  addInitMessage("Preparing interface...");
}

bool MainComponent::saveConfig(const std::optional<std::string> &newBranchName) {
  if (projectRestoreService_ && projectRestoreService_->isLoading()) {
    pushLogMessage("<b>[Save]</b> Please wait for project plug-ins to finish loading.", true);
    return false;
  }
  versioning::ProjectSaveResult result;
  try {
    // Freeze every vendor state blob once, then reuse those exact bytes for
    // both session persistence and the version snapshot. A dirty hint alone
    // must not manufacture a branch after undo.
    mixer_.capturePluginStateCachesForSave();
    saveAllStripsToDB(false);
    result = stateManager_.saveCurrentState(
        mixer_, currentBranchId_, currentVersionId_, newBranchName, false);
  } catch (const std::exception &error) {
    result.error = error.what();
  }
  if (!result.succeeded()) {
    stateManager_.markDirty();
    broadcastMessage("setDirtyState", true);
    pushConfigStatus();
    pushLogMessage("<b>[Save]</b> Failed: " + juce::String(result.error), true);
    return false;
  }

  currentBranchId_ = result.branchId;
  currentVersionId_ = result.versionId;
  stateManager_.setCurrentBranchId(result.branchId);
  stateManager_.setConfigName(juce::String(result.branchName));
  isDetached_ = versionStore_->getBranchHead(result.branchId) !=
                std::optional<versioning::VersionId>(result.versionId);
  stateManager_.clearDirty();
  undoManager_.markSavePoint();
  broadcastMessage("setDirtyState", false);
  pushConfigStatus(); // Advertise the fork's identity before acknowledging save.
  captureStripPluginFingerprints();

  if (result.kind == versioning::ProjectSaveResult::Kind::Branched)
    pushLogMessage("<b>[Save]</b> Created branch: " + juce::String(result.branchName));
  else if (result.kind == versioning::ProjectSaveResult::Kind::Committed)
    pushLogMessage("<b>[Save]</b> Committed to branch");
  else
    pushLogMessage("<b>[Save]</b> Unchanged; retained saved version");

  // WebKit updates are deferred until after the save callback unwinds.
  safeCallAsync([this]() {
    pushBranches();
    broadcastMessage("setCurrentBranch", juce::String(currentBranchId_));
    pushDagHistory();
    pushCurrentVersion();
  });
  scheduleStateRebuild(); // Unchanged historical saves keep their exact identity.
  return true;
}

void MainComponent::handleDoricoSaveRequest(uint64_t requestId) {
  // Consume any plug-in editor notification that arrived immediately before
  // Dorico invoked getState. Ordinary playback callbacks remain suppressed.
  const auto now = juce::Time::getMillisecondCounter();
  const bool suppressPlaybackChanges = shouldSuppressPluginChanges(now);
  processPluginChangeNotifications(suppressPlaybackChanges);
  mixer_.masterAudio().consumePluginChanges(suppressPlaybackChanges);
  for (auto *strip : mixer_.getAllStrips())
    strip->audioEngine().consumePluginChanges(suppressPlaybackChanges);

  const bool saveSucceeded = !stateManager_.isDirty() || saveConfig();

  fiddle::MidiEvent response;
  response.set_timestamp_samples(0);
  auto *saved = response.mutable_save_config_response();
  saved->set_request_id(requestId);

  const bool success = saveSucceeded && versionStore_ &&
                       !currentBranchId_.empty() && !currentVersionId_.empty() &&
                       !stateManager_.isDirty();
  saved->set_success(success);
  saved->set_branch_id(currentBranchId_);
  saved->set_version_id(currentVersionId_);
  saved->set_config_version("");

  if (versionStore_ && !currentBranchId_.empty()) {
    if (const auto branch =
            versionStore_->getStorage().getBranch(currentBranchId_))
      saved->set_config_name(branch->first);
  }
  if (!success)
    saved->set_error("FiddleServer could not commit the current state");

  if (server)
    server->sendToClient(response);
}

void MainComponent::pushConfigStatus() {
  if (!server)
    return;

  // Use the current branch name as the config_name for Dorico compatibility.
  std::string branchName = "default";
  if (versionStore_ && !currentBranchId_.empty()) {
    auto branchOpt = versionStore_->getStorage().getBranch(currentBranchId_);
    if (branchOpt)
      branchName = branchOpt->first;
  }

  fiddle::MidiEvent msg;
  msg.set_timestamp_samples(0);
  auto *cs = msg.mutable_config_status();
  cs->set_config_name(branchName);
  cs->set_config_version("");
  cs->set_dirty(stateManager_.isDirty());
  cs->set_delay_ms(effectivePlaybackDelayMs());
  reportedPlaybackDelayMs_.store(cs->delay_ms(), std::memory_order_relaxed);
  if (renderAhead_) cs->set_audio_stream_id(renderAhead_->streamId());
  cs->set_branch_id(currentBranchId_);
  cs->set_version_id(currentVersionId_);

  // Cache for immediate push on future client connections
  server->setCachedConfigStatus(msg);
  server->sendToClient(msg);
}

void MainComponent::saveAllStripsToDB(bool captureLiveState) {
  if (projectRestoreService_ && projectRestoreService_->isLoading())
    return;
  db_.saveSetting("project_settings", mixer_.projectSettings().serialize());
  auto strips = mixer_.getAllStrips();
  auto *routingRepository = db_.getLibraryRoutingRepository();
  // First, clear and re-save all strips with correct positions
  db_.clearStrips();
  for (int i = 0; i < (int)strips.size(); ++i) {
    db_.saveStrip(*strips[i], i);
    // Capture once, then use the same exact bytes for both the session row
    // and the version commit that follows.
    if (captureLiveState) strips[i]->refreshPluginStateCache();
    const auto block = strips[i]->cachedPluginState();
    if (!block.isEmpty())
      db_.savePluginBlob(strips[i]->id, block);
    db_.saveStripAudio(strips[i]->id,
                       strips[i]->audioEngine().snapshotAll(captureLiveState));

    // A layer row owns the durable mixer defaults for an explicitly assigned
    // patch. Transitional free-standing strips have no matching layer row.
    if (routingRepository) {
      auto layer = routingRepository->getLayer(strips[i]->id.toStdString());
      if (layer) {
        const auto state = strips[i]->realtimeState();
        layer->active = state.active;
        layer->muted = state.muted;
        layer->soloed = state.soloed;
        layer->gainDb = state.gainDb;
        layer->pluginUid = strips[i]->pluginUid;
        layer->pluginState.clear();
        if (!block.isEmpty()) {
          const auto *bytes =
              static_cast<const std::uint8_t *>(block.getData());
          layer->pluginState.assign(bytes, bytes + block.getSize());
        }
        layer->expressionMapId =
            strips[i]->expressionMap ? strips[i]->expressionMap->entityID
                                     : std::string{};
        routingRepository->upsertLayer(*layer);
      }
    }
  }

  // Save plugin scanner cache
  if (auto xml = pluginScanner_.getKnownPluginList().createXml()) {
    db_.saveSetting("plugin_cache", xml->toString().toStdString());
  }
  saveMasterAudioToDB(captureLiveState);
}

void MainComponent::saveMasterAudioToDB(bool captureLiveState) {
  if (db_.isOpen())
    db_.saveMasterAudio(mixer_.masterAudio().snapshotAll(captureLiveState));
}

void MainComponent::saveStripToDB(const juce::String &stripId) {
  int idx = mixer_.stripIndex(stripId);
  if (auto *s = mixer_.getStrip(stripId)) {
    db_.saveStrip(*s, idx >= 0 ? idx : 0);
  }
}

void MainComponent::scheduleStateRebuild() {
  if (projectRestoreService_ && projectRestoreService_->isLoading())
    return;
  // Historical edits are unpublished until saved onto their own branch. The
  // shadow blob uses branch-head ancestry, which would be wrong while detached.
  if (isDetached_)
    return;

  auto now = juce::Time::getMillisecondCounter();
  if (now - lastStateRebuildMs_ < 1000) {
    // Too soon — mark pending; the timer will pick it up
    stateRebuildPending_ = true;
    return;
  }
  lastStateRebuildMs_ = now;
  stateRebuildPending_ = false;
  stateManager_.scheduleRebuild([this]() -> juce::MemoryBlock {
    // A restore may start after this rebuild was queued.
    if (isDetached_ || (projectRestoreService_ && projectRestoreService_->isLoading()))
      return {};
    return stateManager_.buildStateBlob(mixer_);
  });
}

void MainComponent::setupStripPluginSlot(MixerStrip &strip) {
  strip.updatePluginSlotId();
  const auto stripId = strip.id;
  juce::Component::SafePointer<MainComponent> safeThis(this);
  strip.onEditorVisibilityChanged = [safeThis, stripId] {
    if (safeThis == nullptr)
      return;
    if (auto *current = safeThis->mixer_.getStrip(stripId)) {
      auto *state = new juce::DynamicObject();
      state->setProperty("stripId", stripId);
      state->setProperty("editorOpen", current->isEditorVisible());
      safeThis->broadcastMessage("setInstrumentEditorState", juce::var(state));
    }
  };
  strip.audioEngine().setOnChanged([safeThis, stripId] {
    if (safeThis != nullptr)
      safeThis->stripAudioChanged(stripId);
  });
  strip.audioEngine().setOnEditorVisibilityChanged([safeThis, stripId] {
    if (safeThis == nullptr)
      return;
    if (auto *current = safeThis->mixer_.getStrip(stripId)) {
      auto state = current->audioEngine().toJson();
      if (auto *object = state.getDynamicObject())
        object->setProperty("stripId", stripId);
      safeThis->broadcastMessage("setStripAudioState", state);
    }
  });
}

void MainComponent::setupGroupBusAudioCallbacks(GroupBus &bus) {
  const auto busId = bus.id;
  juce::Component::SafePointer<MainComponent> safeThis(this);
  bus.audioEngine().setOnChanged([safeThis, busId] {
    if (safeThis != nullptr)
      safeThis->groupBusAudioChanged(busId);
  });
  bus.audioEngine().setOnEditorVisibilityChanged([safeThis] {
    if (safeThis != nullptr)
      safeThis->pushGroupBusState();
  });
}

void MainComponent::restoreStripPlugin(MixerStrip &strip, int pluginUid,
                                       const juce::MemoryBlock &state) {
  pluginFingerprints_.erase(strip.id);
  if (pluginUid == 0)
    return;

  strip.pluginUid = pluginUid;
  for (const auto &description :
       pluginScanner_.getKnownPluginList().getTypes()) {
    if (description.uniqueId != pluginUid)
      continue;

    const auto stripId = strip.id;
    juce::Component::SafePointer<MainComponent> safeThis(this);
    strip.loadPlugin(description, mixer_.getFormatManager(),
                     [safeThis, stripId, pluginUid, state](bool success) {
                       if (safeThis == nullptr)
                         return;
                       auto *currentStrip = safeThis->mixer_.getStrip(stripId);
                       if (currentStrip == nullptr)
                         return;

                       if (success) {
                         (void)currentStrip
                             ->consumePluginChangeNotification();
                         (void)currentStrip
                             ->consumePluginExplicitEditNotification();
                         (void)currentStrip
                             ->consumePluginNonParameterStateChangeNotification();
                         safeThis->pluginFingerprints_[stripId] = {
                             currentStrip->pluginUid,
                             currentStrip->pluginParameterFingerprint()};
                         safeThis->pluginStateSettleUntilMs_[stripId] =
                             juce::Time::getMillisecondCounter() +
                             kPluginStateSettleMs;
                       }
                       safeThis->pushMixerState(false);
                       safeThis->scheduleStateRebuild();
                     }, state);
    return;
  }

  strip.markPluginMissing(pluginUid, state,
                          "Plug-in is not present in the scanned catalog");
}

void MainComponent::processPluginChangeNotifications(
    bool suppressPlaybackChanges) {
  bool changed = false;
  std::vector<std::string> editedLayerIds;
  for (auto *strip : mixer_.getAllStrips()) {
    const bool explicitEdit =
        strip->consumePluginExplicitEditNotification();
    const bool nonParameterStateChanged =
        strip->consumePluginNonParameterStateChangeNotification();
    if (!strip->consumePluginChangeNotification() && !explicitEdit &&
        !nonParameterStateChanged)
      continue;

    if (strip->pluginUid != 0 &&
        listenerCapableUids_.insert(strip->pluginUid).second)
      db_.savePluginCapability(strip->pluginUid);

    // JUCE mirrors incoming VST3 MIDI controllers into parameter values and
    // emits ordinary and sometimes non-parameter callbacks. Keep that
    // playback state out of the project's dirty flag while preserving genuine
    // editor gestures. Outside performance, a non-parameter notification is a
    // persistent change even if the public parameter surface is unchanged.
    if (suppressPlaybackChanges && !explicitEdit)
      continue;
    // Fingerprinting takes the process-wide control gate. Do not interrupt
    // rendering for notifications that we already know are performance-only.
    const bool parametersChanged = observeStripPluginFingerprint(*strip);
    bool stateSettling = false;
    if (const auto settling = pluginStateSettleUntilMs_.find(strip->id);
        settling != pluginStateSettleUntilMs_.end()) {
      const auto now = juce::Time::getMillisecondCounter();
      if (static_cast<juce::int32>(now - settling->second) < 0) {
        stateSettling = true;
      } else {
        pluginStateSettleUntilMs_.erase(settling);
      }
    }
    if (stateSettling && !explicitEdit)
      continue;
    if (explicitEdit)
      pluginStateSettleUntilMs_.erase(strip->id);
    if (!parametersChanged && !explicitEdit && !nonParameterStateChanged)
      continue;

    changed = true;
    strip->refreshPluginStateCache();
    if (strip->chairId.isNotEmpty())
      editedLayerIds.push_back(strip->id.toStdString());
  }

  if (!changed)
    return;

  if (auto *repository = db_.getLibraryRoutingRepository()) {
    for (const auto &layerId : editedLayerIds)
      repository->markLayerPluginStateEdited(layerId);
  }

  undoManager_.noteExternalChange();
  stateManager_.markDirty();
  pushConfigStatus();
  broadcastMessage("setDirtyState", true);
  pushMixerState(false);
  scheduleStateRebuild();
}

void MainComponent::pluginProgramApplied(const juce::String &stripId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return;

  (void)strip->consumePluginChangeNotification();
  (void)strip->consumePluginExplicitEditNotification();
  (void)strip->consumePluginNonParameterStateChangeNotification();
  pluginFingerprints_[stripId] = {strip->pluginUid,
                                  strip->pluginParameterFingerprint()};
  pluginStateSettleUntilMs_[stripId] =
      juce::Time::getMillisecondCounter() + kPluginStateSettleMs;
}

bool MainComponent::shouldSuppressPluginChanges(uint32_t now) {
  const bool transportStarted =
      isTransportStarted_.load(std::memory_order_relaxed);
  if (transportStarted) {
    pluginPlaybackSettleUntilMs_ = now + 500;
    pluginPlaybackSettling_ = true;
  } else if (pluginPlaybackSettling_ &&
             static_cast<juce::int32>(now - pluginPlaybackSettleUntilMs_) >=
                 0) {
    pluginPlaybackSettling_ = false;
  }

  const auto lastPerformanceActivity =
      lastPluginPerformanceActivityMs_.load(std::memory_order_acquire);
  const bool performanceContext =
      transportStarted || pluginPlaybackSettling_ ||
      hasRecentPluginPerformanceActivity(now, lastPerformanceActivity);
  if (performanceContext) {
    pluginChangesWereSuppressed_ = true;
    return true;
  }

  if (pluginChangesWereSuppressed_) {
    // Capture MIDI-driven parameter values before polling resumes. Return one
    // final suppressed cycle as well, so pending callbacks from the tail of
    // the window are drained using playback semantics.
    captureStripPluginFingerprints();
    mixer_.masterAudio().captureParameterFingerprints();
    // captureStripPluginFingerprints already covers strip inserts. Bus inserts
    // also need a new baseline before unsuppressed polling resumes.
    for (auto *bus : mixer_.getAllGroupBuses())
      bus->audioEngine().captureParameterFingerprints();
    pluginChangesWereSuppressed_ = false;
    return true;
  }

  return false;
}

bool MainComponent::observeStripPluginFingerprint(MixerStrip &strip) {
  if (!strip.hasPlugin()) {
    pluginFingerprints_.erase(strip.id);
    return false;
  }

  const StripPluginFingerprint current{strip.pluginUid,
                                       strip.pluginParameterFingerprint()};
  auto [it, inserted] = pluginFingerprints_.try_emplace(strip.id, current);
  if (inserted)
    return false;

  // A plug-in replacement is already tracked by its undoable mixer command.
  // Treat the new instance as a fresh baseline here.
  if (it->second.pluginUid != current.pluginUid) {
    it->second = current;
    return false;
  }

  if (it->second.parameters == current.parameters)
    return false;

  it->second = current;
  return true;
}

void MainComponent::captureStripPluginFingerprints() {
  pluginFingerprints_.clear();
  for (auto *strip : mixer_.getAllStrips()) {
    if (strip->hasPlugin()) {
      pluginFingerprints_.emplace(
          strip->id,
          StripPluginFingerprint{strip->pluginUid,
                                 strip->pluginParameterFingerprint()});
    }
    strip->audioEngine().captureParameterFingerprints();
  }
}

void MainComponent::pollPluginStateChanges() {
  auto strips = mixer_.getAllStrips();
  const auto now = juce::Time::getMillisecondCounter();
  for (auto *strip : strips) {
    if (!observeStripPluginFingerprint(*strip))
      continue;

    if (const auto settling = pluginStateSettleUntilMs_.find(strip->id);
        settling != pluginStateSettleUntilMs_.end()) {
      if (static_cast<juce::int32>(now - settling->second) < 0)
        continue;
      pluginStateSettleUntilMs_.erase(settling);
    }

    std::cerr << "[PluginPoll] Parameter change detected in strip: "
              << strip->id << " (" << strip->library << ")" << std::endl;
    strip->refreshPluginStateCache();
    if (strip->chairId.isNotEmpty()) {
      if (auto *repository = db_.getLibraryRoutingRepository())
        repository->markLayerPluginStateEdited(strip->id.toStdString());
    }
    undoManager_.noteExternalChange();
    stateManager_.markDirty();
    pushConfigStatus();
    broadcastMessage("setDirtyState", true);
    pushMixerState(false);
    scheduleStateRebuild();
    return; // One dirty notification per poll cycle is enough
  }
}

void MainComponent::loadStripsFromDB() {
  mixer_.setProjectSettings(ProjectSettings::deserialize(
      db_.loadSetting("project_settings")));
  // Try new plugin_cache table first
  auto cachedPlugins = db_.loadPluginCache();
  if (!cachedPlugins.empty()) {
    for (const auto &row : cachedPlugins) {
      juce::PluginDescription desc;
      desc.name = row.name;
      desc.manufacturerName = row.manufacturer;
      desc.category = row.category;
      desc.pluginFormatName = row.format;
      desc.uniqueId = row.uid;
      desc.numInputChannels = row.numInputs;
      desc.numOutputChannels = row.numOutputs;
      desc.isInstrument = row.isInstrument;
      desc.fileOrIdentifier = row.path;
      pluginScanner_.getKnownPluginListMutable().addType(desc);
    }
    std::cerr << "[loadDB] Restored " << cachedPlugins.size()
              << " plugins from plugin_cache table" << std::endl;
  } else {
    // Backward compat: try old XML blob in settings table
    std::string cacheXml = db_.loadSetting("plugin_cache");
    if (!cacheXml.empty()) {
      auto xml = juce::parseXML(juce::String(cacheXml));
      if (xml) {
        pluginScanner_.getKnownPluginListMutable().recreateFromXml(*xml);
        std::cerr << "[loadDB] Restored plugin cache from XML: "
                  << pluginScanner_.getPluginCount() << " plugins" << std::endl;
      }
    }
  }

  mixer_.clear();
  auto rows = db_.loadAllStrips();
  for (auto &row : rows) {
    juce::String newId = mixer_.addStrip();
    if (auto *strip = mixer_.getStrip(newId)) {
      strip->id = row.id;
      strip->library = row.library;
      strip->family = row.family;
      strip->isSolo = row.isSolo;
      strip->setActive(row.active);
      strip->setMuted(row.muted);
      strip->setSoloed(row.soloed);
      strip->setInputAssignment(row.inputPort, row.inputChannel);
      strip->setGainDb(row.gainDb);

      // Wire up plugin change listener for dirty detection
      setupStripPluginSlot(*strip);

      // Restore expression map
      if (!row.expressionMapEntityID.empty()) {
        auto data = xmapLibrary_.loadPersisted(row.expressionMapEntityID,
                                               row.expressionMapSourceXml);
        if (data) {
          const auto mapName = data->name;
          strip->setExpressionMapAssignment(
              {std::move(data), row.expressionMapPath,
               row.expressionMapSourceXml});
          std::cerr << "[loadDB] Restored xmap '" << mapName
                    << "' for strip " << strip->id << std::endl;
        }
      }

      restoreStripPlugin(*strip, row.pluginUid, row.pluginState);
      restoreStripAudio(*strip, row.audio, false);

      // Restore Lua plugins
      for (const auto &fileName : row.luaPluginFileNames) {
        auto resolved = luaCatalog_.resolvePluginPath(fileName);
        auto plugin = std::make_shared<LuaPlugin>(
            resolved.empty() ? fileName : resolved);
        if (!resolved.empty() && plugin->load()) {
          std::cerr << "[loadDB] Restored Lua plugin '" << fileName
                    << "' for strip " << strip->id << std::endl;
        } else {
          std::cerr << "[loadDB] Lua plugin unavailable; retaining unloaded "
                       "entry: "
                    << fileName << std::endl;
        }
        strip->addLuaPlugin(std::move(plugin));
      }
    }
  }
  restoreMasterAudio(db_.loadMasterAudio(), true);
  std::cerr << "[MainComponent] Loaded " << rows.size() << " strips from SQLite"
            << std::endl;
}

void MainComponent::restoreMasterAudio(const MasterAudioSnapshot &snapshot,
                                       bool publishWhenLoaded) {
  auto &master = mixer_.masterAudio();
  master.clear(false);
  master.setGainDb(snapshot.gainDb, false);

  juce::Component::SafePointer<MainComponent> safeThis(this);
  for (int index = 0; index < static_cast<int>(snapshot.inserts.size());
       ++index) {
    master.insert(
        snapshot.inserts[static_cast<std::size_t>(index)], index,
        mixer_.getFormatManager(),
        [safeThis, publishWhenLoaded](bool, const juce::String &) {
          if (publishWhenLoaded && safeThis != nullptr)
            safeThis->pushMasterAudioState();
        },
        false);
  }

  if (publishWhenLoaded)
    pushMasterAudioState();
}


void MainComponent::restoreMasterAudio(const RestoredProjectState &state,
                                       bool publishWhenLoaded) {
  MasterAudioSnapshot snapshot;
  snapshot.gainDb = state.masterGainDb;
  snapshot.inserts.reserve(state.masterInserts.size());
  for (const auto &saved : state.masterInserts) {
    MasterInsertSnapshot insert;
    insert.slotId = saved.slotId;
    insert.description.pluginFormatName = saved.formatName;
    insert.description.uniqueId = saved.pluginUid;
    insert.description.fileOrIdentifier = saved.fileOrIdentifier;
    insert.description.manufacturerName = saved.manufacturer;
    insert.description.name = saved.name;
    insert.description.category = saved.category;
    insert.description.version = saved.pluginVersion;
    insert.description.numInputChannels = saved.numInputChannels;
    insert.description.numOutputChannels = saved.numOutputChannels;
    insert.bypassed = saved.bypassed;
    insert.pluginState = saved.pluginState;
    snapshot.inserts.push_back(std::move(insert));
  }
  restoreMasterAudio(snapshot, publishWhenLoaded);
}


void MainComponent::restoreStripAudio(MixerStrip &strip,
                                      const StripAudioSnapshot &snapshot,
                                      bool publishWhenLoaded) {
  auto &audio = strip.audioEngine();
  audio.clear(false);
  const auto stripId = strip.id;
  juce::Component::SafePointer<MainComponent> safeThis(this);
  const auto restoreRack = [&](const std::vector<AudioInsertSnapshot> &rack,
                               StripInsertPosition position) {
    for (int index = 0; index < static_cast<int>(rack.size()); ++index) {
      audio.insert(
          rack[static_cast<std::size_t>(index)], position, index,
          mixer_.getFormatManager(),
          [safeThis, stripId, publishWhenLoaded](bool, const juce::String &) {
            if (publishWhenLoaded && safeThis != nullptr) {
              if (auto *current = safeThis->mixer_.getStrip(stripId)) {
                auto state = current->audioEngine().toJson();
                if (auto *object = state.getDynamicObject())
                  object->setProperty("stripId", stripId);
                safeThis->broadcastMessage("setStripAudioState", state);
              }
              safeThis->pushMixerState(false);
            }
          },
          false);
    }
  };
  restoreRack(snapshot.preFaderInserts, StripInsertPosition::preFader);
  restoreRack(snapshot.postFaderInserts, StripInsertPosition::postFader);
  if (publishWhenLoaded)
    pushMixerState(false);
}

void MainComponent::restoreStripAudio(MixerStrip &strip,
                                      const juce::MemoryBlock &state,
                                      bool publishWhenLoaded) {
  restoreStripAudio(strip,
                    deserializeStripAudioSnapshot(state.getData(),
                                                  state.getSize()),
                    publishWhenLoaded);
}

bool MainComponent::applyVersionState(const versioning::FiddleState &state) {
  if (!projectRestoreService_) {
    ProjectRestoreService::Callbacks callbacks;
    callbacks.findInstrument = [this](int uid) -> std::optional<juce::PluginDescription> {
      for (const auto &description : pluginScanner_.getKnownPluginList().getTypes())
        if (description.uniqueId == uid)
          return description;
      return std::nullopt;
    };
    callbacks.loadMap = [this](const std::string &id,
                               const juce::String &sourceXml) {
      return xmapLibrary_.loadPersisted(id, sourceXml);
    };
    callbacks.resolveLua = [this](const std::string &name) { return luaCatalog_.resolvePluginPath(name); };
    callbacks.stripCreated = [this](MixerStrip &strip) {
      pluginFingerprints_.erase(strip.id);
      setupStripPluginSlot(strip);
    };
    callbacks.busCreated = [this](GroupBus &bus) { setupGroupBusAudioCallbacks(bus); };
    callbacks.instrumentReady = [this](MixerStrip &strip) {
      pluginFingerprints_[strip.id] = {strip.pluginUid, strip.pluginParameterFingerprint()};
      pluginStateSettleUntilMs_[strip.id] =
          juce::Time::getMillisecondCounter() + kPluginStateSettleMs;
    };
    callbacks.finished = [this] {
      saveAllStripsToDB();
      pushMixerState(false);
      pushGroupBusState();
      pushMasterAudioState();
      scheduleStateRebuild();
    };
    projectRestoreService_ = std::make_unique<ProjectRestoreService>(
        mixer_, *versionStore_, db_.getLibraryRoutingRepository(), std::move(callbacks));
  }
  const auto result = projectRestoreService_->restore(state);
  if (!result.accepted) {
    pushLogMessage("<b>[Restore]</b> Failed: " + juce::String(result.error), true);
    return false;
  }
  undoManager_.clear();
  if (!result.restoredTopology)
    mixer_.syncStripsToInstruments(masterList_);
  pushMixerState(false);
  pushGroupBusState();
  pushMasterAudioState();
  pushChairState();
  return true;
}

bool MainComponent::loadStoredVersion(const versioning::VersionId &versionId,
                                      const versioning::BranchId &branchId,
                                      bool selectBranchWhenDetached) {
  if (!versionStore_)
    return false;

  const auto version = versionStore_->getVersion(versionId);
  const auto state =
      version ? versionStore_->getState(version->stateHash) : std::nullopt;
  const auto branch = versionStore_->getStorage().getBranch(branchId);
  if (!version || !state || !branch)
    return false;

  if (!applyVersionState(*state))
    return false;

  const bool versionIsHead = branch->second == versionId;
  isDetached_ = !versionIsHead;
  currentVersionId_ = versionId;

  const bool selectBranch = versionIsHead || selectBranchWhenDetached;
  if (selectBranch) {
    currentBranchId_ = branchId;
    stateManager_.setCurrentBranchId(branchId);
    stateManager_.setConfigName(juce::String(branch->first));
  }

  const bool needsRoutingUpgrade =
      versionIsHead && state->routingState.schemaVersion == 0;
  if (needsRoutingUpgrade) {
    stateManager_.markDirty();
    broadcastMessage("setDirtyState", true);
  } else {
    stateManager_.clearDirty();
    undoManager_.markSavePoint();
    broadcastMessage("setDirtyState", false);
  }
  scheduleStateRebuild(); // Deliberately a no-op for a detached version.

  pushBranches();
  pushDagHistory();
  if (selectBranch)
    broadcastMessage("setCurrentBranch", juce::String(branchId));
  pushCurrentVersion();
  if (selectBranch)
    pushConfigStatus();
  return true;
}

bool MainComponent::checkoutBranchById(const versioning::BranchId &branchId) {
  if (!versionStore_)
    return false;
  const auto branch = versionStore_->getStorage().getBranch(branchId);
  return branch && loadStoredVersion(branch->second, branchId, true);
}

void MainComponent::restoreDoricoProject(
    const std::string &legacyBranchName,
    const versioning::BranchId &savedBranchId,
    const versioning::VersionId &savedVersionId) {
  if (!versionStore_ || !projectRestoreReady_) {
    pendingDoricoProject_ =
        DoricoProjectRequest{legacyBranchName, savedBranchId, savedVersionId};
    return;
  }

  const auto target = versionStore_->resolveProjectRestoreTarget(
      savedBranchId, savedVersionId, legacyBranchName);
  if (!target) {
    pushLogMessage("<span style=\"color: #fbbf24\"><b>[Project]</b> "
                   "Saved Fiddle branch/version was not found; keeping the "
                   "current branch.</span>");
    return;
  }

  if (target->branchId == currentBranchId_ &&
      target->versionId == currentVersionId_) {
    // The native plug-in announces its saved identity again after every TCP
    // reconnect. Do not reload the mixer and discard unsaved edits when it is
    // already attached to that same project version.
    pushConfigStatus();
    return;
  }

  if (!loadStoredVersion(target->versionId, target->branchId, true)) {
    pushLogMessage("<span style=\"color: red\"><b>[Project]</b> Failed to "
                   "restore the saved Fiddle version.</span>",
                   true);
    return;
  }

  juce::String resolution = "exact saved version";
  if (target->match == versioning::ProjectRestoreTarget::Match::BranchId)
    resolution = "saved branch head (saved version unavailable)";
  else if (target->match ==
           versioning::ProjectRestoreTarget::Match::LegacyBranchName)
    resolution = "legacy branch name";
  const auto branch = versionStore_->getStorage().getBranch(target->branchId);
  const auto branchName =
      branch ? juce::String(branch->first) : juce::String(legacyBranchName);
  pushLogMessage("<b>[Project]</b> Restored " + resolution + ": " + branchName);
}

MainComponent::~MainComponent() {
  stopTimer();
  agentControlServer_.reset();
  deviceManager.removeAudioCallback(this);
  renderAhead_.reset();
  mixer_.masterAudio().setOnChanged(nullptr);
  std::cerr << "[MainComponent] Destructor Invoked. Saving to SQLite..."
            << std::endl;
  try {
    saveAllStripsToDB();
    std::cerr << "[MainComponent] State saved successfully to SQLite"
              << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[MainComponent] Exception during save: " << e.what()
              << std::endl;
  }

  // Persist history window geometry + visibility on quit.
  if (historyWindow_) {
    fiddle::WindowSettings ws;
    ws.windowId = "history";
    auto bounds = historyWindow_->getBounds();
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = historyWindow_->isVisible();
    db_.saveWindowSettings(ws);
  }

  // Persist library manager window geometry on quit.
  if (libraryManagerWindow_) {
    fiddle::WindowSettings ws;
    ws.windowId = "library";
    auto bounds = libraryManagerWindow_->getBounds();
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = libraryManagerWindow_->isVisible();
    db_.saveWindowSettings(ws);
  }

  audioSettings_.reset();
  server.reset();
}

void MainComponent::scanPersistedExpressionMapSources() {
  const auto saved = juce::JSON::parse(
      juce::String(db_.loadSetting("expression_map_source_dirs", "[]")));
  if (!saved.isArray())
    return;
  for (const auto &item : *saved.getArray()) {
    const juce::File directory(item.toString());
    if (directory.isDirectory())
      xmapLibrary_.scanDirectory(directory);
  }
}

void MainComponent::rememberExpressionMapSourceDirectory(
    const juce::File &directory) {
  if (!directory.isDirectory())
    return;
  const auto path = directory.getFullPathName();
  auto saved = juce::JSON::parse(
      juce::String(db_.loadSetting("expression_map_source_dirs", "[]")));
  juce::Array<juce::var> directories;
  if (saved.isArray())
    directories = *saved.getArray();
  for (const auto &item : directories)
    if (item.toString() == path)
      return;
  directories.add(path);
  db_.saveSetting("expression_map_source_dirs",
                  juce::JSON::toString(juce::var(directories), false)
                      .toStdString());
}

void MainComponent::initAgentControl() {
  if (agentControlServer_)
    return;

  juce::Component::SafePointer<MainComponent> safeThis(this);
  agentControlServer_ = std::make_unique<AgentControlServer>(
      FiddleConfig::getAppDataDir().getChildFile("agent-control.json"),
      [safeThis](const juce::String &method, const juce::var &params,
                 AgentControlServer::Completion complete) {
        juce::MessageManager::callAsync(
            [safeThis, method, params,
             complete = std::move(complete)]() mutable {
              if (safeThis == nullptr) {
                complete(AgentControlServer::Response::failure(
                    "Fiddle is shutting down"));
                return;
              }
              complete(safeThis->handleAgentControlRequest(method, params));
            });
      });
  agentControlServer_->startThread();
}

juce::var MainComponent::agentStatus() const {
  auto *status = new juce::DynamicObject();
  status->setProperty("application", "FiddleServer");
  auto *application = juce::JUCEApplicationBase::getInstance();
  status->setProperty("version", application
                                     ? application->getApplicationVersion()
                                     : juce::String("unknown"));
  const bool ready = projectRestoreReady_ &&
                     (!projectRestoreService_ ||
                      !projectRestoreService_->isLoading());
  status->setProperty("ready", ready);
  status->setProperty("dirty", stateManager_.isDirty());
  status->setProperty(
      "transportPlaying",
      isTransportStarted_.load(std::memory_order_relaxed));
  status->setProperty("detached", isDetached_);
  status->setProperty("branchId", juce::String(currentBranchId_));
  status->setProperty("versionId", juce::String(currentVersionId_));
  status->setProperty("layerCount", mixer_.size());

  auto *history = new juce::DynamicObject();
  history->setProperty("canUndo", undoManager_.canUndo());
  history->setProperty("canRedo", undoManager_.canRedo());
  history->setProperty("undoDescription", undoManager_.undoDescription());
  history->setProperty("redoDescription", undoManager_.redoDescription());
  status->setProperty("history", juce::var(history));
  return juce::var(status);
}

juce::var MainComponent::agentLayerSnapshot(const MixerStrip &strip) const {
  auto *layer = new juce::DynamicObject();
  const auto state = strip.realtimeState();
  layer->setProperty("id", strip.id);
  layer->setProperty("name", strip.layerName);
  layer->setProperty("library", strip.library);
  layer->setProperty("family", strip.family);
  layer->setProperty("chairId", strip.chairId);
  layer->setProperty("patchId", strip.patchId);
  layer->setProperty("directOutputBusId", strip.directOutputBusId);
  layer->setProperty("active", state.active);
  layer->setProperty("muted", state.muted);
  layer->setProperty("soloed", state.soloed);
  layer->setProperty("inputPort", state.inputPort);
  layer->setProperty("inputChannel", state.inputChannel);
  layer->setProperty("gainDb", static_cast<double>(state.gainDb));
  layer->setProperty("peakDb", static_cast<double>(state.peakDb));
  layer->setProperty("pluginUid", strip.pluginUid);
  layer->setProperty("hasPlugin", strip.hasPlugin());
  layer->setProperty("pluginStatus",
                     HostedPluginSlot::statusName(strip.pluginStatus()));
  layer->setProperty("pluginError", strip.pluginError());
  layer->setProperty(
      "expressionMap",
      strip.expressionMap ? juce::String(strip.expressionMap->name) : "");

  juce::Array<juce::var> luaProcessors;
  for (const auto &processor : strip.luaPlugins) {
    auto *item = new juce::DynamicObject();
    item->setProperty("name", juce::String(processor->meta().name));
    item->setProperty("loaded", processor->isLoaded());
    luaProcessors.add(juce::var(item));
  }
  layer->setProperty("luaProcessors", juce::var(luaProcessors));
  return juce::var(layer);
}

juce::var MainComponent::agentMixerSnapshot() {
  auto *snapshot = new juce::DynamicObject();
  snapshot->setProperty("status", agentStatus());
  juce::Array<juce::var> layers;
  for (const auto *strip : mixer_.getAllStrips())
    layers.add(agentLayerSnapshot(*strip));
  snapshot->setProperty("layers", juce::var(layers));
  snapshot->setProperty("groupBuses",
                        juce::JSON::parse(mixer_.groupBusesToJson()));
  snapshot->setProperty("master", mixer_.masterAudio().toJson());
  return juce::var(snapshot);
}

juce::var MainComponent::agentLayerResult(const juce::String &stripId,
                                          bool changed) {
  auto *result = new juce::DynamicObject();
  result->setProperty("changed", changed);
  result->setProperty("status", agentStatus());
  if (const auto *strip = mixer_.getStrip(stripId))
    result->setProperty("layer", agentLayerSnapshot(*strip));
  return juce::var(result);
}

void MainComponent::agentMixerChanged() {
  saveAllStripsToDB(false);
  pushMixerState();
  pushGroupBusState();
  pushMasterAudioState();
  scheduleStateRebuild();
}

bool MainComponent::applyAgentUndoRedo(bool redo) {
  const bool changed = redo ? undoManager_.redo() : undoManager_.undo();
  if (!changed)
    return false;
  agentMixerChanged();
  if (undoManager_.isAtSavePoint()) {
    stateManager_.clearDirty();
    broadcastMessage("setDirtyState", false);
    pushConfigStatus();
  }
  return true;
}

juce::var
MainComponent::agentLibrarySetupSnapshot(const juce::String &libraryId) {
  auto *result = new juce::DynamicObject();
  result->setProperty("libraryId", libraryId);
  auto libraries = db_.listLibraries();
  for (const auto &library : libraries) {
    if (library.id != libraryId)
      continue;
    result->setProperty("name", library.name);
    result->setProperty("vendor", library.vendor);
    result->setProperty("variant", library.variant);
    break;
  }

  juce::Array<juce::var> patchValues;
  int guidedCount = 0;
  int configuredCount = 0;
  if (auto *repository = db_.getLibraryRoutingRepository()) {
    for (const auto &patch :
         repository->listPatches(libraryId.toStdString())) {
      auto *item = new juce::DynamicObject();
      item->setProperty("id", juce::String(patch.id));
      item->setProperty("name", juce::String(patch.name));
      item->setProperty("instrumentEntityId",
                        juce::String(patch.instrumentEntityId));
      item->setProperty("family", juce::String(patch.family));
      item->setProperty("character", juce::String(patch.character));
      item->setProperty("pluginUid", patch.pluginUid);
      item->setProperty("expressionMapId",
                        juce::String(patch.expressionMapId));
      item->setProperty("expectedPresetName",
                        juce::String(patch.expectedPresetName));
      item->setProperty("setupComplete", patch.setupComplete);
      item->setProperty("hasPluginState", !patch.pluginState.empty());
      if (!patch.expectedPresetName.empty()) {
        ++guidedCount;
        if (patch.setupComplete)
          ++configuredCount;
      }
      patchValues.add(juce::var(item));
    }
  }
  result->setProperty("patches", juce::var(patchValues));
  result->setProperty("guidedPatchCount", guidedCount);
  result->setProperty("configuredPatchCount", configuredCount);
  result->setProperty("remainingPatchCount", guidedCount - configuredCount);
  result->setProperty("ready", guidedCount > 0 && guidedCount == configuredCount);
  return juce::var(result);
}

AgentControlServer::Response
MainComponent::createAgentGuidedLibrary(const juce::var &params) {
  if (!params.isObject())
    return AgentControlServer::Response::failure(
        "Library parameters must be an object");
  if (isTransportStarted_.load(std::memory_order_relaxed))
    return AgentControlServer::Response::failure(
        "Stop Dorico playback before creating a library");

  const auto name = params["name"].toString().trim();
  const auto pluginValue = params["pluginUid"];
  auto *patchArray = params["patches"].getArray();
  if (name.isEmpty())
    return AgentControlServer::Response::failure("Library name is required");
  if ((!pluginValue.isInt() && !pluginValue.isInt64()) ||
      static_cast<int>(pluginValue) == 0)
    return AgentControlServer::Response::failure(
        "pluginUid must identify an installed instrument plug-in");
  if (!patchArray || patchArray->isEmpty() || patchArray->size() > 256)
    return AgentControlServer::Response::failure(
        "patches must contain between 1 and 256 entries");
  const int pluginUid = static_cast<int>(pluginValue);
  bool pluginAvailable = false;
  for (const auto &description : pluginScanner_.getKnownPluginList().getTypes())
    if (description.uniqueId == pluginUid && description.isInstrument) {
      pluginAvailable = true;
      break;
    }
  if (!pluginAvailable)
    return AgentControlServer::Response::failure(
        "The requested instrument plug-in is not available");

  const auto requestedId = params["libraryId"].toString().trim();
  const auto libraryId =
      requestedId.isNotEmpty() ? requestedId : juce::Uuid().toString();
  for (const auto &library : db_.listLibraries())
    if (library.id == libraryId)
      return AgentControlServer::Response::failure(
          "A library with this ID already exists");

  std::set<std::string> patchIds;
  std::set<juce::String> sourceDirectories;
  std::vector<fiddle::LibraryPatchRow> patches;
  patches.reserve(static_cast<std::size_t>(patchArray->size()));
  int position = 0;
  for (const auto &value : *patchArray) {
    auto *object = value.getDynamicObject();
    if (!object)
      return AgentControlServer::Response::failure(
          "Every patch must be an object");
    fiddle::LibraryPatchRow patch;
    patch.id = object->getProperty("id").toString().trim().toStdString();
    if (patch.id.empty())
      patch.id = juce::Uuid().toString().toStdString();
    if (!patchIds.insert(patch.id).second)
      return AgentControlServer::Response::failure(
          "Patch IDs must be unique");
    patch.libraryId = libraryId.toStdString();
    patch.position = position++;
    patch.name =
        object->getProperty("name").toString().trim().toStdString();
    patch.instrumentEntityId = object->getProperty("instrumentEntityId")
                                   .toString()
                                   .trim()
                                   .toStdString();
    patch.family =
        object->getProperty("family").toString().trim().toStdString();
    patch.character =
        object->getProperty("character").toString().trim().toStdString();
    patch.pluginUid = pluginUid;
    patch.expressionMapId = object->getProperty("expressionMapId")
                                .toString()
                                .trim()
                                .toStdString();
    patch.expectedPresetName = object->getProperty("expectedPresetName")
                                   .toString()
                                   .trim()
                                   .toStdString();
    patch.setupComplete = false;
    const auto mapPath =
        object->getProperty("expressionMapPath").toString().trim();
    if (patch.name.empty() || patch.expressionMapId.empty() ||
        patch.expectedPresetName.empty() || mapPath.isEmpty())
      return AgentControlServer::Response::failure(
          "Each patch needs a name, expressionMapId, expressionMapPath, and expectedPresetName");
    if (!patch.character.empty() && patch.character != "solo" &&
        patch.character != "section" && patch.character != "ensemble" &&
        patch.character != "overlay")
      return AgentControlServer::Response::failure(
          "Patch character must be solo, section, ensemble, overlay, or empty");

    if (!patch.instrumentEntityId.empty()) {
      const bool found = std::any_of(
          instrumentBrowser_.getInstruments().begin(),
          instrumentBrowser_.getInstruments().end(),
          [&patch](const auto &instrument) {
            return instrument.entityID.toStdString() ==
                   patch.instrumentEntityId;
          });
      if (!found)
        return AgentControlServer::Response::failure(
            "Unknown Dorico instrument ID: " +
            juce::String(patch.instrumentEntityId));
    }

    const juce::File mapFile(mapPath);
    if (!mapFile.existsAsFile() ||
        !mapFile.hasFileExtension("doricolib"))
      return AgentControlServer::Response::failure(
          "Expression map file is unavailable: " + mapPath);
    const auto metadata = fiddle::scanExpressionMapMetadata(mapFile);
    const bool hasRequestedMap = std::any_of(
        metadata.begin(), metadata.end(), [&patch](const auto &entry) {
          return entry.entityID == patch.expressionMapId;
        });
    if (!hasRequestedMap)
      return AgentControlServer::Response::failure(
          "Expression map ID was not found in: " + mapPath);
    sourceDirectories.insert(mapFile.getParentDirectory().getFullPathName());
    patches.push_back(std::move(patch));
  }

  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return AgentControlServer::Response::failure(
        "The library catalog is not available");
  fiddle::LibraryCatalogSnapshot target;
  target.id = libraryId.toStdString();
  target.name = name.toStdString();
  target.vendor = params["vendor"].toString().trim().toStdString();
  target.variant = params["variant"].toString().trim().toStdString();
  target.exists = true;
  target.patches = patches;
  auto action =
      std::make_unique<fiddle::LibraryCatalogAction>(*repository, target);
  if (!libraryUndoManager_.perform(std::move(action)))
    return AgentControlServer::Response::failure(
        "Fiddle could not create the guided library");

  for (const auto &path : sourceDirectories) {
    const juce::File directory(path);
    xmapLibrary_.scanDirectory(directory);
    rememberExpressionMapSourceDirectory(directory);
  }
  pushLibraryCatalogHistory();
  pushLayerCatalog();
  return AgentControlServer::Response::success(
      agentLibrarySetupSnapshot(libraryId));
}

AgentControlServer::Response MainComponent::handleAgentControlRequest(
    const juce::String &method, const juce::var &params) {
  if (!projectRestoreReady_ ||
      (projectRestoreService_ && projectRestoreService_->isLoading()))
    return AgentControlServer::Response::failure(
        "Fiddle is still initializing");

  if (method == "status")
    return AgentControlServer::Response::success(agentStatus());
  if (method == "mixer.get")
    return AgentControlServer::Response::success(agentMixerSnapshot());

  if (method == "library.inspectSources") {
    if (!params.isObject())
      return AgentControlServer::Response::failure(
          "Source parameters must be an object");
    const juce::File directory(params["expressionMapDirectory"].toString());
    if (!directory.isDirectory())
      return AgentControlServer::Response::failure(
          "Expression-map directory was not found");
    const int requestedLimit = params["maxMaps"].isInt()
                                   ? static_cast<int>(params["maxMaps"])
                                   : 500;
    const int limit = juce::jlimit(1, 500, requestedLimit);
    auto files = directory.findChildFiles(juce::File::findFiles, true,
                                          "*.doricolib");
    const auto mapQuery = params["mapQuery"].toString().trim();
    if (mapQuery.isEmpty() && files.size() > 2000)
      return AgentControlServer::Response::failure(
          "This directory contains " + juce::String(files.size()) +
          " expression-map files. Supply mapQuery or choose a narrower directory.");
    juce::Array<juce::File> candidateFiles;
    for (const auto &file : files)
      if (mapQuery.isEmpty() ||
          file.getFullPathName().containsIgnoreCase(mapQuery))
        candidateFiles.add(file);
    juce::Array<juce::var> maps;
    int visitedFiles = 0;
    for (const auto &file : candidateFiles) {
      ++visitedFiles;
      for (const auto &metadata : scanExpressionMapMetadata(file)) {
        if (maps.size() >= limit)
          break;
        auto *item = new juce::DynamicObject();
        item->setProperty("name", juce::String(metadata.name));
        item->setProperty("entityId", juce::String(metadata.entityID));
        item->setProperty("version", metadata.version);
        item->setProperty("creator", juce::String(metadata.creator));
        item->setProperty("pluginNames", juce::String(metadata.pluginNames));
        item->setProperty("sourcePath", file.getFullPathName());
        maps.add(juce::var(item));
      }
      if (maps.size() >= limit)
        break;
    }
    const auto query = params["pluginQuery"].toString().trim();
    juce::Array<juce::var> plugins;
    for (const auto &description :
         pluginScanner_.getKnownPluginList().getTypes()) {
      if (!description.isInstrument ||
          (query.isNotEmpty() &&
           !description.name.containsIgnoreCase(query) &&
           !description.manufacturerName.containsIgnoreCase(query)))
        continue;
      auto *item = new juce::DynamicObject();
      item->setProperty("uid", description.uniqueId);
      item->setProperty("name", description.name);
      item->setProperty("manufacturer", description.manufacturerName);
      item->setProperty("format", description.pluginFormatName);
      plugins.add(juce::var(item));
    }
    auto *result = new juce::DynamicObject();
    result->setProperty("directory", directory.getFullPathName());
    result->setProperty("mapQuery", mapQuery);
    result->setProperty("maps", juce::var(maps));
    result->setProperty("mapFileCount", files.size());
    result->setProperty("matchedMapFileCount", candidateFiles.size());
    result->setProperty("truncated",
                        maps.size() >= limit || visitedFiles < candidateFiles.size());
    result->setProperty("plugins", juce::var(plugins));
    return AgentControlServer::Response::success(juce::var(result));
  }

  if (method == "library.setup.create")
    return createAgentGuidedLibrary(params);

  if (method == "library.setup.get") {
    if (!params.isObject() || params["libraryId"].toString().trim().isEmpty())
      return AgentControlServer::Response::failure("libraryId is required");
    const auto libraryId = params["libraryId"].toString().trim();
    const auto libraries = db_.listLibraries();
    const bool exists = std::any_of(
        libraries.begin(), libraries.end(),
        [&libraryId](const auto &library) { return library.id == libraryId; });
    if (!exists)
      return AgentControlServer::Response::failure("Library not found");
    return AgentControlServer::Response::success(
        agentLibrarySetupSnapshot(libraryId));
  }

  if (method == "library.setup.open") {
    if (!params.isObject() || params["libraryId"].toString().trim().isEmpty())
      return AgentControlServer::Response::failure("libraryId is required");
    const auto libraryId = params["libraryId"].toString().trim();
    bool exists = false;
    for (const auto &library : db_.listLibraries())
      if (library.id == libraryId) {
        exists = true;
        break;
      }
    if (!exists)
      return AgentControlServer::Response::failure("Library not found");
    pendingGuidedLibraryId_ = libraryId;
    showLibraryManagerWindow();
    if (libraryManagerWindowLoaded_) {
      broadcastMessage("openGuidedLibrary", libraryId);
      pendingGuidedLibraryId_.clear();
    }
    auto *result = new juce::DynamicObject();
    result->setProperty("opened", true);
    result->setProperty("libraryId", libraryId);
    return AgentControlServer::Response::success(juce::var(result));
  }

  if (method == "history.undo" || method == "history.redo") {
    const bool changed = applyAgentUndoRedo(method == "history.redo");
    auto *result = new juce::DynamicObject();
    result->setProperty("changed", changed);
    result->setProperty("mixer", agentMixerSnapshot());
    return AgentControlServer::Response::success(juce::var(result));
  }

  if (method != "layer.setGain" && method != "layer.setMute" &&
      method != "layer.setSolo")
    return AgentControlServer::Response::failure("Unknown control method: " +
                                                 method);
  if (!params.isObject())
    return AgentControlServer::Response::failure(
        "Control parameters must be an object");

  const auto stripId = params["stripId"].toString();
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return AgentControlServer::Response::failure("Layer not found: " +
                                                 stripId);

  bool changed = false;
  if (method == "layer.setGain") {
    const auto value = params["gainDb"];
    if (!value.isDouble() && !value.isInt() && !value.isInt64())
      return AgentControlServer::Response::failure("gainDb must be a number");
    const auto gainDb = static_cast<float>(static_cast<double>(value));
    if (!std::isfinite(gainDb) || gainDb < -120.0f || gainDb > 6.0f)
      return AgentControlServer::Response::failure(
          "gainDb must be between -120 and 6");
    if (strip->gainDb() != gainDb)
      changed = mixerCommandService_->setGain(stripId, gainDb);
  } else {
    const auto property = method == "layer.setMute" ? "muted" : "soloed";
    const auto value = params[property];
    if (!value.isBool())
      return AgentControlServer::Response::failure(juce::String(property) +
                                                   " must be a boolean");
    const bool target = static_cast<bool>(value);
    const bool current = method == "layer.setMute" ? strip->isMuted()
                                                    : strip->isSoloed();
    if (current != target) {
      changed = method == "layer.setMute"
                    ? mixerCommandService_->setMute(stripId, target)
                    : mixerCommandService_->setSolo(stripId, target);
    }
  }

  if (changed)
    agentMixerChanged();
  return AgentControlServer::Response::success(
      agentLayerResult(stripId, changed));
}

void MainComponent::pushUndoState() {
  auto *state = new juce::DynamicObject();
  state->setProperty("canUndo", undoManager_.canUndo());
  state->setProperty("canRedo", undoManager_.canRedo());
  state->setProperty("undoDescription", undoManager_.undoDescription());
  state->setProperty("redoDescription", undoManager_.redoDescription());
  broadcastMessage("setUndoState", juce::var(state));
  pushLibraryLayerStatus();
}

void MainComponent::pushLibraryLayerStatus() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository) return;
  const auto outdated = repository->outOfDateLayerIds();
  std::map<std::string, std::pair<int, int>> counts;
  for (const auto &row : repository->listLayers()) {
    auto &count = counts[row.patchId];
    ++count.first;
    if (std::find(outdated.begin(), outdated.end(), row.id) != outdated.end()) ++count.second;
  }
  juce::Array<juce::var> status;
  for (const auto &[id, count] : counts) {
    auto *item = new juce::DynamicObject();
    item->setProperty("patchId", juce::String(id));
    item->setProperty("usageCount", count.first);
    item->setProperty("outOfDateLayerCount", count.second);
    status.add(juce::var(item));
  }
  broadcastMessage("setLibraryLayerStatus", status);
}

void MainComponent::pushProjectSettings() {
  mixer_.initialiseLegacyChairLevels();
  const auto settings = mixer_.projectSettings();
  auto *state = new juce::DynamicObject();
  state->setProperty("playbackDelayMs", settings.playbackDelayMs);
  juce::Array<juce::var> locks;
  for (const auto &id : settings.lockedChairIds) locks.add(juce::String(id));
  state->setProperty("lockedChairIds", locks);
  auto *levels = new juce::DynamicObject();
  for (const auto &[id, level] : settings.chairLevels) {
    auto *item = new juce::DynamicObject();
    item->setProperty("targetDb", level.targetDb);
    auto *weights = new juce::DynamicObject();
    for (const auto &[strip, weight] : level.weights) weights->setProperty(juce::Identifier(strip), weight);
    item->setProperty("weights", juce::var(weights));
    levels->setProperty(juce::Identifier(id), juce::var(item));
  }
  state->setProperty("chairLevels", juce::var(levels));
  broadcastMessage("setProjectSettings", juce::var(state));
}

void MainComponent::pushMixerState(bool markDirty) {
  if (!webViewBridge_.isLoaded())
    return;
  pushUndoState();
  pushProjectSettings();
  if (markDirty) {
    bool wasDirty = stateManager_.isDirty();
    stateManager_.markDirty();
    broadcastMessage("setDirtyState", true);
    // Notify plugin on first dirty transition (not on every fader drag)
    if (!wasDirty)
      pushConfigStatus();
  }
  auto state = juce::JSON::fromString(mixer_.toJson());
  std::set<std::string> outOfDateLayerIds;
  if (auto *repository = db_.getLibraryRoutingRepository()) {
    const auto ids = repository->outOfDateLayerIds();
    outOfDateLayerIds.insert(ids.begin(), ids.end());
  }
  if (auto *strips = state.getArray()) {
    for (auto &strip : *strips) {
      if (auto *object = strip.getDynamicObject()) {
        const auto id = object->getProperty("id").toString().toStdString();
        object->setProperty("sourcePatchOutOfDate",
                            outOfDateLayerIds.count(id) != 0);
      }
    }
  }
  broadcastMessage("setMixerState", state);
}

void MainComponent::pushMasterAudioState() {
  if (webViewBridge_.isLoaded())
    broadcastMessage("setMasterAudioState", mixer_.masterAudio().toJson());
}

void MainComponent::pushMixPrintState(const juce::String &errorOverride) {
  juce::var state;
  if (renderAhead_) {
    state = renderAhead_->mixPrintState();
  } else {
    auto *empty = new juce::DynamicObject();
    empty->setProperty("armed", false);
    empty->setProperty("recording", false);
    empty->setProperty("finalizing", false);
    empty->setProperty("filePath", "");
    empty->setProperty("fileName", "");
    empty->setProperty("sampleRate", 0.0);
    empty->setProperty("samples", static_cast<juce::int64>(0));
    empty->setProperty("durationSeconds", 0.0);
    empty->setProperty("droppedBlocks", static_cast<juce::int64>(0));
    empty->setProperty("droppedSamples", static_cast<juce::int64>(0));
    empty->setProperty("error", "Audio rendering is not running");
    empty->setProperty("format", "Stereo WAV · 32-bit float");
    state = juce::var(empty);
  }
  if (errorOverride.isNotEmpty())
    if (auto *object = state.getDynamicObject())
      object->setProperty("error", errorOverride);
  broadcastMessage("setMixPrintState", state);
}

void MainComponent::pushMixerMeters() {
  if (!webViewBridge_.isLoaded() || meterUpdatePending_)
    return;
  // No database reads, plug-in program queries, or full-state round trip.
  const auto json = juce::JSON::toString(mixer_.meterLevels(), true);
  meterUpdatePending_ = true;
  juce::Component::SafePointer<MainComponent> safeThis(this);
  webViewBridge_.getMainWebComponent().evaluateJavascript(
      "window.__dispatchFromCpp && window.__dispatchFromCpp({type:'setMixerMeters',data:" + json + "})",
      [safeThis](juce::WebBrowserComponent::EvaluationResult) {
        if (safeThis != nullptr) safeThis->meterUpdatePending_ = false;
      });
}

void MainComponent::pushGroupBusState() {
  if (webViewBridge_.isLoaded())
    broadcastMessage("setGroupBusState",
                     juce::JSON::fromString(mixer_.groupBusesToJson()));
}

void MainComponent::masterAudioChanged() {
  const bool wasDirty = stateManager_.isDirty();
  stateManager_.markDirty();
  if (!wasDirty)
    pushConfigStatus();
  broadcastMessage("setDirtyState", true);
  saveMasterAudioToDB();
  pushMasterAudioState();
  scheduleStateRebuild();
}

void MainComponent::stripAudioChanged(const juce::String &stripId) {
  auto *strip = mixer_.getStrip(stripId);
  if (!strip)
    return;
  db_.saveStripAudio(stripId, strip->audioEngine().snapshotAll());
  auto state = strip->audioEngine().toJson();
  if (auto *object = state.getDynamicObject()) {
    object->setProperty("stripId", stripId);
    object->setProperty("stripName",
                        strip->layerName.isNotEmpty() ? strip->layerName
                                                     : strip->library);
  }
  broadcastMessage("setStripAudioState", state);
  pushMixerState(true);
  scheduleStateRebuild();
}

void MainComponent::groupBusAudioChanged(const juce::String &busId) {
  if (!mixer_.getGroupBus(busId))
    return;
  mixer_.refreshAudioRouting();
  pushGroupBusState();
  pushMixerState(true);
  scheduleStateRebuild();
}

void MainComponent::pushLogMessage(const juce::String &msg, bool isError) {
  std::lock_guard<std::mutex> lock(logMutex);
  if (!webViewBridge_.isLoaded()) {
    if (logQueue.size() < 1000) {
      logQueue.push_back({msg, isError});
    }
    return;
  }

  safeCallAsync([this, msg, isError]() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("msg", msg);
    obj->setProperty("isError", isError);
    broadcastMessage("addLogMessage", juce::var(obj.get()));
  });
}

void MainComponent::pushEventToWebView(const fiddle::MidiEvent &event) {
  std::string text;
  google::protobuf::TextFormat::PrintToString(event, &text);
  pushLogMessage(juce::String::fromUTF8(text.c_str())
                     .replace("\n", "<br/>")
                     .replace(" ", "&nbsp;"));
}

void MainComponent::pushSubnoteToWebView(const fiddle::Subnote &subnote) {
  // Subnote logging removed — too noisy for the event log.
  juce::ignoreUnused(subnote);
}

void MainComponent::pushToDebugWindow(const juce::String &js) {
  if (debugWindow_ && debugWindow_->isDebugReady()) {
    debugWindow_->evaluateJavascript(js);
  }
}

void MainComponent::toggleDebugWindow() {
  if (debugWindow_) {
    bool wasVisible = debugWindow_->isVisible();
    bool nowVisible = !wasVisible;
    debugWindow_->setVisible(nowVisible);
    if (nowVisible)
      debugWindow_->toFront(true);

    // Save explicitly with the new visibility state — don't re-read
    // from the window, as isVisible() may return stale state.
    fiddle::WindowSettings ws;
    ws.windowId = "debug";
    auto bounds = debugWindow_->getBounds();
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = nowVisible;
    db_.saveWindowSettings(ws);
  }
}

bool MainComponent::isDebugWindowVisible() const {
  return debugWindow_ && debugWindow_->isVisible();
}

void MainComponent::toggleHistoryWindow() {
  // Lazily create the History window on first toggle (same logic as the JS
  // "openHistoryWindow" handler).
  if (!historyWindow_ && versionStore_) {
    historyWindow_ =
        std::make_unique<HistoryWindow>(webViewBridge_.createWebOptions());
    historyWindowLoaded_ = false;
    juce::String root = juce::WebBrowserComponent::getResourceProviderRoot();
    historyWindow_->getWebView().goToURL(root + "index.html?view=history");

    // Restore saved geometry (if any).
    auto hws = db_.loadWindowSettings("history");
    if (hws.width > 0 && hws.height > 0) {
      historyWindow_->restoreGeometry(hws.x, hws.y, hws.width, hws.height,
                                      false /* we toggle below */);
    }
  }

  if (historyWindow_) {
    bool wasVisible = historyWindow_->isVisible();
    bool nowVisible = !wasVisible;
    historyWindow_->setVisible(nowVisible);
    if (nowVisible)
      historyWindow_->toFront(true);

    // Persist geometry + visibility.
    fiddle::WindowSettings ws;
    ws.windowId = "history";
    auto bounds = historyWindow_->getBounds();
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = nowVisible;
    db_.saveWindowSettings(ws);
  }
}

bool MainComponent::isHistoryWindowVisible() const {
  return historyWindow_ && historyWindow_->isVisible();
}

void MainComponent::showLibraryManagerWindow() {
  // Lazily create the Library Manager window on first request.
  if (!libraryManagerWindow_) {
    libraryManagerWindow_ = std::make_unique<LibraryManagerWindow>(
        webViewBridge_.createWebOptions());
    libraryManagerWindowLoaded_ = false;
    juce::String root = juce::WebBrowserComponent::getResourceProviderRoot();
    libraryManagerWindow_->getWebView().goToURL(root +
                                                "index.html?view=library");
    libraryManagerWindow_->setGeometrySaver(
        [this]() { saveLibraryManagerWindowGeometry(); });

    // Restore saved geometry (if any).
    auto lws = db_.loadWindowSettings("library");
    if (lws.width > 0 && lws.height > 0) {
      libraryManagerWindow_->restoreGeometry(
          lws.x, lws.y, lws.width, lws.height, false /* shown below */);
    }
  }

  if (libraryManagerWindow_) {
    libraryManagerWindow_->setMinimised(false);
    libraryManagerWindow_->setVisible(true);
    libraryManagerWindow_->toFront(true);
    libraryManagerWindow_->grabKeyboardFocus();

    auto bounds = libraryManagerWindow_->getBounds();
    fiddle::WindowSettings ws;
    ws.windowId = "library";
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = true;
    db_.saveWindowSettings(ws);
  }
}

void MainComponent::toggleLibraryManagerWindow() {
  if (!libraryManagerWindow_ || !libraryManagerWindow_->isVisible()) {
    showLibraryManagerWindow();
    return;
  }

  libraryManagerWindow_->setVisible(false);
  auto bounds = libraryManagerWindow_->getBounds();
  fiddle::WindowSettings ws;
  ws.windowId = "library";
  ws.x = bounds.getX();
  ws.y = bounds.getY();
  ws.width = bounds.getWidth();
  ws.height = bounds.getHeight();
  ws.visible = false;
  db_.saveWindowSettings(ws);
}

bool MainComponent::isLibraryManagerWindowVisible() const {
  return libraryManagerWindow_ && libraryManagerWindow_->isVisible();
}

void MainComponent::saveMainWindowGeometry(int x, int y, int w, int h) {
  fiddle::WindowSettings ws;
  ws.windowId = "main";
  ws.x = x;
  ws.y = y;
  ws.width = w;
  ws.height = h;
  ws.visible = true;
  db_.saveWindowSettings(ws);
}

void MainComponent::saveDebugWindowGeometry() {
  if (!debugWindow_)
    return;
  fiddle::WindowSettings ws;
  ws.windowId = "debug";
  auto bounds = debugWindow_->getBounds();
  ws.x = bounds.getX();
  ws.y = bounds.getY();
  ws.width = bounds.getWidth();
  ws.height = bounds.getHeight();
  ws.visible = debugWindow_->isVisible();
  db_.saveWindowSettings(ws);
}

void MainComponent::saveLibraryManagerWindowGeometry() {
  if (!libraryManagerWindow_)
    return;
  // Only persist position/size from move/resize/close callbacks.
  // Visibility is managed explicitly by toggleLibraryManagerWindow() and the
  // destructor — NOT from closeButtonPressed, which calls setVisible(false)
  // before this callback fires, causing isVisible() to return stale (false).
  auto existing = db_.loadWindowSettings("library");
  fiddle::WindowSettings ws;
  ws.windowId = "library";
  auto bounds = libraryManagerWindow_->getBounds();
  ws.x = bounds.getX();
  ws.y = bounds.getY();
  ws.width = bounds.getWidth();
  ws.height = bounds.getHeight();
  ws.visible =
      existing.found ? existing.visible : libraryManagerWindow_->isVisible();
  db_.saveWindowSettings(ws);
}

juce::Rectangle<int> MainComponent::restoreMainWindowGeometry() {
  auto ws = db_.loadWindowSettings("main");
  auto bounds = juce::Rectangle<int>(ws.x, ws.y, ws.width, ws.height);

  // Validate against display bounds
  auto displays = juce::Desktop::getInstance().getDisplays();
  auto totalBounds = displays.getTotalBounds(true);
  if (!totalBounds.intersects(bounds)) {
    // Off screen — use defaults
    bounds = juce::Rectangle<int>(0, 0, 800, 600);
  }
  // Clamp minimum size
  if (bounds.getWidth() < 400)
    bounds.setWidth(400);
  if (bounds.getHeight() < 300)
    bounds.setHeight(300);

  return bounds;
}

void MainComponent::timerCallback() {
  if (renderAhead_ &&
      renderAhead_->mixPrintNeedsFinalizing(audioStreamTimeMs())) {
    renderAhead_->stopMixPrint();
    pushMixPrintState();
  }

  const auto diagnosticsNow = juce::Time::getMillisecondCounterHiRes();
  if (diagnosticsNow - lastDiagnosticsPushMs_ >= 250.0) {
    lastDiagnosticsPushMs_ = diagnosticsNow;
    pushAudioDiagnostics();
    if (effectivePlaybackDelayMs() != reportedPlaybackDelayMs_.load(std::memory_order_relaxed))
      pushConfigStatus();
  }
  subnoteGenerator.tick(noteTracker.getSessionSamples());

  // Library patch previews are not project mixer strips. Their editor changes
  // enable the Library Manager's Save button without dirtying the branch.
  for (const auto &patchId :
       libraryPatchPreviewHost_.consumeChangedPatchIds())
    broadcastMessage("libraryPatchPreviewChanged", juce::String(patchId));

  // Only peak levels at meter frequency. Structural state is pushed by its
  // change handlers. Do not enqueue full-state refreshes on this timer.
  static int meterCounter = 0;
  if (++meterCounter % 3 == 0) {
    pushMixerMeters();
  }

  static int hbCounter = 0;
  if (++hbCounter % 50 == 0) { // Every 1 second (20ms * 50)
    safeCallAsync([this, val = hbCounter / 50]() {
      broadcastMessage("setHeartbeat", val);
    });
  }

  // Poll hosted VST plug-in parameters every ~2s (100 × 20ms).
  // Many VST3 plug-ins (e.g. Vienna Synchron Player) don't consistently fire
  // AudioProcessorListener callbacks. Compare their stable parameter surface,
  // not opaque state blobs that may contain changing playback data.
  const auto now = juce::Time::getMillisecondCounter();
  const bool suppressPlaybackChanges = shouldSuppressPluginChanges(now);

  processPluginChangeNotifications(suppressPlaybackChanges);
  bool externalFxEdit = mixer_.masterAudio().consumePluginChanges(suppressPlaybackChanges);
  if (mixer_.masterAudio().consumeLatencyDisplayChange())
    pushMasterAudioState();
  for (auto *strip : mixer_.getAllStrips()) {
    externalFxEdit |= strip->audioEngine().consumePluginChanges(suppressPlaybackChanges);
    if (strip->audioEngine().consumeLatencyDisplayChange()) {
      auto state = strip->audioEngine().toJson();
      state.getDynamicObject()->setProperty("stripId", strip->id);
      broadcastMessage("setStripAudioState", state);
    }
  }
  bool busLatencyChanged = false;
  for (auto *bus : mixer_.getAllGroupBuses()) {
    externalFxEdit |= bus->audioEngine().consumePluginChanges(suppressPlaybackChanges);
    busLatencyChanged |= bus->audioEngine().consumeLatencyDisplayChange();
  }
  if (busLatencyChanged)
    pushGroupBusState();
  if (++pluginPollCounter_ % 100 == 0) {
    if (!suppressPlaybackChanges) {
      pollPluginStateChanges();
      externalFxEdit |= mixer_.masterAudio().refreshPluginStateCaches();
      for (auto *strip : mixer_.getAllStrips())
        externalFxEdit |= strip->audioEngine().refreshPluginStateCaches();
      for (auto *bus : mixer_.getAllGroupBuses())
        externalFxEdit |= bus->audioEngine().refreshPluginStateCaches();
    }
  }
  if (externalFxEdit)
    undoManager_.noteExternalChange();

  // Flush any deferred state rebuild (throttled to 1/sec)
  if (stateRebuildPending_ &&
      !(projectRestoreService_ && projectRestoreService_->isLoading())) {
    auto now = juce::Time::getMillisecondCounter();
    if (now - lastStateRebuildMs_ >= 1000) {
      lastStateRebuildMs_ = now;
      stateRebuildPending_ = false;
      stateManager_.scheduleRebuild([this]() -> juce::MemoryBlock {
        if (isDetached_ || (projectRestoreService_ && projectRestoreService_->isLoading()))
          return {};
        return stateManager_.buildStateBlob(mixer_);
      });
    }
  }
}

void MainComponent::paint(juce::Graphics &g) {
  if (!initComplete_) {
    // Dark splash screen matching the web UI color scheme
    g.fillAll(juce::Colour(0xff020617));

    // "Initializing" title
    g.setFont(juce::Font(juce::FontOptions(28.0f)));
    g.setColour(juce::Colour(0xff94a3b8));
    auto bounds = getLocalBounds();
    g.drawText("Initializing", bounds.removeFromTop(80),
               juce::Justification::centredBottom, false);

    // Bullet-point status messages
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.setColour(juce::Colour(0xff64748b));
    auto msgArea = getLocalBounds().reduced(40, 0).withTrimmedTop(100);
    for (int i = 0; i < initMessages_.size(); ++i) {
      auto line = msgArea.removeFromTop(22);
      g.drawText(juce::String(juce::CharPointer_UTF8("\xe2\x80\xa2")) + "  " +
                     initMessages_[i],
                 line, juce::Justification::centredLeft, true);
    }
  } else {
    g.fillAll(
        getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
  }
}

void MainComponent::resized() {
  if (initComplete_)
    webViewBridge_.getMainWebComponent().setBounds(getLocalBounds());
}

void MainComponent::audioDeviceAboutToStart(juce::AudioIODevice *device) {
  // Pass the actual device sample rate and block size down to the mixer and
  // plugins
  if (device) {
    const auto error = renderAhead_->start(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
    audioDeviceRunning_.store(error.isEmpty(), std::memory_order_relaxed);
    safeCallAsync([this, error] {
      renderAheadError_ = error;
      pushConfigStatus(); // tells the native consumer to map this new generation
    });
  }
}

void MainComponent::audioDeviceStopped() {
  if (renderAhead_) renderAhead_->stop();
  audioDeviceRunning_.store(false, std::memory_order_relaxed);
}

void MainComponent::audioDeviceIOCallbackWithContext(
    const float *const *inputChannelData, int numInputChannels,
    float *const *outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext &context) {

  // The device supplies configuration, not the render clock. Never run DSP or
  // wait for the render worker here. Dorico consumption drives audio production.
  for (int i = 0; i < numOutputChannels; ++i) {
    if (outputChannelData[i] != nullptr) {
      juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
    }
  }

}

int MainComponent::effectivePlaybackDelayMs() const {
  const auto reserve = renderAhead_ ? renderAhead_->reserveMs() : 0.0;
  return juce::jmax(mixer_.getPlaybackDelayMs(),
                   int(std::ceil(reserve + mixer_.maximumPathLatencyMs())));
}

double MainComponent::getDelayedTriggerTimeMs() {
  if (!isTransportStarted_.load(std::memory_order_relaxed)) return 0.0;
  // The renderer now uses future *presentation* time, so do NOT subtract the
  // reserve again. It naturally consumes MIDI early while rendering ahead.
  return audioStreamTimeMs() + reportedPlaybackDelayMs_.load(std::memory_order_relaxed)
         - mixer_.masterLatencyMs();
}

double MainComponent::delayedOutputPresentationTimeMs() const {
  // MIDI is advanced independently for strip, bus, and Master latency. Their
  // final post-Master output therefore reaches this common presentation time.
  return audioStreamTimeMs() +
         reportedPlaybackDelayMs_.load(std::memory_order_relaxed);
}

void MainComponent::showAudioSettings() {
  if (audioSettings_) audioSettings_->show();
}

void MainComponent::pushAudioDiagnostics() {
  audioDiagnostics_.takeLatest(latestAudioDiagnostics_);
  if (!webViewBridge_.isLoaded() || diagnosticsUpdatePending_) return;
  const auto &s = latestAudioDiagnostics_;
  const auto now = juce::Time::getMillisecondCounterHiRes();
  auto *data = new juce::DynamicObject();
  data->setProperty("running", audioDeviceRunning_.load(std::memory_order_relaxed));
#if JUCE_DEBUG
  data->setProperty("buildConfiguration", "Debug");
#else
  data->setProperty("buildConfiguration", "Release");
#endif
  if (audioSettings_) data->setProperty("device", audioSettings_->diagnostics());
  if (renderAhead_) data->setProperty("renderAhead", renderAhead_->diagnostics());
  data->setProperty("renderAheadError", renderAheadError_);
  data->setProperty("effectiveDelayMs", reportedPlaybackDelayMs_.load(std::memory_order_relaxed));
  data->setProperty("requestedDelayMs", mixer_.getPlaybackDelayMs());
  data->setProperty("returnProtocol", int(returnDiagnostics_.audio_protocol()));
  data->setProperty("plugins", mixer_.pluginTimings(now));
  data->setProperty("pluginLoad", s.pluginLoad * 100.0);
  data->setProperty("parallelRendering", s.parallelRendering);
  data->setProperty("otherLoad", s.parallelRendering ? juce::var() : juce::var(s.otherLoad * 100.0));
  data->setProperty("renderWorkerChangeAllowed", !isTransportStarted_.load(std::memory_order_acquire)
                    && !(renderAhead_ && renderAhead_->mixPrintIsArmed()));
  data->setProperty("recentMaxGapMs", s.recentMaxGapMs);
  data->setProperty("lastLongGapMs", s.lastLongGapMs);
  data->setProperty("renderBeforeLongGapMs", s.renderBeforeLongGapMs);
  data->setProperty("ageMs", s.timestampMs > 0 ? now - s.timestampMs : -1.0);
  data->setProperty("sampleRate", s.sampleRate);
  data->setProperty("load", s.load * 100.0);
  data->setProperty("peakLoad", s.peakLoad * 100.0);
  data->setProperty("maxLoad", s.maxLoad * 100.0);
  data->setProperty("maxRenderMs", s.maxRenderMs);
  data->setProperty("maxGapMs", s.maxGapMs);
  data->setProperty("lastOverrunAgeMs", s.lastOverrunMs > 0 ? now - s.lastOverrunMs : -1.0);
  data->setProperty("callbacks", static_cast<juce::int64>(s.callbacks));
  data->setProperty("overruns", static_cast<juce::int64>(s.overruns));
  data->setProperty("longGaps", static_cast<juce::int64>(s.longGaps));
  data->setProperty("ringOverflows", static_cast<juce::int64>(s.ringOverflows));
  data->setProperty("unavailableBlocks", static_cast<juce::int64>(s.unavailableBlocks));
  data->setProperty("droppedReports", static_cast<juce::int64>(s.droppedReports));
  data->setProperty("blockSize", s.blockSize);
  data->setProperty("minBlockSize", s.minBlockSize);
  data->setProperty("maxBlockSize", s.maxBlockSize);
  data->setProperty("deviceXruns", deviceManager.getXRunCount());
  data->setProperty("returnFresh", returnDiagnosticsReceivedMs_ > 0 && now - returnDiagnosticsReceivedMs_ < 3000);
  data->setProperty("returnSampleRate", returnDiagnostics_.sample_rate());
  data->setProperty("returnCallbacks", static_cast<juce::int64>(returnDiagnostics_.callbacks()));
  data->setProperty("underruns", static_cast<juce::int64>(returnDiagnostics_.underruns()));
  data->setProperty("bufferingFrames", static_cast<juce::int64>(returnDiagnostics_.buffering_frames()));
  data->setProperty("unavailableFrames", static_cast<juce::int64>(returnDiagnostics_.unavailable_frames()));
  data->setProperty("safetyMuteEpisodes", static_cast<juce::int64>(returnDiagnostics_.safety_mute_episodes()));
  data->setProperty("safetyMutedFrames", static_cast<juce::int64>(returnDiagnostics_.safety_muted_frames()));
  data->setProperty("minimumQueuedFrames", static_cast<juce::int64>(returnDiagnostics_.minimum_queued_frames()));
  data->setProperty("safetyMuteVersion", int(returnDiagnostics_.safety_mute_version()));
  data->setProperty("droppedMidiEvents", static_cast<juce::int64>(returnDiagnostics_.dropped_midi_events()));
  // Main mixer only: no full-state rebuild, persistence, database query or broadcast.
  const auto json = juce::JSON::toString(juce::var(data), true);
  diagnosticsUpdatePending_ = true;
  juce::Component::SafePointer<MainComponent> safeThis(this);
  webViewBridge_.getMainWebComponent().evaluateJavascript(
      "window.__dispatchFromCpp && window.__dispatchFromCpp({type:'setAudioDiagnostics',data:" + json + "})",
      [safeThis](juce::WebBrowserComponent::EvaluationResult) {
        if (safeThis != nullptr) safeThis->diagnosticsUpdatePending_ = false;
      });
}

void MainComponent::pushBranches() {
  if (!versionStore_)
    return;
  auto branches = versionStore_->getStorage().listBranches();

  juce::Array<juce::var> arr;
  for (const auto &b : branches) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(std::get<0>(b)));
    obj->setProperty("name", juce::String(std::get<1>(b)));
    obj->setProperty("headHash", juce::String(std::get<2>(b)));
    arr.add(juce::var(obj));
  }

  broadcastMessage("setBranches", juce::var(arr));
}

void MainComponent::pushDagHistory() {
  if (!versionStore_)
    return;
  auto versions = versionStore_->listAllVersions();

  juce::Array<juce::var> arr;
  for (const auto &vPair : versions) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("hash", juce::String(vPair.first));

    const auto &ver = vPair.second;
    obj->setProperty("stateHash", juce::String(ver.stateHash));
    obj->setProperty("branchId", juce::String(ver.branchId));
    if (!ver.parentId.empty()) {
      obj->setProperty("parentHash", juce::String(ver.parentId));
    }
    if (!ver.mergeParentId.empty()) {
      obj->setProperty("mergeParentHash", juce::String(ver.mergeParentId));
    }
    if (!ver.createdAt.empty()) {
      obj->setProperty("createdAt", juce::String(ver.createdAt));
    }

    arr.add(juce::var(obj));
  }

  broadcastMessage("setDagHistory", juce::var(arr));
}

void MainComponent::pushCurrentVersion() {
  broadcastMessage("setCurrentVersion", juce::String(currentVersionId_));
  broadcastMessage("setDetachedHead", isDetached_);
}

void MainComponent::broadcastMessage(const juce::String &type,
                                     const juce::var &data) {
  auto *envelope = new juce::DynamicObject();
  envelope->setProperty("type", type);
  envelope->setProperty("data", data);
  juce::String json = juce::JSON::toString(juce::var(envelope), true);
  juce::String js =
      "window.__dispatchFromCpp && window.__dispatchFromCpp(" + json + ")";
  webViewBridge_.broadcastJavascript(
      js, historyWindow_ ? &historyWindow_->getWebView() : nullptr,
      libraryManagerWindow_ ? &libraryManagerWindow_->getWebView() : nullptr);
  // Include debug window so broadcastMessage reaches the Plugins/Timeline
  // panels
  if (debugWindow_) {
    debugWindow_->evaluateJavascript(js);
  }
}

void MainComponent::pushLibraryCatalogHistory() {
  auto *state = new juce::DynamicObject();
  state->setProperty("canUndo", libraryUndoManager_.canUndo());
  state->setProperty("canRedo", libraryUndoManager_.canRedo());
  state->setProperty("undoDescription", libraryUndoManager_.undoDescription());
  state->setProperty("redoDescription", libraryUndoManager_.redoDescription());
  broadcastMessage("setLibraryCatalogHistory", juce::var(state));
  juce::Array<juce::var> libraries;
  for (const auto &library : db_.listLibraries()) {
    auto *item = new juce::DynamicObject();
    item->setProperty("id", library.id); item->setProperty("name", library.name);
    item->setProperty("vendor", library.vendor); item->setProperty("variant", library.variant);
    libraries.add(juce::var(item));
  }
  broadcastMessage("setLibraryList", juce::var(libraries));
}

void MainComponent::setupJsHandlers() {
  jsRouter_.registerHandler("setLibraryDraftDirty", [this](const juce::var &payload) {
    if (payload.isArray() && payload.size() > 0) libraryDraftDirty_ = (bool)payload[0];
  });
  libraryUndoManager_.onChanged = [this] { pushLibraryCatalogHistory(); };
  jsRouter_.registerHandler("requestLibraryCatalogHistory", [this](const juce::var &) {
    safeCallAsync([this] { pushLibraryCatalogHistory(); });
  });
  for (const auto &command : {juce::String("undoLibraryCatalog"), juce::String("redoLibraryCatalog")}) {
    jsRouter_.registerHandler(command, [this, command](const juce::var &) {
      safeCallAsync([this, command] {
        const bool success = command == "undoLibraryCatalog" ? libraryUndoManager_.undo() : libraryUndoManager_.redo();
        if (!success) broadcastMessage("setLibraryDeleteResult",
          juce::var("Catalog history could not be applied. A patch may now be used by a layer. History has been kept."));
        pushLibraryCatalogHistory();
        pushLayerCatalog();
        pushMixerState(false);
        pushLibraryLayerStatus();
      });
    });
  }
  jsRouter_.registerHandler("showAudioSettings", [this](const juce::var &) {
    safeCallAsync([this] { showAudioSettings(); });
  });
  jsRouter_.registerHandler("setRenderWorkerCount", [this](const juce::var &payload) {
    if (!payload.isArray() || payload.size() != 1) return;
    const auto value = static_cast<double>(payload[0]);
    if (value != 1.0 && value != 2.0 && value != 4.0) return;
    safeCallAsync([this, count = static_cast<int>(value)] {
      if (isTransportStarted_.load(std::memory_order_acquire) ||
          (renderAhead_ && renderAhead_->mixPrintIsArmed())) {
        pushLogMessage("Stop Dorico playback and disarm printing before changing render workers.", true);
        return;
      }
      mixer_.setRenderWorkerCount(count);
      // Local preference only: never included in project versions or undo.
      db_.saveSetting("audio_render_workers", std::to_string(count));
      pushAudioDiagnostics();
    });
  });
  jsRouter_.registerHandler("requestMixPrintState", [this](const juce::var &) {
    safeCallAsync([this] { pushMixPrintState(); });
  });
  jsRouter_.registerHandler("startMixPrint", [this](const juce::var &) {
    safeCallAsync([this] {
      if (isTransportStarted_.load(std::memory_order_acquire)) {
        pushMixPrintState("Stop Dorico playback before arming a print");
        return;
      }
      const auto defaultName =
          "Fiddle Mix " +
          juce::Time::getCurrentTime().formatted("%Y-%m-%d %H-%M-%S") +
          ".wav";
      auto chooser = std::make_shared<juce::FileChooser>(
          "Print Fiddle Master Mix",
          juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
              .getChildFile(defaultName),
          "*.wav");
      const juce::Component::SafePointer<MainComponent> safeThis(this);
      chooser->launchAsync(
          juce::FileBrowserComponent::saveMode |
              juce::FileBrowserComponent::canSelectFiles |
              juce::FileBrowserComponent::warnAboutOverwriting,
          [safeThis, chooser](const juce::FileChooser &fc) {
            if (safeThis == nullptr)
              return;
            const auto results = fc.getResults();
            if (results.isEmpty())
              return;
            if (safeThis->isTransportStarted_.load(
                    std::memory_order_acquire)) {
              safeThis->pushMixPrintState(
                  "Stop Dorico playback before arming a print");
              return;
            }
            auto file = results[0];
            if (file.getFileExtension().isEmpty())
              file = file.withFileExtension(".wav");
            const auto error = safeThis->renderAhead_
                                   ? safeThis->renderAhead_->startMixPrint(file)
                                   : juce::String("Audio rendering is not running");
            safeThis->pushMixPrintState(error);
          });
    });
  });
  jsRouter_.registerHandler("stopMixPrint", [this](const juce::var &) {
    safeCallAsync([this] {
      if (renderAhead_)
        renderAhead_->stopMixPrint();
      pushMixPrintState();
    });
  });
  jsRouter_.registerHandler("copyAudioDiagnosticsReport", [](const juce::var &payload) {
    if (payload.isArray() && payload.size() > 0)
      juce::SystemClipboard::copyTextToClipboard(payload[0].toString());
  });
  undoManager_.onChanged = [this] {
    safeCallAsync([this] { pushUndoState(); });
  };
  jsRouter_.registerHandler("beginHistoryGesture", [this](const juce::var &) {
    safeCallAsync([this] { undoManager_.beginGesture(); });
  });
  jsRouter_.registerHandler("setMixerControls", [this](const juce::var &payload) {
    if (!payload.isArray() || payload.size() < 2 || !payload[0].isArray()) return;
    const auto values = payload[0];
    const auto label = payload[1].toString();
    const auto levels = payload.size() > 2 ? payload[2] : juce::var();
    safeCallAsync([this, values, label, levels] {
      std::optional<ProjectSettings> settings;
      if (!levels.isVoid()) {
        if (!levels.isArray()) return;
        settings = mixer_.projectSettings();
        for (const auto &value : *levels.getArray()) {
          const auto id = value["id"].toString().toStdString();
          if (!settings->lockedChairIds.count(id) || !value.hasProperty("targetDb")) return;
          ChairLevelState level;
          level.targetDb = static_cast<double>(value["targetDb"]);
          if (!std::isfinite(level.targetDb) || level.targetDb < -120 || level.targetDb > 60) return;
          const auto *weights = value["weights"].getDynamicObject();
          if (!weights) return;
          for (const auto &entry : weights->getProperties()) {
            const double weight = static_cast<double>(entry.value);
            if (!std::isfinite(weight) || weight < 0 || weight > 1e6) return;
            const auto stripId = entry.name.toString();
            const auto *strip = mixer_.getStrip(stripId);
            if (!strip) continue; // A remembered layer may have since been removed.
            if (strip->chairId.toStdString() != id) return;
            level.weights[stripId.toStdString()] = weight;
          }
          settings->chairLevels[id] = std::move(level);
        }
      }
      std::vector<SetMixerControlsAction::Change> changes;
      std::set<juce::String> seen;
      for (const auto &value : *values.getArray()) {
        const auto id = value["id"].toString();
        auto *strip = mixer_.getStrip(id);
        if (!strip || !seen.insert(id).second) return;
        const auto before = SetMixerControlsAction::Values::of(*strip);
        auto after = before;
        if (value.hasProperty("gainDb")) {
          const float gain = static_cast<float>(value["gainDb"]);
          if (!std::isfinite(gain)) return;
          after.gainDb = std::clamp(gain, -120.0f, 6.0f);
        }
        if (value.hasProperty("muted")) after.muted = value["muted"];
        if (value.hasProperty("soloed")) after.soloed = value["soloed"];
        if (value.hasProperty("active")) after.active = value["active"];
        changes.push_back({id, before, after});
      }
      if (undoManager_.perform(std::make_unique<SetMixerControlsAction>(
              mixer_, std::move(changes), label, label == "Adjust layer gains", std::move(settings)))) {
        saveAllStripsToDB(false);
        pushMixerState();
        scheduleStateRebuild();
      }
    });
  });
  jsRouter_.registerHandler("clearSolos", [this](const juce::var &) {
    safeCallAsync([this] {
      std::vector<std::unique_ptr<UndoableAction>> actions;
      std::vector<SetMixerControlsAction::Change> changes;
      for (auto *strip : mixer_.getAllStrips()) {
        if (!strip->isSoloed()) continue;
        const auto before = SetMixerControlsAction::Values::of(*strip);
        auto after = before;
        after.soloed = false;
        changes.push_back({strip->id, before, after});
      }
      if (!changes.empty()) actions.push_back(std::make_unique<SetMixerControlsAction>(
          mixer_, std::move(changes), "Clear layer solos"));
      for (auto *bus : mixer_.getAllGroupBuses())
        if (bus->isSoloed()) actions.push_back(std::make_unique<SetGroupBusSoloAction>(
            mixer_, bus->id, true, false));
      if (undoManager_.perform(std::make_unique<CompoundAction>("Clear solos", std::move(actions)))) {
        saveAllStripsToDB(false);
        pushMixerState();
        pushGroupBusState();
        scheduleStateRebuild();
      }
    });
  });
  jsRouter_.registerHandler("endHistoryGesture", [this](const juce::var &) {
    safeCallAsync([this] { undoManager_.endGesture(); });
  });
  jsRouter_.registerHandler("setChairLevelLock", [this](const juce::var &payload) {
    if (!payload.isArray() || payload.size() < 2) return;
    const auto id = payload[0].toString().toStdString();
    const bool locked = payload[1];
    safeCallAsync([this, id, locked] {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository || !repository->getChair(id)) return;
      auto settings = mixer_.projectSettings();
      if (locked) {
        settings.lockedChairIds.insert(id);
        settings.chairLevels[id] = mixer_.captureChairLevel(id);
      } else {
        settings.lockedChairIds.erase(id);
        settings.chairLevels.erase(id);
      }
      if (undoManager_.perform(std::make_unique<SetProjectSettingsAction>(
              mixer_, std::move(settings), "Change chair level lock"))) {
        saveAllStripsToDB(false);
        pushMixerState();
        scheduleStateRebuild();
      }
    });
  });
  mixerCommandService_ = std::make_unique<MixerCommandService>(
      mixer_, undoManager_,
      [this](const juce::String &stripId) { pluginProgramApplied(stripId); });
  mixerJsHandlers_ = std::make_unique<MixerJsHandlers>(
      jsRouter_, *mixerCommandService_,
      MixerJsHandlers::Callbacks{[this](MixerJsHandlers::Task task) {
                                   safeCallAsync(std::move(task));
                                 },
                                 [this] { pushMixerState(); },
                                 [this] { saveAllStripsToDB(false); },
                                 [this] {
                                   const auto now =
                                       juce::Time::getMillisecondCounter();
                                   processPluginChangeNotifications(
                                       shouldSuppressPluginChanges(now));
                                 }});
  mixerJsHandlers_->registerHandlers();

  groupBusCommandService_ =
      std::make_unique<GroupBusCommandService>(mixer_, pluginScanner_,
                                                undoManager_);
  groupBusJsHandlers_ = std::make_unique<GroupBusJsHandlers>(
      jsRouter_, *groupBusCommandService_,
      GroupBusJsHandlers::Callbacks{
          [this](GroupBusJsHandlers::Task task) {
            safeCallAsync(std::move(task));
          },
          [this] {
            for (auto *bus : mixer_.getAllGroupBuses())
              setupGroupBusAudioCallbacks(*bus);
            pushGroupBusState();
            pushMixerState(true);
            scheduleStateRebuild();
          },
          [this] { pushGroupBusState(); }});
  groupBusJsHandlers_->registerHandlers();

  masterAudioCommandService_ = std::make_unique<MasterAudioCommandService>(
      mixer_, pluginScanner_, undoManager_);
  masterAudioJsHandlers_ = std::make_unique<MasterAudioJsHandlers>(
      jsRouter_, *masterAudioCommandService_,
      MasterAudioJsHandlers::Callbacks{
          [this](MasterAudioJsHandlers::Task task) {
            safeCallAsync(std::move(task));
          },
          [this](const juce::var &state) {
            broadcastMessage("setMasterAudioState", state);
          }});
  masterAudioJsHandlers_->registerHandlers();
  mixer_.masterAudio().setOnChanged([this] { masterAudioChanged(); });
  mixer_.masterAudio().setOnEditorVisibilityChanged([this] { pushMasterAudioState(); });

  stripAudioCommandService_ = std::make_unique<StripAudioCommandService>(
      mixer_, pluginScanner_, undoManager_);
  stripAudioJsHandlers_ = std::make_unique<StripAudioJsHandlers>(
      jsRouter_, *stripAudioCommandService_,
      StripAudioJsHandlers::Callbacks{
          [this](StripAudioJsHandlers::Task task) {
            safeCallAsync(std::move(task));
          },
          [this](const juce::var &state) {
            broadcastMessage("setStripAudioState", state);
          }});
  stripAudioJsHandlers_->registerHandlers();

  pluginCommandService_ = std::make_unique<PluginCommandService>(
      mixer_, pluginScanner_, undoManager_, db_);
  juce::Component::SafePointer<MainComponent> safeThis(this);
  pluginJsHandlers_ = std::make_unique<PluginJsHandlers>(
      jsRouter_, *pluginCommandService_,
      PluginJsHandlers::Callbacks{
          [this](PluginJsHandlers::Task task) {
            safeCallAsync(std::move(task));
          },
          [safeThis](const juce::String &message) {
            if (safeThis != nullptr)
              safeThis->pushLogMessage(message);
          },
          [safeThis](bool scanning) {
            if (safeThis != nullptr)
              safeThis->broadcastMessage("isScanningPlugins", scanning);
          },
          [safeThis](const juce::var &plugins) {
            if (safeThis != nullptr)
              safeThis->broadcastMessage("setPluginList", plugins);
          },
          [safeThis] {
            if (safeThis != nullptr) {
              safeThis->safeCallAsync([safeThis] {
                if (safeThis == nullptr) return;
                // Completion can occur after Undo has returned to the saved
                // state. Loading that state is not a new user edit.
                const bool dirty = !safeThis->undoManager_.isAtSavePoint();
                safeThis->saveAllStripsToDB(false);
                safeThis->pushMixerState(dirty);
                if (!dirty) {
                  safeThis->stateManager_.clearDirty();
                  safeThis->broadcastMessage("setDirtyState", false);
                }
                safeThis->scheduleStateRebuild();
              });
            }
          }});
  pluginJsHandlers_->registerHandlers();

  expressionMapCommandService_ = std::make_unique<ExpressionMapCommandService>(
      mixer_, xmapLibrary_, undoManager_);
  expressionMapJsHandlers_ = std::make_unique<ExpressionMapJsHandlers>(
      jsRouter_, *expressionMapCommandService_,
      ExpressionMapJsHandlers::Callbacks{
          [this](ExpressionMapJsHandlers::Task task) {
            safeCallAsync(std::move(task));
          },
          [safeThis](ExpressionMapJsHandlers::FileSelection selection) {
            if (safeThis == nullptr)
              return;
            auto chooser = std::make_shared<juce::FileChooser>(
                "Load Expression Map", juce::File{}, "*.doricolib");
            chooser->launchAsync(
                juce::FileBrowserComponent::openMode |
                    juce::FileBrowserComponent::canSelectFiles,
                [safeThis, chooser, selection = std::move(selection)](
                    const juce::FileChooser &fc) {
                  if (safeThis == nullptr)
                    return;
                  const auto results = fc.getResults();
                  if (!results.isEmpty())
                    selection(results[0]);
                });
          },
          [safeThis](const juce::var &catalog) {
            if (safeThis != nullptr)
              safeThis->broadcastMessage("setExpressionMaps", catalog);
          },
          [safeThis](const juce::var &details) {
            if (safeThis != nullptr)
              safeThis->broadcastMessage("setExpressionMapDetails", details);
          },
          [safeThis] {
            if (safeThis != nullptr) {
              safeThis->saveAllStripsToDB(false);
              safeThis->pushMixerState();
              safeThis->scheduleStateRebuild();
            }
          }});
  expressionMapJsHandlers_->registerHandlers();

  jsRouter_.registerHandler("signalReady", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    juce::String viewMode = "mixer";
    if (args.size() > 0) {
      viewMode = args[0].toString();
    }

    juce::WebBrowserComponent *targetWebComponent =
        &webViewBridge_.getMainWebComponent();
    bool isHistoryWindow = false;

    if (viewMode == "history" && historyWindow_) {
      targetWebComponent = &(historyWindow_->getWebView());
      historyWindowLoaded_ = true;
      isHistoryWindow = true;
      std::cerr << "[WebView] Handshake: History window ready" << std::endl;

    } else if (viewMode == "library" && libraryManagerWindow_) {
      targetWebComponent = &(libraryManagerWindow_->getWebView());
      libraryManagerWindowLoaded_ = true;
      isHistoryWindow = true; // Reuse flag to skip main-window-only init
      std::cerr << "[WebView] Handshake: Library Manager window ready"
                << std::endl;
      if (pendingGuidedLibraryId_.isNotEmpty()) {
        broadcastMessage("openGuidedLibrary", pendingGuidedLibraryId_);
        pendingGuidedLibraryId_.clear();
      }
    } else {
      webViewBridge_.setLoaded(true);
      std::cerr << "[WebView] Handshake: Main window ready" << std::endl;

      std::vector<std::pair<juce::String, bool>> pending;
      {
        std::lock_guard<std::mutex> lock(logMutex);
        pending.swap(logQueue);
      }
      for (const auto &item : pending) {
        pushLogMessage(item.first, item.second);
      }

      pushLogMessage("<i>Server started and listening for connections...</i>");
    }

    // Send Version
    if (auto *app = juce::JUCEApplication::getInstance()) {
      broadcastMessage("setServerVersion",
                       juce::var(app->getApplicationVersion()));
    }

    if (connectionWarning_.isNotEmpty())
      broadcastMessage("setConnectionWarning", connectionWarning_);

    // Push current BPM
    double bpm = currentBpm_.load(std::memory_order_relaxed);
    if (bpm > 0.0) {
      juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
      tempoObj->setProperty("bpm", bpm);
      // Hardcode initial marker to 0, future ones get correct absoluteSamples
      tempoObj->setProperty("samplePosition", (juce::int64)0);
      tempoObj->setProperty("timeSigNumerator", 4);
      tempoObj->setProperty("timeSigDenominator", 4);
      broadcastMessage("setTempo", juce::var(tempoObj.get()));
    }

    // Push channel map (port/channel → instrument) to Timeline
    {
      juce::String mapJson = masterList_.getChannelMapAsJson();
      broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson));
    }

    // Push cached plugin list (if any prior scan exists)
    if (pluginScanner_.getPluginCount() > 0) {
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    }

    // Push current mixer state
    if (!isHistoryWindow) {
      pushMixerState(false);

      // Push available Lua plugins catalog
      {
        juce::Array<juce::var> pluginArr;
        for (const auto &meta : luaCatalog_.plugins()) {
          auto *obj = new juce::DynamicObject();
          obj->setProperty("name", juce::String(meta.name));
          obj->setProperty("version", juce::String(meta.version));
          obj->setProperty("author", juce::String(meta.author));
          obj->setProperty("description", juce::String(meta.description));
          obj->setProperty("filePath", juce::String(meta.filePath));
          juce::Array<juce::var> params;
          for (const auto &p : meta.params) {
            auto *pObj = new juce::DynamicObject();
            pObj->setProperty("name", juce::String(p.name));
            pObj->setProperty("type", juce::String(p.type));
            pObj->setProperty("min", p.min);
            pObj->setProperty("max", p.max);
            pObj->setProperty("default", p.defaultVal);
            juce::Array<juce::var> opts;
            for (const auto &o : p.options)
              opts.add(juce::String(o));
            pObj->setProperty("options", opts);
            params.add(juce::var(pObj));
          }
          obj->setProperty("params", params);
          pluginArr.add(juce::var(obj));
        }
        broadcastMessage("setLuaPluginCatalog", juce::var(pluginArr));
      }

      // Send Dorico instruments (with score order) so mixer can sort
      juce::String instrJson = instrumentBrowser_.getInstrumentsAsJson();
      safeCallAsync([this, instrJson]() {
        broadcastMessage("setDoricoInstruments",
                         juce::JSON::fromString(instrJson));
      });
    }

    // Restore saved main window mode.
    if (!isHistoryWindow) {
      auto mws = db_.loadWindowSettings("main");
      juce::String mode = mws.mode;
      broadcastMessage("setMainMode", juce::var(mode));
    }

    if (isHistoryWindow) {
      safeCallAsync([this]() {
        pushDagHistory();
        pushBranches();
        pushCurrentVersion();
      });
    }

    // Push branches to main window
    if (!isHistoryWindow) {
      pushBranches();
      pushCurrentVersion();
    }

    // Reveal the WebView now that it's fully loaded
    if (!isHistoryWindow) {
      initComplete_ = true;
      webViewBridge_.getMainWebComponent().setBounds(getLocalBounds());
      repaint();
    }
    return;
  });
  jsRouter_.registerHandler("setMode", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() > 0) {
      juce::String mode = args[0].toString();
      if (mode == "mixer" || mode == "setup") {
        // Save mode to window_settings for the main window
        auto ws = db_.loadWindowSettings("main");
        ws.mode = mode;
        db_.saveWindowSettings(ws);
      }
    }
    return;
  });
  jsRouter_.registerHandler("nativeLog", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() > 0)
      std::cerr << "[JS NativeLog] " << args[0].toString() << std::endl;
    return;
  });
  jsRouter_.registerHandler("requestSetupData", [this](
                                                    const juce::var &payload) {
    std::cerr << "[Setup] requestSetupData called" << std::endl;

    // Send Dorico instruments list
    {
      auto t0 = std::chrono::steady_clock::now();
      juce::String instrJson = instrumentBrowser_.getInstrumentsAsJson();
      auto t1 = std::chrono::steady_clock::now();
      std::cerr << "[Setup] Built instrument JSON: "
                << std::chrono::duration_cast<std::chrono::milliseconds>(t1 -
                                                                         t0)
                       .count()
                << "ms, " << instrJson.length() << " chars" << std::endl;
      safeCallAsync([this, instrJson]() {
        broadcastMessage("setDoricoInstruments",
                         juce::JSON::fromString(instrJson));
      });
    }

    // Push saved selections to the UI
    {
      juce::String selJson = masterList_.getSlotsAsJson();
      safeCallAsync([this, selJson]() {
        broadcastMessage("setSelectedInstruments",
                         juce::JSON::fromString(selJson));
      });
    }

    // Push channel map (port/channel → instrument) to the UI
    {
      juce::String mapJson = masterList_.getChannelMapAsJson();
      safeCallAsync([this, mapJson]() {
        broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson));
      });
    }
    return;
  });
  jsRouter_.registerHandler(
      "saveSelectedInstruments", [this](const juce::var &payload) {
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();

        if (args.size() < 1) {
          safeCallAsync([this]() {
            broadcastMessage("setSaveResult", juce::var("Error: no data"));
          });
          return;
        }
        juce::String json = args[0].toString();
        if (masterList_.setSlotsFromJson(json)) {
          masterList_.saveToDB(db_);

          // Reconcile stable channel assignments
          bool compacted = masterList_.reconcileAssignments(db_);

          // Generate Dorico config files
          DoricoConfigGenerator generator;
          auto slots = masterList_.getSlots();
          auto assignments = DoricoConfigGenerator::expandSlots(slots);
          int numChannels = masterList_.totalSlotCount();

          auto result = generator.generateAndInstallFiles(
              assignments, numChannels, instrumentBrowser_.getInstruments());
          juce::String msg;
          if (result.wasOk()) {
            msg = "OK: Installed " + juce::String((int)assignments.size()) +
                  " presets (" + juce::String(numChannels) + " channels)";

            // Rebuild channel_assignments from the sequential flat indices used
            // by the generated Dorico playback template. reconcileAssignments
            // above tries to preserve stable indices across score changes, but
            // can diverge from the template's sequential order (e.g. after
            // reordering ensemble slots). By rebuilding here we guarantee the
            // DB always matches the MIDI port/channel layout that Dorico will
            // use.
            std::vector<ChannelAssignmentRow> newRows;
            newRows.reserve(assignments.size());
            // Track instanceNum per (entityID, isSolo) pair
            std::map<std::pair<juce::String, bool>, int> instanceCounts;
            for (int idx = 0; idx < (int)assignments.size(); ++idx) {
              const auto &a = assignments[idx];
              auto key = std::make_pair(a.entityID, a.isSolo);
              int instanceNum = ++instanceCounts[key];
              ChannelAssignmentRow row;
              row.flatIndex = idx;
              row.entityID = a.entityID;
              row.isSolo = a.isSolo;
              row.instanceNum = instanceNum;
              newRows.push_back(row);
            }
            db_.saveChannelAssignments(newRows);
            // Reload into masterList so getChannelMapAsJson reflects new order
            masterList_.reconcileAssignments(db_);
          } else {
            msg = "Error: " + result.getErrorMessage();
          }
          safeCallAsync([this, msg]() {
            broadcastMessage("setSaveResult", juce::var(msg));
          });

          if (compacted) {
            pushLogMessage("<b>[Setup]</b> ⚠️ Channel assignments were "
                           "compacted. Existing Dorico projects may need "
                           "their playback template re-applied.");
          }

          // Sync mixer strips to match updated instruments
          mixer_.syncStripsToInstruments(masterList_);

          // Update channel map for Timeline
          juce::String mapJson2 = masterList_.getChannelMapAsJson();
          safeCallAsync([this, mapJson2]() {
            broadcastMessage("setInstrumentMap",
                             juce::JSON::fromString(mapJson2));
          });

          // Push updated mixer state to UI
          pushMixerState();
        } else {
          safeCallAsync([this]() {
            broadcastMessage("setSaveResult", juce::var("Error: Invalid JSON"));
          });
        }
        return;
      });
  jsRouter_.registerHandler(
      "getAnnotationRecords", [this](const juce::var &payload) {
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();

        if (args.size() < 1)
          return;
        juce::String stripId = args[0].toString();
        safeCallAsync([this, stripId]() {
          auto *strip = mixer_.getStrip(stripId);
          if (!strip)
            return;
          juce::String json = strip->getAnnotationRecordsAsJson();
          auto *envelope = new juce::DynamicObject();
          envelope->setProperty("stripId", stripId);
          envelope->setProperty("records", juce::JSON::parse(json));
          broadcastMessage("setAnnotationRecords", juce::var(envelope));
        });
        return;
      });
  jsRouter_.registerHandler("clearAnnotationRecords",
                            [this](const juce::var &payload) {
                              juce::Array<juce::var> args;
                              if (payload.isArray())
                                args = *payload.getArray();

                              if (args.size() < 1)
                                return;
                              juce::String stripId = args[0].toString();
                              safeCallAsync([this, stripId]() {
                                auto *strip = mixer_.getStrip(stripId);
                                if (!strip)
                                  return;
                                strip->clearAnnotations();
                              });
                              return;
                            });
  jsRouter_.registerHandler("startMidiCapture", [this](
                                                    const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    juce::String mode = args[1].toString(); // "incoming", "emitted", or "both"

    auto *strip = mixer_.getStrip(stripId);
    if (!strip)
      return;
    if (mode == "incoming" || mode == "both")
      strip->incomingCapture.startCapture();
    if (mode == "emitted" || mode == "both")
      strip->emittedCapture.startCapture();
    return;
  });
  jsRouter_.registerHandler("stopMidiCapture",
                            [this](const juce::var &payload) {
                              juce::Array<juce::var> args;
                              if (payload.isArray())
                                args = *payload.getArray();
                              if (args.size() < 2)
                                return;
                              juce::String stripId = args[0].toString();
                              juce::String mode = args[1].toString();

                              auto *strip = mixer_.getStrip(stripId);
                              if (!strip)
                                return;
                              if (mode == "incoming" || mode == "both")
                                strip->incomingCapture.stopCapture();
                              if (mode == "emitted" || mode == "both")
                                strip->emittedCapture.stopCapture();
                              return;
                            });
  jsRouter_.registerHandler("getMidiCapture", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    juce::String mode = args[1].toString(); // "incoming" or "emitted"

    safeCallAsync([this, stripId, mode]() {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip)
        return;
      auto *envelope = new juce::DynamicObject();
      envelope->setProperty("stripId", stripId);
      envelope->setProperty("mode", mode);
      if (mode == "incoming")
        envelope->setProperty("events", strip->incomingCapture.toVar());
      else
        envelope->setProperty("events", strip->emittedCapture.toVar());
      envelope->setProperty("count",
                            (int)(mode == "incoming"
                                      ? strip->incomingCapture.eventCount()
                                      : strip->emittedCapture.eventCount()));
      broadcastMessage("setMidiCapture", juce::var(envelope));
    });
    return;
  });
  jsRouter_.registerHandler("clearMidiCapture",
                            [this](const juce::var &payload) {
                              juce::Array<juce::var> args;
                              if (payload.isArray())
                                args = *payload.getArray();
                              if (args.size() < 2)
                                return;
                              juce::String stripId = args[0].toString();
                              juce::String mode = args[1].toString();

                              auto *strip = mixer_.getStrip(stripId);
                              if (!strip)
                                return;
                              if (mode == "incoming" || mode == "both")
                                strip->incomingCapture.clear();
                              if (mode == "emitted" || mode == "both")
                                strip->emittedCapture.clear();
                              return;
                            });
  jsRouter_.registerHandler(
      "getCaptureStripList", [this](const juce::var &payload) {
        safeCallAsync([this]() {
          juce::Array<juce::var> arr;
          for (auto *strip : mixer_.getAllStrips()) {
            // Only show strips that have an expression map assigned
            if (!strip->expressionMap)
              continue;
            auto *obj = new juce::DynamicObject();
            obj->setProperty("id", strip->id);
            obj->setProperty("name",
                             strip->library.isNotEmpty()
                                 ? strip->library + " (" + strip->family + ")"
                                 : strip->id);
            const auto state = strip->realtimeState();
            obj->setProperty("port", state.inputPort + 1); // 1-based for Dorico
            obj->setProperty("channel", state.inputChannel);
            arr.add(juce::var(obj));
          }
          broadcastMessage("setCaptureStripList", juce::var(arr));
        });
        return;
      });
  jsRouter_.registerHandler("exportCaptureFile", [this](
                                                     const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String text = args[0].toString();
    juce::String defaultName = args[1].toString();

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export MIDI Capture",
        juce::File::getSpecialLocation(juce::File::userDesktopDirectory)
            .getChildFile(defaultName + ".midi-dump"),
        "*.midi-dump");

    chooser->launchAsync(juce::FileBrowserComponent::saveMode |
                             juce::FileBrowserComponent::canSelectFiles |
                             juce::FileBrowserComponent::warnAboutOverwriting,
                         [text, chooser](const juce::FileChooser &fc) {
                           auto results = fc.getResults();
                           if (results.isEmpty())
                             return;
                           auto file = results[0];
                           file.replaceWithText(text);
                         });
    return;
  });
  jsRouter_.registerHandler("addStripLuaPlugin", [this](
                                                     const juce::var &payload) {
    // payload: [stripId, pluginFilePath]
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    std::string pluginPath = args[1].toString().toStdString();

    // Verify the plugin is in the catalog (try full path first, then just
    // filename)
    std::string fileName =
        std::filesystem::path(pluginPath).filename().string();
    const auto *meta = luaCatalog_.findByPath(pluginPath);
    if (!meta)
      meta = luaCatalog_.findByFileName(fileName);
    if (!meta) {
      std::cerr << "[Lua] Plugin not in catalog: " << pluginPath << std::endl;
      return;
    }
    const auto pluginReference = meta->filePath;
    safeCallAsync([this, stripId, pluginReference]() {
      auto action = std::make_unique<AddLuaPluginAction>(mixer_, luaCatalog_,
                                                         stripId,
                                                         pluginReference);
      if (!undoManager_.perform(std::move(action))) {
        pushLogMessage("<b>[Lua]</b> Could not add processor; the script "
                       "did not load and no history entry was created.",
                       true);
        return;
      }
      saveAllStripsToDB(false);
      pushMixerState();
      scheduleStateRebuild();
    });
    return;
  });
  jsRouter_.registerHandler(
      "removeStripLuaPlugin", [this](const juce::var &payload) {
        // payload: [stripId, pluginIndex]
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();
        if (args.size() < 2)
          return;
        juce::String stripId = args[0].toString();
        int pluginIndex = (int)args[1];

        safeCallAsync([this, stripId, pluginIndex]() {
          auto action = std::make_unique<RemoveLuaPluginAction>(
              mixer_, stripId, pluginIndex);
          if (!undoManager_.perform(std::move(action))) {
            pushLogMessage("<b>[Lua]</b> Could not remove processor; no "
                           "history entry was created.",
                           true);
            return;
          }
          saveAllStripsToDB(false);
          pushMixerState();
          scheduleStateRebuild();
        });
        return;
      });
  jsRouter_.registerHandler(
      "getLuaPluginCatalog", [this](const juce::var &payload) {
        // Re-push the catalog (e.g., after a rescan)
        juce::Array<juce::var> pluginArr;
        for (const auto &meta : luaCatalog_.plugins()) {
          auto *obj = new juce::DynamicObject();
          obj->setProperty("name", juce::String(meta.name));
          obj->setProperty("version", juce::String(meta.version));
          obj->setProperty("author", juce::String(meta.author));
          obj->setProperty("description", juce::String(meta.description));
          obj->setProperty("filePath", juce::String(meta.filePath));
          pluginArr.add(juce::var(obj));
        }
        broadcastMessage("setLuaPluginCatalog", juce::var(pluginArr));
        return;
      });
  jsRouter_.registerHandler("requestChairs", [this](const juce::var &) {
    safeCallAsync([this]() {
      pushChairState();
      pushUndoState();
      pushLayerCatalog();
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("requestDoricoInstruments",
                            [this](const juce::var &) {
    const auto instruments = instrumentBrowser_.getInstrumentsAsJson();
    safeCallAsync([this, instruments]() {
      broadcastMessage("setDoricoInstruments",
                       juce::JSON::fromString(instruments));
    });
    return;
  });
  jsRouter_.registerHandler("requestLayerCatalog", [this](const juce::var &) {
    safeCallAsync([this]() { pushLayerCatalog(); });
    return;
  });
  jsRouter_.registerHandler("assignPatchToChair",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    auto *data = args.isEmpty() ? nullptr : args[0].getDynamicObject();
    if (!data)
      return;
    const auto chairId =
        data->getProperty("chairId").toString().toStdString();
    const auto patchId =
        data->getProperty("patchId").toString().toStdString();
    safeCallAsync([this, chairId, patchId]() {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository || !repository->getChair(chairId) ||
          !repository->getPatch(patchId)) {
        broadcastMessage("layerOperationResult",
                         juce::var("Could not find the chair or patch"));
        return;
      }
      int position = 0;
      for (const auto &existing : repository->listLayers(chairId))
        position = std::max(position, existing.position + 1);
      undoManager_.perform(std::make_unique<AddChairLayerAction>(
          *repository, mixer_, chairId, patchId, position,
          [this, repository](const LayerRow &layer) {
            const auto chair = repository->getChair(layer.chairId);
            const auto patch = repository->getPatch(layer.patchId);
            return chair && patch &&
                   instantiateLayer(layer, *chair, &*patch,
                                    juce::String(layer.libraryName));
          },
          [this](bool success) {
            if (!success) {
              broadcastMessage("layerOperationResult",
                               juce::var("Could not change the layer"));
              return;
            }
            saveAllStripsToDB();
            pushChairState();
            pushMixerState();
            scheduleStateRebuild();
          }));
    });
    return;
  });
  jsRouter_.registerHandler("removeChairLayer",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;
    const auto layerId = args[0].toString();
    safeCallAsync([this, layerId]() {
      auto *repository = db_.getLibraryRoutingRepository();
      const auto layer = repository
                             ? repository->getLayer(layerId.toStdString())
                             : std::nullopt;
      if (!layer || !mixer_.getStrip(layerId)) {
        broadcastMessage("layerOperationResult",
                         juce::var("Could not remove the layer"));
        return;
      }
      undoManager_.perform(std::make_unique<RemoveChairLayerAction>(
          *repository, mixer_, *layer, [this](bool success) {
            if (!success) {
              broadcastMessage("layerOperationResult",
                               juce::var("Could not change the layer"));
              return;
            }
            // Also run on undo/redo: persist the retained live setup and
            // refresh chair membership, not just the visible strip list.
            saveAllStripsToDB();
            pushChairState();
            pushMixerState();
            scheduleStateRebuild();
          }));
    });
    return;
  });
  jsRouter_.registerHandler("refreshChairLayerFromLibrary",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;
    const auto layerId = args[0].toString().toStdString();
    safeCallAsync([this, layerId]() {
      auto *repository = db_.getLibraryRoutingRepository();
      auto before = repository ? repository->getLayer(layerId) : std::nullopt;
      auto *strip = mixer_.getStrip(juce::String(layerId));
      const auto patch = before && repository
                             ? repository->getPatch(before->patchId)
                             : std::nullopt;
      if (!before || !strip || !patch) {
        broadcastMessage("layerOperationResult",
                         juce::var("Could not refresh the layer from its "
                                   "library patch"));
        return;
      }

      if (!refreshLayersFromPatch(*patch, {*before})) {
        broadcastMessage("layerOperationResult", juce::var("Could not refresh the layer"));
        return;
      }
      pushMixerState();
      scheduleStateRebuild();

      auto *result = new juce::DynamicObject();
      result->setProperty("success", true);
      result->setProperty("operation", "refresh");
      result->setProperty("layerId", juce::String(layerId));
      result->setProperty("message", "Layer refreshed from library");
      broadcastMessage("layerOperationResult", juce::var(result));
    });
    return;
  });
  jsRouter_.registerHandler("createChair", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    auto *data = args.isEmpty() ? nullptr : args[0].getDynamicObject();
    if (!data)
      return;

    ChairRow chair;
    chair.id = juce::Uuid().toString().toStdString();
    chair.instrumentEntityId =
        data->getProperty("entityID").toString().toStdString();
    chair.name = data->getProperty("name").toString().trim().toStdString();
    chair.family = data->getProperty("family").toString().toStdString();
    chair.role = data->getProperty("role").toString() == "section"
                     ? DoricoRole::section
                     : DoricoRole::solo;

    safeCallAsync([this, chair = std::move(chair)]() mutable {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository) { chairEditChanged(false); return; }
      undoManager_.perform(std::make_unique<AddChairAction>(
          *repository, mixer_, std::move(chair),
          [this](bool success) { chairEditChanged(success); }));
    });
    return;
  });
  jsRouter_.registerHandler("updateChair", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    auto *data = args.isEmpty() ? nullptr : args[0].getDynamicObject();
    if (!data)
      return;
    const auto chairId = data->getProperty("id").toString().toStdString();
    const auto name = data->getProperty("name").toString().trim().toStdString();
    const auto roleText = data->getProperty("role").toString();
    const bool updateRole = roleText == "solo" || roleText == "section";
    const auto requestedRole = roleText == "section" ? DoricoRole::section
                                                      : DoricoRole::solo;
    if (chairId.empty() || (name.empty() && !updateRole))
      return;

    safeCallAsync([this, chairId, name, updateRole, requestedRole]() {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository) { chairEditChanged(false); return; }
      EditChairsAction::Edit edit{chairId, std::nullopt, std::nullopt};
      if (!name.empty()) edit.name = name;
      if (updateRole) edit.role = requestedRole;
      undoManager_.perform(std::make_unique<EditChairsAction>(
          *repository, mixer_, std::vector<EditChairsAction::Edit>{edit},
          [this](bool success) { chairEditChanged(success); }));
    });
    return;
  });
  jsRouter_.registerHandler("updateChairRoles",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    auto *data = args.isEmpty() ? nullptr : args[0].getDynamicObject();
    if (!data)
      return;

    const auto roleText = data->getProperty("role").toString();
    const auto ids = data->getProperty("ids");
    auto *idsValue = ids.getArray();
    if (!idsValue || (roleText != "solo" && roleText != "section"))
      return;
    std::vector<std::string> chairIds;
    chairIds.reserve(static_cast<std::size_t>(idsValue->size()));
    for (const auto &id : *idsValue) {
      const auto text = id.toString().toStdString();
      if (!text.empty())
        chairIds.push_back(text);
    }
    const auto requestedRole = roleText == "section" ? DoricoRole::section
                                                      : DoricoRole::solo;

    safeCallAsync([this, chairIds = std::move(chairIds), requestedRole]() {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository) { chairEditChanged(false); return; }
      std::vector<EditChairsAction::Edit> edits;
      for (const auto &chairId : chairIds) {
        edits.push_back({chairId, std::nullopt, requestedRole});
      }
      undoManager_.perform(std::make_unique<EditChairsAction>(
          *repository, mixer_, edits,
          [this](bool success) { chairEditChanged(success); }));
    });
    return;
  });
  jsRouter_.registerHandler("deleteChair", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;
    const auto chairId = args[0].toString().toStdString();
    safeCallAsync([this, chairId]() {
      auto *repository = db_.getLibraryRoutingRepository();
      if (!repository) { chairEditChanged(false); return; }
      undoManager_.perform(std::make_unique<RemoveChairAction>(
          *repository, mixer_, chairId,
          [this](bool success) { chairEditChanged(success); }));
    });
    return;
  });
  jsRouter_.registerHandler("installChairTemplate",
                            [this](const juce::var &) {
                              safeCallAsync(
                                  [this]() { installChairPlaybackTemplate(); });
                              return;
                            });
  jsRouter_.registerHandler(
      "requestLibraries", [this](const juce::var &payload) {
        safeCallAsync([this]() {
          auto libs = db_.listLibraries();
          juce::Array<juce::var> arr;
          for (const auto &lib : libs) {
            auto *obj = new juce::DynamicObject();
            obj->setProperty("id", lib.id);
            obj->setProperty("name", lib.name);
            obj->setProperty("vendor", lib.vendor);
            obj->setProperty("variant", lib.variant);
            arr.add(juce::var(obj));
          }
          broadcastMessage("setLibraryList", juce::var(arr));
          pushLayerCatalog();
        });
        return;
      });
  jsRouter_.registerHandler("openLibraryPatchEditor",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    auto *data = args.isEmpty() ? nullptr : args[0].getDynamicObject();
    if (!data)
      return;

    const auto patchId =
        data->getProperty("patchId").toString().toStdString();
    const auto sourcePatchId =
        data->getProperty("sourcePatchId").toString().toStdString();
    const auto previewId = data->hasProperty("previewId")
      ? data->getProperty("previewId").toString().toStdString() : patchId;
    const bool useSavedState = !data->hasProperty("useSavedState") || (bool)data->getProperty("useSavedState");
    const auto title = data->getProperty("title").toString();
    const int pluginUid = static_cast<int>(data->getProperty("pluginUid"));

    safeCallAsync([this, patchId, previewId, sourcePatchId, useSavedState, title, pluginUid]() {
      std::optional<juce::PluginDescription> description;
      for (const auto &candidate :
           pluginScanner_.getKnownPluginList().getTypes()) {
        if (candidate.uniqueId == pluginUid) {
          description = candidate;
          break;
        }
      }
      if (!description) {
        broadcastMessage("libraryPatchPreviewResult",
                         juce::var("Plug-in is not available"));
        return;
      }

      juce::MemoryBlock initialState;
      std::vector<std::uint8_t> previewState;
      bool foundState = libraryPatchPreviewHost_.captureState(previewId, pluginUid, previewState);
      if (!foundState && useSavedState && !sourcePatchId.empty()) {
        foundState = libraryPatchPreviewHost_.captureState(
            sourcePatchId, pluginUid, previewState);
      }
      if (foundState) {
        if (!previewState.empty())
          initialState.append(previewState.data(), previewState.size());
      } else if (auto *repository = useSavedState ? db_.getLibraryRoutingRepository() : nullptr) {
        auto patch = repository->getPatch(patchId);
        if (!patch && !sourcePatchId.empty())
          patch = repository->getPatch(sourcePatchId);
        if (patch && patch->pluginUid == pluginUid &&
            !patch->pluginState.empty()) {
          initialState.append(patch->pluginState.data(),
                              patch->pluginState.size());
        }
      }

      juce::Component::SafePointer<MainComponent> safeThis(this);
      if (!libraryPatchPreviewHost_.open(
              previewId, title, *description, initialState,
              [safeThis](bool success, const juce::String &error) {
                if (safeThis == nullptr || success)
                  return;
                safeThis->broadcastMessage(
                    "libraryPatchPreviewResult",
                    juce::var(error.isNotEmpty() ? error
                                                 : "Could not open plug-in"));
              })) {
        broadcastMessage("libraryPatchPreviewResult",
                         juce::var("Could not open plug-in"));
      }
    });
    return;
  });
  jsRouter_.registerHandler("discardLibraryPatchPreview",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;
    const auto patchId = args[0].toString().toStdString();
    safeCallAsync(
        [this, patchId]() { libraryPatchPreviewHost_.discard(patchId); });
    return;
  });
  jsRouter_.registerHandler("closeLibraryPatchEditor",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;
    const auto patchId = args[0].toString().toStdString();
    safeCallAsync(
        [this, patchId]() { libraryPatchPreviewHost_.closeEditor(patchId); });
  });
  jsRouter_.registerHandler("showLibraryDraftPreviews", [this](const juce::var &payload) {
    std::vector<std::string> ids;
    if (payload.isArray() && payload.size() > 0 && payload[0].isArray())
      for (const auto &id : *payload[0].getArray()) ids.push_back(id.toString().toStdString());
    std::optional<std::vector<std::string>> retained;
    if (payload.isArray() && payload.size() > 1 && payload[1].isArray()) {
      retained.emplace();
      for (const auto &id : *payload[1].getArray()) retained->push_back(id.toString().toStdString());
    }
    safeCallAsync([this, ids, retained] {
      for (const auto &id : libraryPatchPreviewHost_.consumeChangedPatchIds())
        broadcastMessage("libraryPatchPreviewChanged", juce::String(id));
      libraryPatchPreviewHost_.showOnly(ids);
      if (retained) libraryPatchPreviewHost_.retainOnly(*retained);
    });
  });
  jsRouter_.registerHandler("cloneLibraryPatchPreview", [this](const juce::var &payload) {
    if (!payload.isArray() || payload.size() == 0) return;
    const auto data = payload[0];
    safeCallAsync([this, data] {
      const auto id = data["previewId"].toString().toStdString();
      const int uid = (int)data["pluginUid"];
      std::vector<std::uint8_t> state;
      if (!libraryPatchPreviewHost_.captureState(data["sourcePreviewId"].toString().toStdString(), uid, state)) {
        if (auto *repository = (bool)data["useSavedState"] ? db_.getLibraryRoutingRepository() : nullptr)
          if (auto patch = repository->getPatch(data["sourcePatchId"].toString().toStdString()); patch && patch->pluginUid == uid)
            state = patch->pluginState;
      }
      libraryPatchPreviewHost_.seedState(id, uid, std::move(state));
    });
  });
  jsRouter_.registerHandler("closeLibraryPatchPreviews",
                            [this](const juce::var &) {
    safeCallAsync([this]() { libraryPatchPreviewHost_.closeAll(); });
    return;
  });
  jsRouter_.registerHandler("saveLibrary", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    auto *data = args[0].getDynamicObject();
    if (!data)
      return;

    fiddle::LibraryRow lib;
    lib.id = data->getProperty("id").toString();
    lib.name = data->getProperty("name").toString();
    lib.vendor = data->getProperty("vendor").toString();
    lib.variant = data->getProperty("variant").toString();

    if (libraryPatchPreviewHost_.isLoading()) {
      auto *result = new juce::DynamicObject();
      result->setProperty("id", lib.id); result->setProperty("success", false);
      result->setProperty("message", "A player is still loading. Wait for it to finish, then save again.");
      broadcastMessage("librarySaveResult", juce::var(result));
      return;
    }

    std::vector<fiddle::LibraryInstrumentRow> instruments;
    std::vector<fiddle::LibraryPatchRow> patches;
    for (const auto &id : libraryPatchPreviewHost_.consumeChangedPatchIds())
      broadcastMessage("libraryPatchPreviewChanged", juce::String(id));
    bool usesPatchModel = false;
    if (auto *patchArr = data->getProperty("patches").getArray()) {
      usesPatchModel = true;
      int sortOrder = 0;
      for (const auto &item : *patchArr) {
        if (auto *patchObj = item.getDynamicObject()) {
          fiddle::LibraryPatchRow patch;
          patch.id = patchObj->getProperty("id").toString().toStdString();
          patch.libraryId = lib.id.toStdString();
          patch.position = sortOrder++;
          patch.name = patchObj->getProperty("name").toString().toStdString();
          patch.instrumentEntityId =
              patchObj->getProperty("entityID").toString().toStdString();
          patch.family =
              patchObj->getProperty("family").toString().toStdString();
          patch.character =
              patchObj->getProperty("character").toString().toStdString();
          patch.pluginUid = (int)patchObj->getProperty("vstPlugin");
          patch.expressionMapId =
              patchObj->getProperty("exprMap").toString().toStdString();
          patch.expectedPresetName = patchObj->getProperty("expectedPresetName")
                                         .toString()
                                         .toStdString();
          patch.setupComplete =
              !patchObj->hasProperty("setupComplete") ||
              (bool)patchObj->getProperty("setupComplete");

          // An isolated preview owns the newest explicitly configured state.
          if (libraryPatchPreviewHost_.captureState(
                  patchObj->hasProperty("previewId") ? patchObj->getProperty("previewId").toString().toStdString() : patch.id,
                  patch.pluginUid, patch.pluginState)) {
            // Captured successfully, including the valid empty-state case.
          } else if ((bool)patchObj->getProperty("hasPluginState")) {
            // Loading and saving a library without opening every plug-in must
            // not erase the catalog's stored default state. A newly duplicated
            // patch may explicitly inherit the source patch's saved state.
            if (auto *repository = db_.getLibraryRoutingRepository()) {
              auto existing = repository->getPatch(patch.id);
              if (!existing) {
                const auto sourcePatchId =
                    patchObj->getProperty("sourcePatchId").toString();
                if (sourcePatchId.isNotEmpty())
                  existing =
                      repository->getPatch(sourcePatchId.toStdString());
              }
              if (existing && existing->pluginUid == patch.pluginUid)
                patch.pluginState = existing->pluginState;
            }
          }

          patches.push_back(std::move(patch));
        }
      }
    } else if (auto *instArr =
                   data->getProperty("instruments").getArray()) {
      int sortOrder = 0;
      for (const auto &item : *instArr) {
        if (auto *instObj = item.getDynamicObject()) {
          fiddle::LibraryInstrumentRow inst;
          inst.libraryId = lib.id;
          inst.sortOrder = sortOrder++;
          inst.entityId = instObj->getProperty("entityID").toString();
          inst.name = instObj->getProperty("name").toString();
          inst.family = instObj->getProperty("family").toString();
          inst.category = instObj->getProperty("category").toString();
          auto sizeStr = instObj->getProperty("size").toString();
          inst.isSolo = (sizeStr != "section");
          inst.vstPlugin = instObj->getProperty("vstPlugin").toString();
          inst.exprMap = instObj->getProperty("exprMap").toString();
          inst.note = instObj->getProperty("note").toString();
          auto instNumProp = instObj->getProperty("instanceNums");
          if (instNumProp.isArray()) {
            inst.instanceNums.clear();
            for (const auto &v : *instNumProp.getArray())
              inst.instanceNums.push_back((int)v);
            if (inst.instanceNums.empty())
              inst.instanceNums.push_back(1);
          } else {
            // Legacy: single integer
            int n = instNumProp.isVoid() ? 1 : (int)instNumProp;
            inst.instanceNums = {n};
          }
          // Capture live plugin state from the mixer strip (if linked)
          juce::String stripId = instObj->getProperty("stripId").toString();
          if (stripId.isNotEmpty()) {
            if (auto *strip = mixer_.getStrip(stripId)) {
              inst.pluginUid = strip->pluginUid;
              strip->refreshPluginStateCache();
              inst.pluginState = strip->cachedPluginState();
            }
          }
          // Ensure pluginUid is populated from the vstPlugin string
          // even when no live strip was linked during this save.
          if (inst.pluginUid == 0 && inst.vstPlugin.isNotEmpty())
            inst.pluginUid = inst.vstPlugin.getIntValue();
          instruments.push_back(std::move(inst));
        }
      }
    }

    safeCallAsync(
        [this, lib = std::move(lib), instruments = std::move(instruments),
         patches = std::move(patches), usesPatchModel]() {
          if (usesPatchModel) {
            auto *repository = db_.getLibraryRoutingRepository();
            LibraryCatalogSnapshot target;
            target.id = lib.id.toStdString(); target.name = lib.name.toStdString();
            target.vendor = lib.vendor.toStdString(); target.variant = lib.variant.toStdString();
            target.exists = true; target.patches = patches;
            bool success = false;
            if (repository) {
              auto action = std::make_unique<LibraryCatalogAction>(*repository, std::move(target));
              success = action->isNoOp() || libraryUndoManager_.perform(std::move(action));
            }
            auto *result = new juce::DynamicObject();
            result->setProperty("id", lib.id); result->setProperty("success", success);
            juce::Array<juce::var> savedPatches;
            for (const auto &patch : patches) {
              auto *item = new juce::DynamicObject();
              item->setProperty("id", juce::String(patch.id));
              item->setProperty("hasPluginState", !patch.pluginState.empty());
              item->setProperty("setupComplete", patch.setupComplete);
              savedPatches.add(juce::var(item));
            }
            result->setProperty("patches", juce::var(savedPatches));
            result->setProperty("message", success ? "Library saved" :
              "Could not save the library. A removed patch may still be assigned to a chair. Your draft has been kept.");
            broadcastMessage("librarySaveResult", juce::var(result));
            if (!success) return;
          } else {
            db_.saveLibrary(lib, instruments);
            libraryUndoManager_.clear(); // Legacy callers cannot retain stale catalog history.
          }

          // Broadcast updated library list
          auto libs = db_.listLibraries();
          juce::Array<juce::var> arr;
          for (const auto &l : libs) {
            auto *obj = new juce::DynamicObject();
            obj->setProperty("id", l.id);
            obj->setProperty("name", l.name);
            obj->setProperty("vendor", l.vendor);
            obj->setProperty("variant", l.variant);
            arr.add(juce::var(obj));
          }
          broadcastMessage("setLibraryList", juce::var(arr));
          pushLayerCatalog();
          // Catalog edits can make individual chair layers eligible for a
          // refresh even though the project itself has not changed yet.
          pushMixerState(false);
          pushLibraryLayerStatus();
        });
    return;
  });
  jsRouter_.registerHandler("loadLibrary", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    juce::String libraryId = args[0].toString();
    safeCallAsync([this, libraryId]() {
      auto libs = db_.listLibraries();
      fiddle::LibraryRow foundLib;
      bool found = false;
      for (const auto &lib : libs) {
        if (lib.id == libraryId) {
          foundLib = lib;
          found = true;
          break;
        }
      }
      if (!found)
        return;

      libraryPatchPreviewHost_.closeAll();

      auto instruments = db_.loadLibraryInstruments(libraryId);
      std::vector<fiddle::LibraryPatchRow> patches;
      auto *repository = db_.getLibraryRoutingRepository();
      if (repository)
        patches = repository->listPatches(libraryId.toStdString());

      auto *result = new juce::DynamicObject();
      result->setProperty("id", foundLib.id);
      result->setProperty("name", foundLib.name);
      result->setProperty("vendor", foundLib.vendor);
      result->setProperty("variant", foundLib.variant);

      juce::Array<juce::var> patchArr;
      if (!patches.empty()) {
        for (const auto &patch : patches) {
          libraryPatchPreviewHost_.seedState(patch.id, patch.pluginUid, patch.pluginState);
          auto *patchObj = new juce::DynamicObject();
          patchObj->setProperty("id", juce::String(patch.id));
          patchObj->setProperty("entityID",
                                juce::String(patch.instrumentEntityId));
          patchObj->setProperty("name", juce::String(patch.name));
          patchObj->setProperty("family", juce::String(patch.family));
          patchObj->setProperty("character", juce::String(patch.character));
          patchObj->setProperty("vstPlugin", patch.pluginUid);
          patchObj->setProperty("exprMap",
                                juce::String(patch.expressionMapId));
          patchObj->setProperty("expectedPresetName",
                                juce::String(patch.expectedPresetName));
          patchObj->setProperty("setupComplete", patch.setupComplete);
          patchObj->setProperty("pluginUid", patch.pluginUid);
          patchObj->setProperty("hasPluginState", !patch.pluginState.empty());
          patchObj->setProperty("usageCount",
                                repository->patchUsageCount(patch.id));
          patchObj->setProperty("outOfDateLayerCount",
                                repository->outOfDateLayerCount(patch.id));
          patchArr.add(juce::var(patchObj));
        }
      } else {
        // A best-effort one-time view of pre-redesign library rows. Saving the
        // library assigns stable patch UUIDs and writes the new catalog.
        for (const auto &inst : instruments) {
          auto *patchObj = new juce::DynamicObject();
          patchObj->setProperty("id", juce::Uuid().toString());
          patchObj->setProperty("entityID", inst.entityId);
          patchObj->setProperty("name", inst.name);
          patchObj->setProperty("family", inst.family);
          patchObj->setProperty("character",
                                inst.isSolo ? "solo" : "section");
          patchObj->setProperty("vstPlugin", inst.pluginUid);
          patchObj->setProperty("exprMap", inst.exprMap);
          patchObj->setProperty("pluginUid", inst.pluginUid);
          patchObj->setProperty("hasPluginState",
                                inst.pluginState.getSize() > 0);
          patchObj->setProperty("usageCount", 0);
          patchObj->setProperty("outOfDateLayerCount", 0);
          patchArr.add(juce::var(patchObj));
        }
      }
      result->setProperty("patches", juce::var(patchArr));

      broadcastMessage("setLibraryData", juce::var(result));
    });
    return;
  });
  jsRouter_.registerHandler("updateLayersFromLibraryPatch",
                            [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;
    const auto patchId = args[0].toString().toStdString();

    safeCallAsync([this, patchId]() {
      auto *repository = db_.getLibraryRoutingRepository();
      const auto patch = repository ? repository->getPatch(patchId)
                                    : std::nullopt;
      if (!patch) {
        broadcastMessage("libraryLayersUpdateResult",
                         juce::var("Could not update layers from the patch"));
        return;
      }

      std::vector<LayerRow> layers;
      for (const auto &layer : repository->listLayers())
        if (layer.patchId == patchId) layers.push_back(layer);
      const int updatedCount = (int)layers.size();
      if (updatedCount > 0 && !refreshLayersFromPatch(*patch, layers)) {
        broadcastMessage("libraryLayersUpdateResult",
                         juce::var("Could not update layers; no changes were applied"));
        return;
      }
      if (updatedCount > 0) {
        pushMixerState();
        scheduleStateRebuild();
      }
      const auto noun = updatedCount == 1 ? " layer" : " layers";
      auto *result = new juce::DynamicObject();
      result->setProperty("success", true);
      result->setProperty("patchId", juce::String(patchId));
      result->setProperty("updatedCount", updatedCount);
      result->setProperty("message",
                          "OK: Updated " + juce::String(updatedCount) + noun +
                              " from " + juce::String(patch->name) + ". Undo in the main mixer.");
      broadcastMessage("libraryLayersUpdateResult", juce::var(result));
    });
    return;
  });
  jsRouter_.registerHandler("buildPlaybackTemplate", [this](const juce::var
                                                                &payload) {
    safeCallAsync([this]() {
      std::cerr << "[BuildTemplate] Starting..." << std::endl;

      // 1. Load all libraries and their instruments
      auto libs = db_.listLibraries();
      auto allInstruments = db_.loadAllLibraryInstruments();

      // Build library name lookup
      std::map<juce::String, juce::String> libNames;
      for (const auto &lib : libs)
        libNames[lib.id] = lib.name;

      // 2. Compute union of instruments → EnsembleSlots
      //    Key: (entityId, isSolo) → max instanceNum
      struct SlotKey {
        juce::String entityId;
        bool isSolo;
        bool operator<(const SlotKey &o) const {
          if (entityId != o.entityId)
            return entityId < o.entityId;
          return isSolo < o.isSolo;
        }
      };
      // Track: max instanceNum per unique instrument type, plus count of
      // libraries that have each (entityId, isSolo, instanceNum) tuple.
      // The total count for a slot = number of libraries × max instanceNum.
      struct SlotInfo {
        int maxInstanceNum = 0;
        juce::String name;
        juce::String family;
        juce::String category;
        // Track which libraries have this instrument type
        std::set<juce::String> libraryIds;
      };
      std::map<SlotKey, SlotInfo> slotMap;

      // Also keep a flat lookup for strip initialization later
      // Key: (entityId, isSolo, instanceNum) → list of library instruments
      using StripKey = std::tuple<juce::String, bool, int>;
      std::map<StripKey, std::vector<fiddle::LibraryInstrumentRow>> stripLookup;

      for (const auto &inst : allInstruments) {
        SlotKey key{inst.entityId, inst.isSolo};
        auto &info = slotMap[key];
        info.maxInstanceNum = std::max(info.maxInstanceNum, inst.maxInstance());
        if (info.name.isEmpty()) {
          info.name = inst.name;
          info.family = inst.family;
          info.category = inst.category;
        }
        info.libraryIds.insert(inst.libraryId);

        // Insert one lookup entry per instance number so the strip
        // creation loop can find this row for each Dorico channel.
        for (int iNum : inst.instanceNums) {
          StripKey sk{inst.entityId, inst.isSolo, iNum};
          stripLookup[sk].push_back(inst);
        }
      }

      // Build EnsembleSlots. For each unique (entityId, isSolo), the count
      // is: numLibraries × maxInstanceNum (each library gets its own set of
      // strips for each instance).
      std::vector<MasterInstrumentList::EnsembleSlot> newSlots;

      // First collect unique entityIds to preserve order
      std::map<juce::String, std::pair<int, int>>
          entityCounts; // soloCount, sectionCount
      struct EntityInfo {
        juce::String name, family;
        juce::String musicXMLSoundID;
        int soloCount = 0;
        int sectionCount = 0;
      };
      std::map<juce::String, EntityInfo> entityMap;

      // Look up musicXMLSoundID from the instrument browser
      auto &browserInstruments = instrumentBrowser_.getInstruments();
      std::map<juce::String, juce::String> entityToSoundID;
      for (const auto &bi : browserInstruments)
        entityToSoundID[bi.entityID] = bi.musicXMLSoundID;

      for (const auto &[key, info] : slotMap) {
        auto &ei = entityMap[key.entityId];
        if (ei.name.isEmpty()) {
          ei.name = info.name;
          ei.family = info.family;
          ei.musicXMLSoundID = entityToSoundID[key.entityId];
        }
        // Use max instance count across libraries (not sum).
        // Multiple libraries share the same Dorico channels.
        int count = info.maxInstanceNum;
        if (key.isSolo)
          ei.soloCount += count;
        else
          ei.sectionCount += count;
      }

      for (const auto &[entityId, ei] : entityMap) {
        MasterInstrumentList::EnsembleSlot slot;
        slot.entityID = entityId;
        slot.name = ei.name;
        slot.musicXMLSoundID = ei.musicXMLSoundID;
        slot.family = ei.family;
        slot.soloCount = ei.soloCount;
        slot.sectionCount = ei.sectionCount;
        newSlots.push_back(std::move(slot));
      }

      if (newSlots.empty()) {
        broadcastMessage("buildPlaybackTemplateResult",
                         juce::var("Error: No library instruments found"));
        return;
      }

      std::cerr << "[BuildTemplate] " << newSlots.size()
                << " unique instrument types from " << libs.size()
                << " libraries" << std::endl;

      // 3. Set slots and generate Dorico config
      masterList_.setSlots(std::move(newSlots));
      masterList_.saveToDB(db_);
      // Clear graveyard so stale entries don't get reclaimed into the new
      // template
      db_.clearGraveyard();

      DoricoConfigGenerator generator;
      auto slots = masterList_.getSlots();
      auto assignments = DoricoConfigGenerator::expandSlots(slots);
      int numChannels = masterList_.totalSlotCount();

      auto result = generator.generateAndInstallFiles(assignments, numChannels,
                                                      browserInstruments);

      juce::String msg;
      if (result.wasOk()) {
        msg = "OK: Built template with " +
              juce::String((int)assignments.size()) + " instruments (" +
              juce::String(numChannels) + " channels)";

        // Rebuild channel assignments
        std::vector<ChannelAssignmentRow> newRows;
        newRows.reserve(assignments.size());
        std::map<std::pair<juce::String, bool>, int> instanceCounts;
        for (int idx = 0; idx < (int)assignments.size(); ++idx) {
          const auto &a = assignments[idx];
          auto aKey = std::make_pair(a.entityID, a.isSolo);
          int instanceNum = ++instanceCounts[aKey];
          ChannelAssignmentRow row;
          row.flatIndex = idx;
          row.entityID = a.entityID;
          row.isSolo = a.isSolo;
          row.instanceNum = instanceNum;
          newRows.push_back(row);
        }
        db_.saveChannelAssignments(newRows);
        masterList_.reconcileAssignments(db_);
      } else {
        msg = "Error: " + result.getErrorMessage();
      }

      // 4. Snapshot which libraries are currently active before clearing.
      //    Any library that had at least one active strip is considered
      //    "previously active" and should remain active after rebuild.
      std::set<juce::String> previouslyActiveLibs;
      for (auto *s : mixer_.getAllStrips()) {
        if (s->isActive() && s->library.isNotEmpty())
          previouslyActiveLibs.insert(s->library);
      }

      // Clear existing strips and create fresh ones from the new template
      mixer_.clear();
      mixer_.syncStripsToInstruments(masterList_);

      // 5. Initialize strips from library data.
      // For each Dorico channel, create one strip per library that has
      // that instrument. All strips for the same channel share the same
      // inputPort/inputChannel.
      juce::String mapJson = masterList_.getChannelMapAsJson();
      auto parsed = juce::JSON::parse(mapJson);
      if (auto *mapArr = parsed.getArray()) {
        for (const auto &item : *mapArr) {
          if (auto *obj = item.getDynamicObject()) {
            int port = (int)obj->getProperty("port");
            int ch = (int)obj->getProperty("channel");
            juce::String entityId = obj->getProperty("entityID").toString();
            bool isSolo = (bool)obj->getProperty("isSolo");
            int instNum = (int)obj->getProperty("instanceNum");

            // Find the base strip (created by syncStripsToInstruments)
            auto *baseStrip = [&]() -> MixerStrip * {
              auto strips = mixer_.getAllStrips();
              for (auto *s : strips) {
                if (s->matchesInput(port, ch) && s->library.isEmpty())
                  return s;
              }
              return nullptr;
            }();

            // Get the slot info for this instrument type
            SlotKey sKey{entityId, isSolo};
            auto slotIt = slotMap.find(sKey);
            if (slotIt == slotMap.end())
              continue;
            const auto &sInfo = slotIt->second;

            // Get sorted library IDs for deterministic order
            std::vector<juce::String> sortedLibIds(sInfo.libraryIds.begin(),
                                                   sInfo.libraryIds.end());
            std::sort(sortedLibIds.begin(), sortedLibIds.end());

            // Helper lambda to set up a strip from a library instrument
            auto initStrip = [&](MixerStrip *strip,
                                 const fiddle::LibraryInstrumentRow &inst,
                                 const juce::String &libId) {
              strip->library = libNames[libId];
              // Preserve activation for previously-active libraries;
              // new libraries default to inactive.
              strip->setActive(previouslyActiveLibs.count(libNames[libId]) > 0);

              if (inst.exprMap.isNotEmpty()) {
                auto xmapData = xmapLibrary_.load(inst.exprMap.toStdString());
                if (xmapData)
                  strip->setExpressionMap(xmapData);
              }

              // Resolve effective plugin UID: pluginUid is set when a
              // live strip was linked during save; vstPlugin (string)
              // always holds the dropdown selection.
              int effectiveUid = inst.pluginUid;
              if (effectiveUid == 0 && inst.vstPlugin.isNotEmpty())
                effectiveUid = inst.vstPlugin.getIntValue();

              restoreStripPlugin(*strip, effectiveUid, inst.pluginState);
            };

            // Create one strip per library for this channel
            for (int libIdx = 0; libIdx < (int)sortedLibIds.size(); ++libIdx) {
              const juce::String &libId = sortedLibIds[libIdx];

              // Find the matching library instrument
              StripKey lookupKey{entityId, isSolo, instNum};
              auto lookupIt = stripLookup.find(lookupKey);
              if (lookupIt == stripLookup.end())
                continue;

              const fiddle::LibraryInstrumentRow *matchedInst = nullptr;
              for (const auto &li : lookupIt->second) {
                if (li.libraryId == libId) {
                  matchedInst = &li;
                  break;
                }
              }
              if (!matchedInst)
                continue;

              if (libIdx == 0 && baseStrip) {
                // First library uses the base strip
                initStrip(baseStrip, *matchedInst, libId);
              } else {
                // Additional libraries: create a new strip sharing
                // the same input
                auto newStrip = std::make_unique<MixerStrip>();
                newStrip->id = juce::Uuid().toString();
                newStrip->setInputAssignment(port, ch);
                newStrip->family = baseStrip ? baseStrip->family : "";
                newStrip->isSolo = isSolo;
                auto *rawPtr = newStrip.get();

                // Insert right after the base strip (or at end)
                int insertIdx = -1;
                if (baseStrip)
                  insertIdx = mixer_.stripIndex(baseStrip->id) + 1;
                else
                  insertIdx = mixer_.size();
                mixer_.insertStripAt(std::move(newStrip), insertIdx);

                initStrip(rawPtr, *matchedInst, libId);
              }
            }
          }
        }
      }

      // 6. Push state to UIs
      saveAllStripsToDB();

      juce::String mapJson2 = masterList_.getChannelMapAsJson();
      broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson2));

      pushMixerState();

      // Push updated setup data for the Setup pane
      {
        juce::String selJson = masterList_.getSlotsAsJson();
        broadcastMessage("setSelectedInstruments",
                         juce::JSON::fromString(selJson));
      }

      broadcastMessage("buildPlaybackTemplateResult", juce::var(msg));
      std::cerr << "[BuildTemplate] " << msg << std::endl;

      // Update fingerprint so the button disables until libraries change
      lastInstalledFingerprint_ = computeChairFingerprint();
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("deleteLibrary", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    juce::String libraryId = args[0].toString();
    safeCallAsync([this, libraryId]() {
      if (auto *repository = db_.getLibraryRoutingRepository()) {
        const auto patches =
            repository->listPatches(libraryId.toStdString());
        int referencedPatchCount = 0;
        int layerReferenceCount = 0;
        for (const auto &patch : patches) {
          const int usageCount = repository->patchUsageCount(patch.id);
          if (usageCount > 0) {
            ++referencedPatchCount;
            layerReferenceCount += usageCount;
          }
        }
        if (referencedPatchCount > 0) {
          broadcastMessage(
              "setLibraryDeleteResult",
              juce::var("Cannot delete this library: " +
                        juce::String(referencedPatchCount) +
                        (referencedPatchCount == 1 ? " patch is" :
                                                     " patches are") +
                        " used by " + juce::String(layerReferenceCount) +
                        (layerReferenceCount == 1 ? " layer" : " layers")));
          return;
        }
      }
      auto *repository = db_.getLibraryRoutingRepository();
      LibraryCatalogSnapshot target;
      target.id = libraryId.toStdString();
      if (!repository || !libraryUndoManager_.perform(
          std::make_unique<LibraryCatalogAction>(*repository, std::move(target)))) {
        broadcastMessage("setLibraryDeleteResult", juce::var("Could not delete the library. Nothing was changed."));
        return;
      }

      auto libs = db_.listLibraries();
      juce::Array<juce::var> arr;
      for (const auto &l : libs) {
        auto *obj = new juce::DynamicObject();
        obj->setProperty("id", l.id);
        obj->setProperty("name", l.name);
        obj->setProperty("vendor", l.vendor);
        obj->setProperty("variant", l.variant);
        arr.add(juce::var(obj));
      }
      broadcastMessage("setLibraryList", juce::var(arr));
      pushLayerCatalog();
    });
    return;
  });
  jsRouter_.registerHandler("undo", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.undo()) {
        saveAllStripsToDB(false);
        pushMixerState();
        pushGroupBusState();
        pushMasterAudioState();
        scheduleStateRebuild();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          broadcastMessage("setDirtyState", false);
          pushConfigStatus();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("redo", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.redo()) {
        saveAllStripsToDB(false);
        pushMixerState();
        pushGroupBusState();
        pushMasterAudioState();
        scheduleStateRebuild();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          broadcastMessage("setDirtyState", false);
          pushConfigStatus();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler(
      "requestMixerState", [this](const juce::var &payload) {
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();

        safeCallAsync([this]() {
          pushMixerState(false);
          pushGroupBusState();
          pushMasterAudioState();
          pushMixerMeters();
        });
        return;
      });
  jsRouter_.registerHandler(
      "getAvailableInputs", [this](const juce::var &payload) {
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();

        safeCallAsync([this]() {
          juce::String json = masterList_.getChannelMapAsJson();
          broadcastMessage("setAvailableInputs", juce::JSON::parse(json));
        });
        return;
      });
  jsRouter_.registerHandler(
      "setPlaybackDelay", [this](const juce::var &payload) {
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();

        if (args.size() >= 1) {
          int ms = static_cast<int>(args[0]);
          safeCallAsync([this, ms]() {
            auto settings = mixer_.projectSettings();
            settings.playbackDelayMs = std::clamp(ms, 0, 5000);
            if (!undoManager_.perform(std::make_unique<SetProjectSettingsAction>(
                    mixer_, settings, "Change playback delay", "playback-delay")))
              return;
            saveAllStripsToDB(false);
            pushMixerState();
            scheduleStateRebuild();
            pushConfigStatus();
            pushLogMessage("<b>[Mixer]</b> Playback delay set to " +
                           juce::String(ms) + " ms");
          });
        }
        return;
      });
  jsRouter_.registerHandler(
      "getPlaybackDelay", [this](const juce::var &payload) {
        juce::Array<juce::var> args;
        if (payload.isArray())
          args = *payload.getArray();

        safeCallAsync([this]() {
          int ms = mixer_.getPlaybackDelayMs();
          broadcastMessage("setPlaybackDelay", juce::var(ms));
        });
        return;
      });
  jsRouter_.registerHandler("requestBranches",
                            [this](const juce::var &payload) {
                              safeCallAsync([this]() { pushBranches(); });
                              return;
                            });
  jsRouter_.registerHandler(
      "requestCurrentBranch", [this](const juce::var &payload) {
        safeCallAsync([this]() {
          if (versionStore_) {
            auto b = versionStore_->getStorage().getBranch(currentBranchId_);
            if (b) {
              broadcastMessage("setCurrentBranch",
                               juce::var(juce::String(currentBranchId_)));
              return;
            }
          }
          broadcastMessage("setCurrentBranch",
                           juce::var(juce::String("default")));
        });
        return;
      });
  jsRouter_.registerHandler("openHistoryWindow", [this](
                                                     const juce::var &payload) {
    safeCallAsync([this]() {
      if (!historyWindow_ && versionStore_) {
        historyWindow_ =
            std::make_unique<HistoryWindow>(webViewBridge_.createWebOptions());
        historyWindowLoaded_ = false;
        juce::String root =
            juce::WebBrowserComponent::getResourceProviderRoot();
        historyWindow_->getWebView().goToURL(root + "index.html?view=history");
      }
      if (historyWindow_) {
        historyWindow_->setVisible(true);
        historyWindow_->toFront(true);
        // pushDagHistory() will be called once the window signals ready
      }
    });
    return;
  });
  jsRouter_.registerHandler("showLibraryManagerWindow", [this](
                                                           const juce::var &) {
    safeCallAsync([this]() { showLibraryManagerWindow(); });
    return;
  });
  jsRouter_.registerHandler("dismissConnectionWarning", [this](
                                                          const juce::var &) {
    safeCallAsync([this]() {
      connectionWarning_.clear();
      broadcastMessage("setConnectionWarning", juce::String());
    });
    return;
  });
  jsRouter_.registerHandler("requestDagHistory",
                            [this](const juce::var &payload) {
                              safeCallAsync([this]() { pushDagHistory(); });
                              return;
                            });
  jsRouter_.registerHandler("createBranch", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string branchName = args[0].toString().toStdString();
      // Optional second arg: a specific version UUID to branch from.
      // If omitted, save the working state on a named branch rooted at the
      // loaded version, without first changing the old branch's head.
      std::string fromVersionId =
          (args.size() > 1) ? args[1].toString().toStdString() : "";
      safeCallAsync([this, branchName, fromVersionId]() {
        if (fromVersionId.empty()) {
          saveConfig(branchName);
          return;
        }
        if (branchName.empty() ||
            versionStore_->getStorage().branchNameExists(branchName)) {
          pushLogMessage("<b>[Branch]</b> Name is empty or already in use", true);
          return;
        }
        auto newBranchId =
            versionStore_->createBranch(branchName, fromVersionId);
        if (newBranchId.empty()) {
          std::cerr << "[createBranch] Failed to create branch from version: "
                    << fromVersionId << std::endl;
          return;
        }
        if (!checkoutBranchById(newBranchId))
          std::cerr << "[createBranch] Failed to check out new branch: "
                    << newBranchId << std::endl;
      });
    }
    return;
  });
  jsRouter_.registerHandler("checkoutBranch", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string branchId = args[0].toString().toStdString();
      safeCallAsync([this, branchId]() {
        if (!checkoutBranchById(branchId)) {
          std::cerr << "[checkoutBranch] Failed to find branch id: " << branchId
                    << std::endl;
        } else {
          std::cerr << "[checkoutBranch] Checked out branch id: " << branchId
                    << std::endl;
        }
      });
    }
    return;
  });
  jsRouter_.registerHandler("checkoutVersion", [this](
                                                   const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string versionHash = args[0].toString().toStdString();
      safeCallAsync([this, versionHash]() {
        auto verOpt = versionStore_->getVersion(versionHash);
        if (!verOpt) {
          std::cerr << "[checkoutVersion] Version not found: " << versionHash
                    << std::endl;
          return;
        }
        if (!loadStoredVersion(versionHash, verOpt->branchId, true)) {
          std::cerr << "[checkoutVersion] State not found for: " << versionHash
                    << std::endl;
        }
      });
    }
    return;
  });
  jsRouter_.registerHandler("mergeBranch", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() >= 2 && versionStore_) {
      std::string sourceBranchId = args[0].toString().toStdString();
      std::string targetBranchId = args[1].toString().toStdString();
      safeCallAsync([this, sourceBranchId, targetBranchId]() {
        using MR = fiddle::versioning::MergeResult;
        auto result = versionStore_->merge(sourceBranchId, targetBranchId);

        auto *obj = new juce::DynamicObject();
        obj->setProperty("ok", result.kind != MR::Error);
        obj->setProperty("kind", result.kind == MR::FastForward ? "fast-forward"
                                 : result.kind == MR::ThreeWay  ? "three-way"
                                                                : "error");
        obj->setProperty("error", juce::String(result.error));

        if (result.kind != MR::Error) {
          // If the current branch was the merge target, reload the mixer with
          // the merged state.
          if (targetBranchId == currentBranchId_)
            loadStoredVersion(result.newHeadId, targetBranchId, true);
          pushBranches();
          pushDagHistory();
        }
        broadcastMessage("mergeResult", juce::var(obj));
      });
    }
    return;
  });
  jsRouter_.registerHandler("deleteVersion", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    std::cerr << "[IPC] deleteVersion: args.size=" << args.size()
              << " versionStore=" << (versionStore_ ? "ok" : "null")
              << std::endl;
    if (args.size() > 0 && versionStore_) {
      std::string versionHash = args[0].toString().toStdString();
      std::cerr << "[IPC] deleteVersion hash=" << versionHash << std::endl;
      safeCallAsync([this, versionHash]() {
        auto result = versionStore_->deleteVersion(versionHash);
        auto *obj = new juce::DynamicObject();
        obj->setProperty("ok", result.success);
        obj->setProperty("error", juce::String(result.error));
        if (result.success) {
          // If the deletion caused the owning branch to be auto-deleted and
          // that branch was the one we're currently on, switch to a survivor.
          if (!result.deletedBranchId.empty() &&
              result.deletedBranchId == currentBranchId_) {
            auto allBranches = versionStore_->getStorage().listBranches();
            if (!allBranches.empty()) {
              const auto survivor = std::get<0>(allBranches[0]);
              std::cerr << "[IPC] deleteVersion: branch auto-deleted; "
                           "switched to "
                        << survivor << std::endl;
              checkoutBranchById(survivor);
            }
          }
          pushBranches();
          pushDagHistory();
        }
        broadcastMessage("deleteResult", juce::var(obj));
      });
    }
    return;
  });
  jsRouter_.registerHandler("saveConfig", [this](const juce::var &payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { saveConfig(); });
    return;
  });
}

void MainComponent::chairEditChanged(bool success) {
  if (!success) {
    broadcastMessage("chairOperationResult", juce::var("Could not change the chair; no edit was applied"));
    return;
  }
  // Also run on Undo/Redo. Preserve live layer settings rather than reloading
  // possibly stale catalog/session rows after a metadata-only chair edit.
  saveAllStripsToDB(false);
  pushChairState();
  pushMixerState();
  broadcastTemplateDirty();
  scheduleStateRebuild();
}

void MainComponent::pushChairState() {
  juce::Array<juce::var> result;
  if (auto *repository = db_.getLibraryRoutingRepository()) {
    const auto &scoreOrder = instrumentBrowser_.getScoreOrder();
    for (const auto &chair : repository->listChairs()) {
      auto *obj = new juce::DynamicObject();
      obj->setProperty("id", juce::String(chair.id));
      obj->setProperty("entityID", juce::String(chair.instrumentEntityId));
      obj->setProperty("name", juce::String(chair.name));
      obj->setProperty("family", juce::String(chair.family));
      obj->setProperty("role",
                       chair.role == DoricoRole::solo ? "solo" : "section");
      obj->setProperty("ordinal", chair.ordinal);
      obj->setProperty("displayOrder", chair.displayOrder);
      const auto order =
          scoreOrder.find(juce::String(chair.instrumentEntityId));
      obj->setProperty("scoreOrder",
                       order != scoreOrder.end() ? order->second : 99999);
      obj->setProperty("flatIndex", chair.flatIndex);
      obj->setProperty("port", chair.flatIndex / 16);
      obj->setProperty("channel", chair.flatIndex % 16);
      obj->setProperty(
          "layerCount",
          static_cast<int>(repository->listLayers(chair.id).size()));
      result.add(juce::var(obj));
    }
  }
  broadcastMessage("setChairState", juce::var(result));
}

void MainComponent::pushLayerCatalog() {
  juce::Array<juce::var> result;
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository) {
    broadcastMessage("setLayerCatalog", juce::var(result));
    return;
  }

  std::map<std::string, juce::String> libraryNames;
  for (const auto &library : db_.listLibraries())
    libraryNames[library.id.toStdString()] = library.name;

  for (const auto &patch : repository->listPatches()) {
    auto *obj = new juce::DynamicObject();
    obj->setProperty("id", juce::String(patch.id));
    obj->setProperty("libraryId", juce::String(patch.libraryId));
    obj->setProperty("libraryName", libraryNames[patch.libraryId]);
    obj->setProperty("name", juce::String(patch.name));
    obj->setProperty("entityID", juce::String(patch.instrumentEntityId));
    obj->setProperty("family", juce::String(patch.family));
    obj->setProperty("character", juce::String(patch.character));
    obj->setProperty("pluginUid", patch.pluginUid);
    obj->setProperty("expressionMapId",
                     juce::String(patch.expressionMapId));
    result.add(juce::var(obj));
  }
  broadcastMessage("setLayerCatalog", juce::var(result));
}

bool MainComponent::refreshLayersFromPatch(const LibraryPatchRow &patch,
                                          const std::vector<LayerRow> &layers) {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository || layers.empty()) return false;
  LayerLibrarySetup setup;
  setup.row.patchName = patch.name;
  setup.row.libraryId = patch.libraryId;
  for (const auto &library : db_.listLibraries())
    if (library.id.toStdString() == patch.libraryId)
      setup.row.libraryName = library.name.toStdString();
  setup.row.pluginUid = patch.pluginUid;
  setup.row.pluginState = patch.pluginState;
  setup.row.expressionMapId = patch.expressionMapId;
  setup.row.sourcePatchRevision = patch.revision;
  // Keep a missing player's identity/state even if it isn't in today's scan.
  setup.instrument.description.uniqueId = patch.pluginUid;
  for (const auto &desc : pluginScanner_.getKnownPluginList().getTypes())
    if (desc.uniqueId == patch.pluginUid) setup.instrument.description = desc;
  if (!patch.pluginState.empty())
    setup.instrument.state.append(patch.pluginState.data(), patch.pluginState.size());
  if (!patch.expressionMapId.empty()) setup.expressionMap = xmapLibrary_.load(patch.expressionMapId);
  std::vector<LayerLibrarySetup> targets;
  for (const auto &layer : layers) {
    auto target = setup;
    target.row.id = layer.id;
    target.row.chairId = layer.chairId;
    target.row.patchId = layer.patchId;
    // Bypass is a layer control, not a library default.
    if (auto *strip = mixer_.getStrip(juce::String(layer.id)))
      target.instrument.bypassed = strip->isPluginBypassed();
    targets.push_back(std::move(target));
  }
  const auto description = layers.size() == 1
      ? "Refresh layer '" + juce::String(patch.name) + "' from library"
      : "Update " + juce::String((int)layers.size()) + " layers from '" + juce::String(patch.name) + "'";
  return undoManager_.perform(std::make_unique<LayerLibrarySetupAction>(
      *repository, mixer_, std::move(targets), description,
      [safeThis = juce::Component::SafePointer<MainComponent>(this)](const juce::String &id) {
        if (safeThis) safeThis->layerLibrarySetupSettled(id);
      }));
}

void MainComponent::layerLibrarySetupSettled(const juce::String &stripId) {
  if (auto *strip = mixer_.getStrip(stripId)) {
    (void)strip->consumePluginChangeNotification();
    (void)strip->consumePluginExplicitEditNotification();
    (void)strip->consumePluginNonParameterStateChangeNotification();
    pluginFingerprints_[stripId] = {strip->pluginUid, strip->pluginParameterFingerprint()};
    pluginStateSettleUntilMs_[stripId] = juce::Time::getMillisecondCounter() + kPluginStateSettleMs;
  }
  // UndoManager advances its saved identity after the action returns. Defer
  // persistence/UI so an async completion cannot dirty a restored save point.
  safeCallAsync([this] {
    saveAllStripsToDB(false);
    const bool dirty = !undoManager_.isAtSavePoint();
    pushMixerState(dirty);
    if (!dirty) {
      stateManager_.clearDirty();
      broadcastMessage("setDirtyState", false);
      pushConfigStatus();
    }
    scheduleStateRebuild();
  });
}

bool MainComponent::instantiateLayer(const LayerRow &layer,
                                     const ChairRow &chair,
                                     const LibraryPatchRow *patch,
                                     const juce::String &libraryName) {
  MixerStrip *strip = mixer_.getStrip(juce::String(layer.id));
  const bool isNew = strip == nullptr;
  if (isNew) {
    auto newStrip = std::make_unique<MixerStrip>();
    newStrip->id = juce::String(layer.id);
    strip = newStrip.get();
    mixer_.insertStripAt(std::move(newStrip), mixer_.size());
  }
  if (!strip)
    return false;

  if (isNew)
    setupStripPluginSlot(*strip);

  strip->chairId = juce::String(chair.id);
  strip->patchId = juce::String(layer.patchId);
  strip->layerName = juce::String(
      !layer.patchName.empty()
          ? layer.patchName
          : (patch ? patch->name : std::string{"Missing catalog patch"}));
  strip->library = libraryName;
  strip->missingPatchReference = patch == nullptr;
  strip->family = juce::String(chair.family);
  strip->isSolo = chair.role == DoricoRole::solo;
  strip->setInputAssignment(chair.flatIndex / 16, chair.flatIndex % 16);
  strip->setActive(layer.active);
  strip->setMuted(layer.muted);
  strip->setSoloed(layer.soloed);
  strip->setGainDb(layer.gainDb);

  const auto currentMapId =
      strip->expressionMap ? strip->expressionMap->entityID : std::string{};
  if (currentMapId != layer.expressionMapId) {
    strip->setExpressionMap(nullptr);
    if (!layer.expressionMapId.empty()) {
      if (auto map = xmapLibrary_.load(layer.expressionMapId))
        strip->setExpressionMap(std::move(map));
    }
  }

  if (isNew || strip->pluginUid != layer.pluginUid) {
    if (layer.pluginUid == 0) {
      strip->unloadPlugin();
      strip->pluginUid = 0;
    } else {
      juce::MemoryBlock pluginState;
      if (!layer.pluginState.empty())
        pluginState.append(layer.pluginState.data(), layer.pluginState.size());
      restoreStripPlugin(*strip, layer.pluginUid, pluginState);
    }
  }
  return true;
}

void MainComponent::syncMixerToLayers() {
  auto *repository = db_.getLibraryRoutingRepository();
  if (!repository)
    return;
  const auto chairs = repository->listChairs();

  std::map<std::string, juce::String> libraryNames;
  for (const auto &library : db_.listLibraries())
    libraryNames[library.id.toStdString()] = library.name;

  std::set<std::string> desiredLayerIds;
  for (const auto &layer : repository->listLayers())
    desiredLayerIds.insert(layer.id);

  std::vector<juce::String> obsoleteStripIds;
  for (auto *strip : mixer_.getAllStrips()) {
    if (desiredLayerIds.count(strip->id.toStdString()) == 0)
      obsoleteStripIds.push_back(strip->id);
  }
  for (const auto &stripId : obsoleteStripIds)
    mixer_.removeStrip(stripId);

  for (const auto &chair : chairs) {
    for (const auto &layer : repository->listLayers(chair.id)) {
      const auto patch = repository->getPatch(layer.patchId);
      const auto libraryName = !layer.libraryName.empty()
                                   ? juce::String(layer.libraryName)
                                   : patch ? libraryNames[patch->libraryId]
                                           : juce::String("Missing library");
      instantiateLayer(layer, chair, patch ? &*patch : nullptr, libraryName);
    }
  }
}

void MainComponent::installChairPlaybackTemplate() {
  auto *repository = db_.getLibraryRoutingRepository();
  const auto chairs = repository ? repository->listChairs()
                                  : std::vector<ChairRow>{};
  if (chairs.empty()) {
    broadcastMessage("chairTemplateResult",
                     juce::var("Add at least one chair before installing"));
    return;
  }

  const auto &browserInstruments = instrumentBrowser_.getInstruments();
  std::map<juce::String, const BrowsableInstrument *> browserByEntity;
  for (const auto &instrument : browserInstruments)
    browserByEntity[instrument.entityID] = &instrument;

  std::vector<InstrumentAssignment> assignments;
  assignments.reserve(chairs.size());
  int program = 1;
  int bankMSB = 0;
  int bankLSB = 0;
  int maxFlatIndex = 0;
  for (const auto &chair : chairs) {
    const auto browserIt =
        browserByEntity.find(juce::String(chair.instrumentEntityId));
    if (browserIt == browserByEntity.end()) {
      broadcastMessage(
          "chairTemplateResult",
          juce::var("Dorico instrument data is unavailable for " +
                    juce::String(chair.name)));
      return;
    }

    InstrumentAssignment assignment;
    assignment.entityID = juce::String(chair.instrumentEntityId);
    assignment.name = juce::String(chair.name);
    assignment.musicXMLSoundID = browserIt->second->musicXMLSoundID;
    assignment.category = chair.family.empty()
                              ? browserIt->second->family
                              : juce::String(chair.family);
    assignment.program = program;
    assignment.bankMSB = bankMSB;
    assignment.bankLSB = bankLSB;
    assignment.isSolo = chair.role == DoricoRole::solo;
    assignment.flatIndex = chair.flatIndex;
    assignments.push_back(std::move(assignment));
    maxFlatIndex = std::max(maxFlatIndex, chair.flatIndex);

    if (++program > 128) {
      program = 1;
      if (++bankLSB > 127) {
        bankLSB = 0;
        ++bankMSB;
      }
    }
  }

  DoricoConfigGenerator generator;
  const auto result = generator.generateAndInstallFiles(
      assignments, maxFlatIndex + 1, browserInstruments);
  if (result.failed()) {
    broadcastMessage("chairTemplateResult",
                     juce::var("Error: " + result.getErrorMessage()));
    return;
  }

  // Keep the channel-map infrastructure synchronized as a derived view of
  // chairs for Timeline labels and Dorico compatibility metadata.
  std::vector<MasterInstrumentList::EnsembleSlot> slots;
  std::map<juce::String, std::size_t> slotByEntity;
  for (const auto &chair : chairs) {
    const auto entityId = juce::String(chair.instrumentEntityId);
    auto slotIt = slotByEntity.find(entityId);
    if (slotIt == slotByEntity.end()) {
      const auto *browserInstrument = browserByEntity[entityId];
      MasterInstrumentList::EnsembleSlot slot;
      slot.entityID = entityId;
      slot.name = browserInstrument->name;
      slot.musicXMLSoundID = browserInstrument->musicXMLSoundID;
      slot.family = chair.family.empty() ? browserInstrument->family
                                         : juce::String(chair.family);
      slots.push_back(std::move(slot));
      slotIt = slotByEntity.emplace(entityId, slots.size() - 1).first;
    }
    auto &slot = slots[slotIt->second];
    if (chair.role == DoricoRole::solo)
      ++slot.soloCount;
    else
      ++slot.sectionCount;
  }
  masterList_.setSlots(std::move(slots));
  masterList_.saveToDB(db_);

  std::vector<ChannelAssignmentRow> channelRows;
  std::map<std::pair<juce::String, bool>, int> instanceCounts;
  for (const auto &chair : chairs) {
    const bool isSolo = chair.role == DoricoRole::solo;
    const auto entityId = juce::String(chair.instrumentEntityId);
    channelRows.push_back({chair.flatIndex, entityId, isSolo,
                           ++instanceCounts[{entityId, isSolo}]});
  }
  db_.saveChannelAssignments(channelRows);
  masterList_.reconcileAssignments(db_);
  syncMixerToLayers();
  saveAllStripsToDB();

  broadcastMessage(
      "setInstrumentMap",
      juce::JSON::fromString(masterList_.getChannelMapAsJson()));
  broadcastMessage(
      "setSelectedInstruments",
      juce::JSON::fromString(masterList_.getSlotsAsJson()));
  pushMixerState();

  lastInstalledFingerprint_ = computeChairFingerprint();
  broadcastTemplateDirty();
  broadcastMessage("chairTemplateResult",
                   juce::var("Installed playback template with " +
                             juce::String((int)chairs.size()) + " chairs"));
}

juce::String MainComponent::computeChairFingerprint() {
  auto *repository = db_.getLibraryRoutingRepository();
  const auto chairs = repository ? repository->listChairs()
                                  : std::vector<ChairRow>{};
  juce::String data;
  for (const auto &chair : chairs) {
    data += juce::String(chair.id) + "|" +
            juce::String(chair.instrumentEntityId) + "|" +
            juce::String(chair.name) + "|" + juce::String(chair.family) +
            "|" + (chair.role == DoricoRole::solo ? "S" : "T") + "|" +
            juce::String(chair.ordinal) + "|" +
            juce::String(chair.displayOrder) + "|" +
            juce::String(chair.flatIndex) + "\n";
  }
  return juce::String(data.hashCode64());
}

void MainComponent::broadcastTemplateDirty() {
  juce::String current = computeChairFingerprint();
  bool dirty = current != lastInstalledFingerprint_;
  broadcastMessage("setTemplateDirty", juce::var(dirty));
}

} // namespace fiddle
