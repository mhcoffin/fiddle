#include "MainComponent.h"
#include "DoricoConfigGenerator.h"
#include "ExpressionMapParser.h"
#include "FiddleConfig.h"

#include "midi_event.pb.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <google/protobuf/text_format.h>
#include <memory>
#include <string>

namespace fiddle {



MainComponent::MainComponent()
    : webViewBridge_(jsRouter_, [this](std::function<void()> f) { safeCallAsync(f); }) {
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
          juce::String call = "setPluginList('" + WebViewBridge::escapeForJS(json) + "')";
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
        [this](const juce::String &url) { return webViewBridge_.getResource(url); },
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
  jsTestBridge_ = std::make_unique<JsTestBridge>(webViewBridge_.getMainWebComponent(), 9223);
}

void MainComponent::addInitMessage(const juce::String &msg) {
  initMessages_.add(msg);
  repaint();
}

void MainComponent::initializeApp() {
  initExpressionMaps();
}

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
    auto exeFile = juce::File::getSpecialLocation(
        juce::File::currentExecutableFile);
    auto bundledDir = exeFile.getParentDirectory()
                          .getChildFile("scripts")
                          .getChildFile("plugins");
    if (bundledDir.isDirectory()) {
      luaCatalog_.scanDirectory(bundledDir.getFullPathName().toStdString());
    }
    // Scan user plugins directory
    luaCatalog_.scanDefaultDirectory();
    std::cerr << "[Init] Lua plugin system ready — "
              << luaCatalog_.plugins().size() << " plugins found"
              << std::endl;
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
        auto bundleDir = juce::File::getSpecialLocation(
            juce::File::currentExecutableFile).getParentDirectory();
        auto transFile = bundleDir.getChildFile("resources/hmm/cpe_transitions.bin");
        if (!transFile.existsAsFile()) {
          // Also check project resources dir (for dev builds).
          // From MacOS dir, project root is 6 levels up:
          // MacOS → Contents → .app → Debug → FiddleServer_artefacts → build → project
          auto projectRoot = bundleDir;
          for (int i = 0; i < 6; ++i)
            projectRoot = projectRoot.getParentDirectory();
          auto projectResources = projectRoot.getChildFile("resources/hmm/cpe_transitions.bin");
          if (projectResources.existsAsFile())
            transFile = projectResources;
        }

        if (transFile.existsAsFile()) {
          juce::MemoryBlock data;
          transFile.loadFileAsData(data);
          if (harmonicService_.loadTransitionMatrix(data.getData(), data.getSize()))
            std::cerr << "[Init] Loaded transition matrix: " << transFile.getFullPathName() << std::endl;
          else
            std::cerr << "[Init] WARNING: Failed to parse transition matrix" << std::endl;

          // Load degree priors from the same directory.
          auto priorsFile = transFile.getSiblingFile("degree_priors.bin");
          if (priorsFile.existsAsFile()) {
            juce::MemoryBlock priorsData;
            priorsFile.loadFileAsData(priorsData);
            if (harmonicService_.loadDegreePriors(priorsData.getData(), priorsData.getSize()))
              std::cerr << "[Init] Loaded degree priors: " << priorsFile.getFullPathName() << std::endl;
            else
              std::cerr << "[Init] WARNING: Failed to parse degree priors" << std::endl;
          }
        } else {
          std::cerr << "[Init] No transition matrix found; using emission-only mode" << std::endl;
        }
      }

      mixer_.setHarmonicService(&harmonicService_);
      harmonicService_.start();
      std::cerr << "[Init] HarmonicAnalysisService started" << std::endl;

      // Wire metronome tempo tracker callback.
      // When the tracker detects a tempo change from the click track,
      // update the global BPM and broadcast to the UI.
      metronomeTracker_.onTempoChanged =
          [this](double bpm, int tsNum, int tsDen, uint64_t samplePos) {
        currentBpm_.store(bpm, std::memory_order_relaxed);
        harmonicService_.setBpm(bpm);

        juce::DynamicObject::Ptr tempoObj = new juce::DynamicObject();
        tempoObj->setProperty("bpm", bpm);
        tempoObj->setProperty("timeSigNumerator", tsNum);
        tempoObj->setProperty("timeSigDenominator", tsDen);
        tempoObj->setProperty("samplePosition", (juce::int64)samplePos);
        tempoObj->setProperty("source", juce::String("metronome"));
        juce::var tempoVar(tempoObj.get());
        safeCallAsync([this, tempoVar]() {
          broadcastMessage("setTempo", tempoVar);
        });
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
            endVel =
                (int)it->second.points(it->second.points_size() - 1).value();
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
                  // Graceful stop: ramp CC1 (expression) to 0 over 300ms,
                  // then send note-OFFs, then All Sound Off.
                  auto active = noteTracker.getActiveNotes();
                  std::map<int, uint8_t> channelCC1;
                  for (const auto &n : active) {
                    int ch = (int)n.channel();
                    if (channelCC1.find(ch) == channelCC1.end())
                      channelCC1[ch] = noteTracker.getCC(ch, 1);
                  }
                  mixer_.gracefulStop(active, 300.0, channelCC1);
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
        // ── Transport State Machine ───────────────────────────────────
        if (event.has_transport()) {
          if (event.transport().type() ==
              fiddle::MidiEvent_TransportEvent_Type_START) {
            bool expected = false;
            if (!isTransportStarted_.compare_exchange_strong(expected, true)) {
              return; // Already started, ignore redundant event
            }
          } else if (event.transport().type() ==
                     fiddle::MidiEvent_TransportEvent_Type_STOP) {
            bool expected = true;
            if (!isTransportStarted_.compare_exchange_strong(expected, false)) {
              return; // Already stopped, ignore redundant event
            }
          }
        }

        // Force a log to the UI so we can see the flow
        pushLogMessage("<b>[Server]</b> Received Event Case: " +
                       juce::String((int)event.event_case()) +
                       " Ch: " + juce::String(event.channel()));

        // ── Metronome interception ────────────────────────────────────
        // The reserved metronome channel is port 0, channel 0 (0-based).
        // In the protobuf, channel is 1-based, so channel()==1 is ch 0.
        bool isMetronomeChannel =
            ((int)event.port() == kMetronomePort &&
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
            uint64_t samplePos =
                event.has_host_sample_position()
                    ? event.host_sample_position()
                    : event.timestamp_samples();
            int noteNum = (int)event.note_on().note_number();
            metronomeTracker_.onBeat(noteNum, samplePos);

            // Broadcast each beat position to the UI so the Timeline
            // can use actual beat positions for its grid.
            bool isDownbeat = (noteNum == metronomeTracker_.downbeatNote);
            juce::DynamicObject::Ptr beatObj = new juce::DynamicObject();
            beatObj->setProperty("samplePosition",
                                 (juce::int64)samplePos);
            beatObj->setProperty("isDownbeat", isDownbeat);
            beatObj->setProperty("noteNumber", noteNum);
            beatObj->setProperty("velocity",
                                 (int)event.note_on().velocity());
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
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::NoteOn,
                (int)event.note_on().note_number(),
                (int)event.note_on().velocity(), nowMs);
          } else if (event.has_note_off()) {
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::NoteOff,
                (int)event.note_off().note_number(),
                (int)event.note_off().velocity(), nowMs);
          } else if (event.has_cc()) {
            mixer_.recordIncomingMidi(
                port, ch0, CapturedMidiEvent::CC,
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
          // event.port() from Dorico is 0-based (matches strip inputPort directly).
          // event.channel() from Dorico is 1-based; subtract 1 for strip inputChannel.
          int port = (int)event.port();
          int ccNum = event.cc().controller_number();
          int ccVal = event.cc().controller_value();

          // CC 120 (All Sound Off) or CC 123 (All Notes Off) → panic matching strips
          if (ccNum == 120 || ccNum == 123) {
            mixer_.panicStrips(port, ch - 1);
            // Do NOT update transport state here! Dorico sends CC 123
            // continually during playback as an emergency off, not just on transport stop.
            // If we stop transport here, it causes the delay buffer to bypass
            // and cuts off notes randomly, resulting in tremolo.
            std::cerr << "[MainComponent] Panic: CC " << ccNum
                      << " → panicStrips on port " << port << " ch " << ch - 1 << std::endl;
          } else {
            // Route CC through annotator-aware path.
            // ch from Dorico is 1-based; JUCE controllerEvent also uses 1-16.
            juce::MidiMessage ccMsg =
                juce::MidiMessage::controllerEvent(ch, ccNum, ccVal);
            double triggerTimeMs = getDelayedTriggerTimeMs();
            mixer_.routeAnnotatedCC(port, ch - 1, event, ccMsg,
                                    triggerTimeMs);
          }

          // ch is 1-based from protobuf — log it directly (no +1 needed)
          juce::String logMsg = "<b>[CC]</b> Ch " + juce::String(ch) +
                                " CC" + juce::String(ccNum) + " = " +
                                juce::String(ccVal);
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
          safeCallAsync([this]() {
            pushLogMessage("<b>[Host]</b> Plugin connected");
            pushConfigStatus();
          });
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
            undoManager_.clear();

            for (auto &rs : restored->strips) {
              juce::String newId = mixer_.addStrip();
              if (auto *strip = mixer_.getStrip(newId)) {
                strip->id = rs.id;
                strip->library = rs.library;
                strip->family = rs.family;
                strip->isSolo = rs.isSolo;
                strip->active = rs.active;
                strip->inputPort = rs.inputPort;
                strip->inputChannel = rs.inputChannel;
                strip->pluginUid = rs.pluginUid;
                strip->gainDb.store(rs.gainDb, std::memory_order_relaxed);
                setupStripListener(*strip);

                // Restore expression map
                if (!rs.expressionMapEntityID.empty()) {
                  auto xmapData = xmapLibrary_.load(rs.expressionMapEntityID);
                  if (xmapData)
                    strip->setExpressionMap(xmapData);
                }

                // Load plugin
                if (strip->pluginUid != 0) {
                  for (const auto &d :
                       pluginScanner_.getKnownPluginList().getTypes()) {
                    if (d.uniqueId == strip->pluginUid) {
                      auto stateBlock = rs.pluginState;
                      strip->loadPlugin(
                          d, mixer_.getFormatManager(),
                          [strip, stateBlock](bool success) {
                            if (success && stateBlock.getSize() > 0 &&
                                strip->pluginInstance) {
                              strip->pluginInstance->setStateInformation(
                                  stateBlock.getData(),
                                  (int)stateBlock.getSize());
                              strip->refreshPluginStateCache();
                            }
                          });
                      break;
                    }
                  }
                }

                // Restore Lua plugins
                for (const auto &fileName : rs.luaPluginFileNames) {
                  auto resolved = luaCatalog_.resolvePluginPath(fileName);
                  if (!resolved.empty()) {
                    auto plugin = std::make_shared<LuaPlugin>(resolved);
                    if (plugin->load())
                      strip->addLuaPlugin(std::move(plugin));
                  }
                }
              }
            }

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
          broadcastMessage("setConnectionState", connected);
          if (connected) {
            pushConfigStatus();
            pushLogMessage("<span style=\"color: #03dac6\">[Connected: " +
                           host + "]</span>");
          } else {
            pushLogMessage(
                "<span style=\"color: #cf6679\">[Disconnected]</span>");
          }
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
      juce::String err = deviceManager.initialiseWithDefaultDevices(0, 2);
      if (err.isNotEmpty()) {
        std::cerr << "[Audio] Failed to initialize device manager: " << err
                  << std::endl;
      } else {
        deviceManager.addAudioCallback(this);
      }
    }
    safeCallAsync([this]() { initDatabase(); });
}

void MainComponent::initDatabase() {
  addInitMessage("Opening database...");

    // Open SQLite database
    auto dbFile = FiddleConfig::getAppDataDir().getChildFile("fiddle.db");
    db_.open(dbFile);

    // Create the VersionStore backed by the database's SqliteVersionStorage.
    // This must happen immediately after db_.open() so that all downstream
    // code (pushDagHistory, pushBranches, stateManager) can use it.
    if (auto *storage = db_.getVersionStorage()) {
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
                      << ver->branchId << " — removing record only."
                      << std::endl;
            // Only delete the branch record; its versions belong to another
            // branch and must not be removed.
            versionStore_->getStorage().deleteBranch(bid);
          }
        }
      }

      // Wire the version store into the state manager so it embeds ancestor
      // hashes in the Dorico blob.
      stateManager_.setVersionStore(versionStore_.get());

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
            historyWindow_ =
                std::make_unique<HistoryWindow>(webViewBridge_.createWebOptions());
            historyWindowLoaded_ = false;
            juce::String root =
                juce::WebBrowserComponent::getResourceProviderRoot();
            historyWindow_->getWebView().goToURL(root +
                                                 "index.html?view=history");
            if (hws.width > 0 && hws.height > 0) {
              historyWindow_->restoreGeometry(hws.x, hws.y, hws.width,
                                              hws.height, true);
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
            libraryManagerWindow_ =
                std::make_unique<LibraryManagerWindow>(webViewBridge_.createWebOptions());
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

    // Auto-scan for plugins on startup (incremental — uses plugin_cache).
    std::cerr << "[Startup] Auto-scanning for VST3 plugins..." << std::endl;

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
      pushLogMessage("<b>[Setup]</b> Loaded " +
                     juce::String(masterList_.size()) + " saved instruments");
    }

    // Auto-scan for plugins on startup (incremental — uses plugin_cache).
    std::cerr << "[Startup] Auto-scanning for VST3 plugins..." << std::endl;
    pluginScanner_.scanIncrementalAsync(db_, [this]() {
      int count = pluginScanner_.getPluginCount();
      std::cerr << "[Startup] Plugin scan complete: " << count
                << " plugins found" << std::endl;
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    });

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
      server->setCachedConfigStatus(msg);
    }
    safeCallAsync([this]() { initPluginsAndStrips(); });
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

    // Ensure mixer strips exist for all ensemble instruments
    mixer_.syncStripsToInstruments(masterList_);

    pushMixerState(false);

    // Build initial shadow state blob
    scheduleStateRebuild();

    // Cache config_status so new client connections get it immediately
    pushConfigStatus();

    // Wait for the WebView to signal it's ready before dismissing splash
    addInitMessage("Preparing interface...");
}


void MainComponent::saveConfig() {
  saveAllStripsToDB();

  // Commit a new version on the currently-checked-out branch.
  if (versionStore_ && !currentBranchId_.empty()) {
    stateManager_.commitCurrentState(mixer_, currentBranchId_);
    // Track the new head as the current version.
    auto headOpt = versionStore_->getBranchHead(currentBranchId_);
    if (headOpt)
      currentVersionId_ = *headOpt;

    stateManager_.clearDirty();
    undoManager_.markSavePoint();
    broadcastMessage("setDirtyState", false);
    pushLogMessage("<b>[Save]</b> Committed to branch");
    pushConfigStatus();

    // Reset plugin state hashes so unchanged state doesn't re-trigger dirty
    pluginStateHashes_.clear();

    // pushBranches() must come before pushDagHistory() so the frontend's
    // headHashes set is up-to-date when it processes the new version list.
    pushBranches();
    pushDagHistory();
    pushCurrentVersion();
  } else {
    pushLogMessage("<b>[Save]</b> No branch checked out");
  }

  // Rebuild state blob
  stateManager_.scheduleRebuild([this]() -> juce::MemoryBlock {
    return stateManager_.buildStateBlob(mixer_);
  });
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
  cs->set_delay_ms(mixer_.getPlaybackDelayMs());

  // Cache for immediate push on future client connections
  server->setCachedConfigStatus(msg);
  server->sendToClient(msg);
}

void MainComponent::saveAllStripsToDB() {
  auto strips = mixer_.getAllStrips();
  // First, clear and re-save all strips with correct positions
  db_.clearStrips();
  for (int i = 0; i < (int)strips.size(); ++i) {
    db_.saveStrip(*strips[i], i);
    // Also save plugin BLOB if loaded
    if (strips[i]->pluginInstance) {
      juce::MemoryBlock block;
      strips[i]->pluginInstance->getStateInformation(block);
      if (block.getSize() > 0) {
        db_.savePluginBlob(strips[i]->id, block);
      }
    }
  }

  // Save plugin scanner cache
  if (auto xml = pluginScanner_.getKnownPluginList().createXml()) {
    db_.saveSetting("plugin_cache", xml->toString().toStdString());
  }
}

void MainComponent::saveStripToDB(const juce::String &stripId) {
  int idx = mixer_.stripIndex(stripId);
  if (auto *s = mixer_.getStrip(stripId)) {
    db_.saveStrip(*s, idx >= 0 ? idx : 0);
  }
}

void MainComponent::scheduleStateRebuild() {
  // While viewing a historical version, do not update the Dorico shared-
  // memory blob — the current mixer state is read-only and must not be
  // committed as an ancestor of the live branch.
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
    return stateManager_.buildStateBlob(mixer_);
  });
}

void MainComponent::setupStripListener(MixerStrip &strip) {
  strip.pluginChangeListener_.listenerCapableUids = &listenerCapableUids_;
  strip.pluginChangeListener_.onDirty = [this, &strip]() {
    // Debounce: listener fires from any thread (often audio thread)
    if (!listenerDirtyPending_.exchange(true, std::memory_order_acq_rel)) {
      int uid = strip.pluginUid;
      safeCallAsync([this, uid, &strip]() {
        listenerDirtyPending_.store(false, std::memory_order_release);
        stateManager_.markDirty();
        pushConfigStatus();
        broadcastMessage("setDirtyState", true);
        // Refresh cache for the changed strip
        strip.refreshPluginStateCache();
        scheduleStateRebuild();
        // Persist this UID as listener-capable (idempotent)
        if (uid != 0)
          db_.savePluginCapability(uid);
      });
    }
  };
}

void MainComponent::pollPluginStateChanges() {
  auto strips = mixer_.getAllStrips();
  for (auto *strip : strips) {
    if (!strip->pluginInstance)
      continue;

    // Skip plugins known to fire listener callbacks
    if (strip->pluginUid != 0 &&
        listenerCapableUids_.count(strip->pluginUid) > 0)
      continue;

    // Hash the plugin's serialized state (FNV-1a)
    juce::MemoryBlock block;
    strip->pluginInstance->getStateInformation(block);
    uint64_t hash = 14695981039346656037ULL; // FNV offset basis
    auto *data = static_cast<const uint8_t *>(block.getData());
    for (size_t i = 0; i < block.getSize(); ++i) {
      hash ^= data[i];
      hash *= 1099511628211ULL; // FNV prime
    }

    auto it = pluginStateHashes_.find(strip->id);
    if (it == pluginStateHashes_.end()) {
      // First time seeing this plugin — cache its hash
      pluginStateHashes_[strip->id] = hash;
    } else if (it->second != hash) {
      // State changed!
      it->second = hash;
      std::cerr << "[PluginPoll] State change detected in strip: " << strip->id
                << " (" << strip->library << ")" << std::endl;
      strip->refreshPluginStateCache();
      stateManager_.markDirty();
      pushConfigStatus();
      broadcastMessage("setDirtyState", true);
      scheduleStateRebuild();
      return; // One dirty notification per poll cycle is enough
    }
  }
}

void MainComponent::loadStripsFromDB() {
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
      strip->active = row.active;
      strip->inputPort = row.inputPort;
      strip->inputChannel = row.inputChannel;
      strip->gainDb.store(row.gainDb, std::memory_order_relaxed);

      // Wire up plugin change listener for dirty detection
      setupStripListener(*strip);

      // Restore expression map
      if (!row.expressionMapEntityID.empty()) {
        auto data = xmapLibrary_.load(row.expressionMapEntityID);
        if (data) {
          strip->setExpressionMap(data);
          std::cerr << "[loadDB] Restored xmap '" << data->name
                    << "' for strip " << strip->id << std::endl;
        }
      }

      // Restore plugin
      if (row.pluginUid != 0) {
        for (const auto &d : pluginScanner_.getKnownPluginList().getTypes()) {
          if (d.uniqueId == row.pluginUid) {
            auto stateBlock = row.pluginState;
            strip->loadPlugin(d, mixer_.getFormatManager(),
                              [strip, stateBlock](bool success) {
                                if (success && stateBlock.getSize() > 0 &&
                                    strip->pluginInstance) {
                                  strip->pluginInstance->setStateInformation(
                                      stateBlock.getData(),
                                      (int)stateBlock.getSize());
                                  strip->refreshPluginStateCache();
                                }
                              });
            break;
          }
        }
      }

      // Restore Lua plugins
      for (const auto &fileName : row.luaPluginFileNames) {
        auto resolved = luaCatalog_.resolvePluginPath(fileName);
        if (resolved.empty()) {
          std::cerr << "[loadDB] Lua plugin not found: " << fileName
                    << std::endl;
          continue;
        }
        auto plugin = std::make_shared<LuaPlugin>(resolved);
        if (plugin->load()) {
          strip->addLuaPlugin(std::move(plugin));
          std::cerr << "[loadDB] Restored Lua plugin '" << fileName
                    << "' for strip " << strip->id << std::endl;
        } else {
          std::cerr << "[loadDB] Failed to load Lua plugin: " << fileName
                    << std::endl;
        }
      }
    }
  }
  std::cerr << "[MainComponent] Loaded " << rows.size() << " strips from SQLite"
            << std::endl;
}

MainComponent::~MainComponent() {
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

  stopTimer();
  deviceManager.removeAudioCallback(this);
  server.reset();
}



void MainComponent::pushMixerState(bool markDirty) {
  if (!webViewBridge_.isLoaded())
    return;
  if (markDirty) {
    bool wasDirty = stateManager_.isDirty();
    stateManager_.markDirty();
    broadcastMessage("setDirtyState", true);
    // Notify plugin on first dirty transition (not on every fader drag)
    if (!wasDirty)
      pushConfigStatus();
  }
  juce::String json = mixer_.toJson();
  broadcastMessage("setMixerState", juce::JSON::fromString(json));
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
    historyWindow_ = std::make_unique<HistoryWindow>(webViewBridge_.createWebOptions());
    historyWindowLoaded_ = false;
    juce::String root =
        juce::WebBrowserComponent::getResourceProviderRoot();
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


void MainComponent::toggleLibraryManagerWindow() {
  // Lazily create the Library Manager window on first toggle.
  if (!libraryManagerWindow_) {
    libraryManagerWindow_ =
        std::make_unique<LibraryManagerWindow>(webViewBridge_.createWebOptions());
    libraryManagerWindowLoaded_ = false;
    juce::String root =
        juce::WebBrowserComponent::getResourceProviderRoot();
    libraryManagerWindow_->getWebView().goToURL(root +
                                                "index.html?view=library");
    libraryManagerWindow_->setGeometrySaver(
        [this]() { saveLibraryManagerWindowGeometry(); });

    // Restore saved geometry (if any).
    auto lws = db_.loadWindowSettings("library");
    if (lws.width > 0 && lws.height > 0) {
      libraryManagerWindow_->restoreGeometry(lws.x, lws.y, lws.width,
                                             lws.height,
                                             false /* we toggle below */);
    }
  }

  if (libraryManagerWindow_) {
    bool wasVisible = libraryManagerWindow_->isVisible();
    bool nowVisible = !wasVisible;
    libraryManagerWindow_->setVisible(nowVisible);
    if (nowVisible) {
      libraryManagerWindow_->toFront(true);
    }

    auto bounds = libraryManagerWindow_->getBounds();
    fiddle::WindowSettings ws;
    ws.windowId = "library";
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = nowVisible;
    db_.saveWindowSettings(ws);
  }
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
  ws.visible = existing.found ? existing.visible : libraryManagerWindow_->isVisible();
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
  subnoteGenerator.tick(noteTracker.getSessionSamples());

  // Push meter levels to UI every ~60ms (3 × 20ms timer ticks)
  static int meterCounter = 0;
  if (++meterCounter % 3 == 0) {
    safeCallAsync([this]() { pushMixerState(false); });
  }

  static int hbCounter = 0;
  if (++hbCounter % 50 == 0) { // Every 1 second (20ms * 50)
    safeCallAsync([this, val = hbCounter / 50]() {
      broadcastMessage("setHeartbeat", val);
    });
  }

  // Poll hosted VST plugin state every ~2s (100 × 20ms).
  // Many VST3 plugins (e.g. Vienna Synchron Player) don't fire
  // AudioProcessorListener callbacks, so we hash getStateInformation()
  // to detect changes.
  if (++pluginPollCounter_ % 100 == 0) {
    pollPluginStateChanges();
  }

  // Flush any deferred state rebuild (throttled to 1/sec)
  if (stateRebuildPending_) {
    auto now = juce::Time::getMillisecondCounter();
    if (now - lastStateRebuildMs_ >= 1000) {
      lastStateRebuildMs_ = now;
      stateRebuildPending_ = false;
      stateManager_.scheduleRebuild([this]() -> juce::MemoryBlock {
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
    mixer_.prepareToPlay(device->getCurrentSampleRate(),
                         device->getCurrentBufferSizeSamples());
  }
}

void MainComponent::audioDeviceStopped() {}

void MainComponent::audioDeviceIOCallbackWithContext(
    const float *const *inputChannelData, int numInputChannels,
    float *const *outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext &context) {

  // Clear any garbage from output buffers
  for (int i = 0; i < numOutputChannels; ++i) {
    if (outputChannelData[i] != nullptr) {
      juce::FloatVectorOperations::clear(outputChannelData[i], numSamples);
    }
  }

  juce::AudioBuffer<float> audioBuffer(outputChannelData, numOutputChannels,
                                       numSamples);
  double currentTime = juce::Time::getMillisecondCounterHiRes();

  // 1. Process VST instruments and mix down to audioBuffer
  mixer_.processBlock(audioBuffer, currentTime);

  // Diagnostic: check if the mixer produced any audio
  {
    static int diagCounter = 0;
    if (++diagCounter % 500 == 0) {
      float peak = 0.0f;
      for (int ch = 0; ch < audioBuffer.getNumChannels(); ++ch) {
        float chPeak =
            audioBuffer.getMagnitude(ch, 0, audioBuffer.getNumSamples());
        if (chPeak > peak)
          peak = chPeak;
      }
      if (peak > 0.0f) {
        // std::cerr << "[AudioDiag] Peak after mixer: " << peak
        //           << " dB=" << juce::Decibels::gainToDecibels(peak)
        //           << std::endl;
      }
    }
  }

  // 2. Transmit the mixed audioBuffer to Dorico via Shared Memory IPC
  audioSharedMemory_.pushAudio(audioBuffer);

  // 3. Clear the local speaker buffer so FiddleServer doesn't play directly
  // through macOS CoreAudio.
  //    This forces us to listen ONLY through the Dorico Mixer return route!
  audioBuffer.clear();
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
}



void MainComponent::broadcastMessage(const juce::String &type,
                                     const juce::var &data) {
  auto *envelope = new juce::DynamicObject();
  envelope->setProperty("type", type);
  envelope->setProperty("data", data);
  juce::String json = juce::JSON::toString(juce::var(envelope), true);
  juce::String js =
      "window.__dispatchFromCpp && window.__dispatchFromCpp(" + json + ")";
  webViewBridge_.broadcastJavascript(js, 
                                     historyWindow_ ? &historyWindow_->getWebView() : nullptr,
                                     libraryManagerWindow_ ? &libraryManagerWindow_->getWebView() : nullptr);
  // Include debug window so broadcastMessage reaches the Plugins/Timeline panels
  if (debugWindow_) {
    debugWindow_->evaluateJavascript(js);
  }
}

void MainComponent::setupJsHandlers() {
  jsRouter_.registerHandler("signalReady", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    juce::String viewMode = "mixer";
    if (args.size() > 0) {
      viewMode = args[0].toString();
    }

    juce::WebBrowserComponent *targetWebComponent = &webViewBridge_.getMainWebComponent();
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
    }

    // Reveal the WebView now that it's fully loaded
    if (!isHistoryWindow) {
      initComplete_ = true;
      webViewBridge_.getMainWebComponent().setBounds(getLocalBounds());
      repaint();
    }
    return;
  });
  jsRouter_.registerHandler("setMode", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("nativeLog", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() > 0)
      std::cerr << "[JS NativeLog] " << args[0].toString() << std::endl;
    return;
  });
  jsRouter_.registerHandler("requestSetupData", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("saveSelectedInstruments", [this](const juce::var& payload) {
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

        // Rebuild channel_assignments from the sequential flat indices used by
        // the generated Dorico playback template. reconcileAssignments above
        // tries to preserve stable indices across score changes, but can
        // diverge from the template's sequential order (e.g. after reordering
        // ensemble slots). By rebuilding here we guarantee the DB always
        // matches the MIDI port/channel layout that Dorico will use.
        std::vector<ChannelAssignmentRow> newRows;
        newRows.reserve(assignments.size());
        // Track instanceNum per (entityID, isSolo) pair
        std::map<std::pair<juce::String, bool>, int> instanceCounts;
        for (int idx = 0; idx < (int)assignments.size(); ++idx) {
          const auto& a = assignments[idx];
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
        broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson2));
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
  jsRouter_.registerHandler("scanPlugins", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (pluginScanner_.isScanning()) {
      return;
    }
    pushLogMessage("<b>[Plugins]</b> Scanning for VST3 plugins "
                   "(incremental)...");
    pluginScanner_.scanIncrementalAsync(db_, [this]() {
      int count = pluginScanner_.getPluginCount();
      pushLogMessage("<b>[Plugins]</b> Scan complete: " + juce::String(count) +
                     " plugins found");
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    });
    return;
  });
  jsRouter_.registerHandler("rescanPlugins", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (pluginScanner_.isScanning()) {
      return;
    }
    pushLogMessage("<b>[Plugins]</b> Full rescan of VST3 plugins...");
    pluginScanner_.rescanAsync(db_, [this]() {
      int count = pluginScanner_.getPluginCount();
      pushLogMessage("<b>[Plugins]</b> Rescan complete: " +
                     juce::String(count) + " plugins found");
      juce::String json = pluginScanner_.getPluginListAsJson();
      broadcastMessage("setPluginList", juce::JSON::fromString(json));
    });
    return;
  });
  jsRouter_.registerHandler("addMixerStrip", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      auto action = std::make_unique<AddStripAction>(mixer_);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("duplicateStripInput", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      auto action = std::make_unique<DuplicateStripAction>(mixer_, stripId);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("removeMixerStrip", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      auto action = std::make_unique<RemoveStripAction>(mixer_, stripId);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    juce::String lib = args[1].toString();
    safeCallAsync([this, stripId, lib]() {
      juce::String oldLib;
      if (auto *s = mixer_.getStrip(stripId))
        oldLib = s->library;
      auto action =
          std::make_unique<SetLibraryAction>(mixer_, stripId, oldLib, lib);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripInput", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 3) {
      return;
    }
    juce::String stripId = args[0].toString();
    int port = (int)args[1];
    int channel = (int)args[2];
    safeCallAsync([this, stripId, port, channel]() {
      int oldPort = -1, oldCh = -1;
      if (auto *s = mixer_.getStrip(stripId)) {
        oldPort = s->inputPort;
        oldCh = s->inputChannel;
      }
      auto action = std::make_unique<SetInputAction>(mixer_, stripId, oldPort,
                                                     oldCh, port, channel);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripGain", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    float gainDb = static_cast<float>((double)args[1]);
    gainDb = juce::jlimit(-120.0f, 6.0f, gainDb);
    safeCallAsync([this, stripId, gainDb]() {
      float oldGain = 0.0f;
      if (auto *s = mixer_.getStrip(stripId))
        oldGain = s->gainDb.load(std::memory_order_relaxed);
      auto action =
          std::make_unique<SetGainAction>(mixer_, stripId, oldGain, gainDb);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripMute", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    bool mute = (bool)args[1];
    safeCallAsync([this, stripId, mute]() {
      mixer_.setStripMute(stripId, mute);
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("setStripSolo", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    bool solo = (bool)args[1];
    safeCallAsync([this, stripId, solo]() {
      mixer_.setStripSolo(stripId, solo);
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("toggleLibraryActive", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1)
      return;
    juce::String libraryName = args[0].toString();
    safeCallAsync([this, libraryName]() {
      // Determine current state: active if ALL strips with this library are
      // active
      bool allActive = true;
      auto strips = mixer_.getAllStrips();
      for (auto *s : strips) {
        if (s->library == libraryName && !s->active) {
          allActive = false;
          break;
        }
      }
      // Toggle: if all active, deactivate; if any inactive, activate all
      bool newState = !allActive;
      auto action = std::make_unique<ToggleLibraryActiveAction>(
          mixer_, libraryName, newState);
      undoManager_.perform(std::move(action));
      saveAllStripsToDB();
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("getAnnotationRecords", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("clearAnnotationRecords", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("startMidiCapture", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("stopMidiCapture", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("getMidiCapture", [this](const juce::var& payload) {
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
      envelope->setProperty(
          "count",
          (int)(mode == "incoming" ? strip->incomingCapture.eventCount()
                                   : strip->emittedCapture.eventCount()));
      broadcastMessage("setMidiCapture", juce::var(envelope));
    });
    return;
  });
  jsRouter_.registerHandler("clearMidiCapture", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("getCaptureStripList", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      juce::Array<juce::var> arr;
      for (auto *strip : mixer_.getAllStrips()) {
        // Only show strips that have an expression map assigned
        if (!strip->expressionMap)
          continue;
        auto *obj = new juce::DynamicObject();
        obj->setProperty("id", strip->id);
        obj->setProperty(
            "name", strip->library.isNotEmpty()
                        ? strip->library + " (" + strip->family + ")"
                        : strip->id);
        obj->setProperty("port", strip->inputPort + 1); // 1-based for Dorico
        obj->setProperty("channel", strip->inputChannel);
        arr.add(juce::var(obj));
      }
      broadcastMessage("setCaptureStripList", juce::var(arr));
    });
    return;
  });
  jsRouter_.registerHandler("exportCaptureFile", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String text = args[0].toString();
    juce::String defaultName = args[1].toString();

    auto chooser = std::make_shared<juce::FileChooser>(
        "Export MIDI Capture",
        juce::File::getSpecialLocation(
            juce::File::userDesktopDirectory)
            .getChildFile(defaultName + ".midi-dump"),
        "*.midi-dump");

    chooser->launchAsync(
        juce::FileBrowserComponent::saveMode |
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
  jsRouter_.registerHandler("addStripLuaPlugin", [this](const juce::var& payload) {
    // payload: [stripId, pluginFilePath]
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    std::string pluginPath = args[1].toString().toStdString();

    // Verify the plugin is in the catalog (try full path first, then just filename)
    std::string fileName =
        std::filesystem::path(pluginPath).filename().string();
    const auto *meta = luaCatalog_.findByPath(pluginPath);
    if (!meta)
      meta = luaCatalog_.findByFileName(fileName);
    if (!meta) {
      std::cerr << "[Lua] Plugin not in catalog: " << pluginPath << std::endl;
      return;
    }
    safeCallAsync([this, stripId, fileName]() {
      auto action = std::make_unique<AddLuaPluginAction>(
          mixer_, luaCatalog_, stripId, fileName);
      undoManager_.perform(std::move(action));
      saveAllStripsToDB();
      pushMixerState();
      scheduleStateRebuild();
    });
    return;
  });
  jsRouter_.registerHandler("removeStripLuaPlugin", [this](const juce::var& payload) {
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
          mixer_, luaCatalog_, stripId, pluginIndex);
      undoManager_.perform(std::move(action));
      saveAllStripsToDB();
      pushMixerState();
      scheduleStateRebuild();
    });
    return;
  });
  jsRouter_.registerHandler("getLuaPluginCatalog", [this](const juce::var& payload) {
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
  jsRouter_.registerHandler("setStripPlugin", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    int pluginUid = (int)args[1];

    // Verify plugin exists in scanner
    if (pluginUid != 0) {
      bool found = false;
      for (const auto &d : pluginScanner_.getKnownPluginList().getTypes()) {
        if (d.uniqueId == pluginUid) {
          found = true;
          break;
        }
      }
      if (!found) {
        return;
      }
    }

    safeCallAsync([this, stripId, pluginUid]() {
      int oldUid = 0;
      if (auto *s = mixer_.getStrip(stripId))
        oldUid = s->pluginUid;
      auto action = std::make_unique<SetPluginAction>(
          mixer_, pluginScanner_, stripId, oldUid, pluginUid,
          [this]() { pushMixerState(); });
      undoManager_.perform(std::move(action));
    });
    return;
  });
  jsRouter_.registerHandler("setStripProgram", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() < 2)
      return;
    juce::String stripId = args[0].toString();
    int progIndex = (int)args[1];
    safeCallAsync([this, stripId, progIndex]() {
      if (auto *strip = mixer_.getStrip(stripId)) {
        if (strip->pluginInstance != nullptr) {
          strip->pluginInstance->setCurrentProgram(progIndex);
          strip->refreshPluginStateCache();
          pushMixerState();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("showStripEditor", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      if (auto *s = mixer_.getStrip(stripId))
        s->showEditor();
    });
    return;
  });
  jsRouter_.registerHandler("restoreLibraryPluginState", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 4) {
      return;
    }
    juce::String stripId = args[0].toString();
    juce::String libraryId = args[1].toString();
    juce::String entityId = args[2].toString();
    int instanceNum = (int)args[3];

    safeCallAsync([this, stripId, libraryId, entityId, instanceNum]() {
      auto *strip = mixer_.getStrip(stripId);
      if (!strip || !strip->pluginInstance) {
        std::cerr << "[restoreLibraryPluginState] No strip/plugin for "
                  << stripId << std::endl;
        return;
      }

      // Load saved instruments from DB and find the matching one
      auto instruments = db_.loadLibraryInstruments(libraryId);
      std::cerr << "[restoreLibraryPluginState] libraryId=" << libraryId
                << " entityId=" << entityId
                << " instanceNum=" << instanceNum
                << " instruments=" << instruments.size() << std::endl;
      bool found = false;
      for (const auto &inst : instruments) {
        if (inst.entityId == entityId && inst.hasInstance(instanceNum)) {
          std::cerr << "[restoreLibraryPluginState] Found entity+instance, blobSize="
                    << inst.pluginState.getSize() << std::endl;
          if (inst.pluginState.getSize() > 0) {
            strip->pluginInstance->setStateInformation(
                inst.pluginState.getData(),
                static_cast<int>(inst.pluginState.getSize()));
            strip->refreshPluginStateCache();
            std::cerr << "[restoreLibraryPluginState] Restored "
                      << inst.pluginState.getSize() << " bytes for strip "
                      << stripId << std::endl;
          } else {
            std::cerr << "[restoreLibraryPluginState] No blob stored for "
                      << entityId << " #" << instanceNum << std::endl;
          }
          found = true;
          break;
        }
      }
      if (!found) {
        std::cerr << "[restoreLibraryPluginState] Entity+instance not found in library"
                  << std::endl;
      }
    });
    return;
  });
  jsRouter_.registerHandler("loadExpressionMap", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    juce::String stripId = args[0].toString();
    std::string entityID = args[1].toString().toStdString();

    auto data = xmapLibrary_.load(entityID);
    if (!data) {
      return;
    }

    safeCallAsync([this, stripId, entityID, data]() {
      std::string oldEntityID;
      if (auto *s = mixer_.getStrip(stripId)) {
        if (s->expressionMap)
          oldEntityID = s->expressionMap->entityID;
      }
      auto action = std::make_unique<SetExpressionMapAction>(
          mixer_, xmapLibrary_, stripId, oldEntityID, entityID, data);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("clearExpressionMap", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();
    safeCallAsync([this, stripId]() {
      std::string oldEntityID;
      if (auto *s = mixer_.getStrip(stripId)) {
        if (s->expressionMap)
          oldEntityID = s->expressionMap->entityID;
      }
      if (oldEntityID.empty())
        return; // already clear
      auto action = std::make_unique<SetExpressionMapAction>(
          mixer_, xmapLibrary_, stripId, oldEntityID, "", nullptr);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  });
  jsRouter_.registerHandler("loadExpressionMapFromFile", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    juce::String stripId = args[0].toString();

    auto chooser = std::make_shared<juce::FileChooser>(
        "Load Expression Map", juce::File{}, "*.doricolib");

    chooser->launchAsync(juce::FileBrowserComponent::openMode |
                             juce::FileBrowserComponent::canSelectFiles,
                         [this, stripId, chooser](const juce::FileChooser &fc) {
                           auto results = fc.getResults();
                           if (results.isEmpty())
                             return;

                           auto file = results[0];
                           auto data = std::make_shared<ExpressionMapData>();
                           if (!parseExpressionMap(file, *data))
                             return;

                           if (auto *s = mixer_.getStrip(stripId)) {
                             s->setExpressionMap(data);
                             s->expressionMapPath = file.getFullPathName();
                           }

                           safeCallAsync([this]() { pushMixerState(); });
                         });
    return;
  });
  jsRouter_.registerHandler("requestLibraries", [this](const juce::var& payload) {
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
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("saveLibrary", [this](const juce::var& payload) {
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

    std::vector<fiddle::LibraryInstrumentRow> instruments;
    if (auto *instArr = data->getProperty("instruments").getArray()) {
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
              inst.pluginState = strip->cachedPluginState_;
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

    safeCallAsync([this, lib = std::move(lib),
                   instruments = std::move(instruments)]() {
      db_.saveLibrary(lib, instruments);

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
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("loadLibrary", [this](const juce::var& payload) {
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

      auto instruments = db_.loadLibraryInstruments(libraryId);

      auto *result = new juce::DynamicObject();
      result->setProperty("id", foundLib.id);
      result->setProperty("name", foundLib.name);
      result->setProperty("vendor", foundLib.vendor);
      result->setProperty("variant", foundLib.variant);

      juce::Array<juce::var> instArr;
      for (const auto &inst : instruments) {
        auto *instObj = new juce::DynamicObject();
        instObj->setProperty("entityID", inst.entityId);
        instObj->setProperty("name", inst.name);
        instObj->setProperty("family", inst.family);
        instObj->setProperty("category", inst.category);
        instObj->setProperty("vstPlugin", inst.vstPlugin);
        instObj->setProperty("exprMap", inst.exprMap);
        instObj->setProperty("pluginUid", inst.pluginUid);
        instObj->setProperty("hasPluginState",
                             inst.pluginState.getSize() > 0);
        juce::Array<juce::var> iNums;
        for (int n : inst.instanceNums) iNums.add(n);
        instObj->setProperty("instanceNums", juce::var(iNums));
        instObj->setProperty("isSolo", inst.isSolo);
        instObj->setProperty("note", inst.note);
        instArr.add(juce::var(instObj));
      }
      result->setProperty("instruments", juce::var(instArr));

      broadcastMessage("setLibraryData", juce::var(result));
    });
    return;
  });
  jsRouter_.registerHandler("buildPlaybackTemplate", [this](const juce::var& payload) {
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
          if (entityId != o.entityId) return entityId < o.entityId;
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
      std::map<juce::String, std::pair<int, int>> entityCounts; // soloCount, sectionCount
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
      // Clear graveyard so stale entries don't get reclaimed into the new template
      db_.clearGraveyard();

      DoricoConfigGenerator generator;
      auto slots = masterList_.getSlots();
      auto assignments = DoricoConfigGenerator::expandSlots(slots);
      int numChannels = masterList_.totalSlotCount();

      auto result = generator.generateAndInstallFiles(
          assignments, numChannels, browserInstruments);

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
        if (s->active && s->library.isNotEmpty())
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
                if (s->inputPort == port && s->inputChannel == ch &&
                    s->library.isEmpty())
                  return s;
              }
              return nullptr;
            }();

            // Get the slot info for this instrument type
            SlotKey sKey{entityId, isSolo};
            auto slotIt = slotMap.find(sKey);
            if (slotIt == slotMap.end()) continue;
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
              strip->active = previouslyActiveLibs.count(libNames[libId]) > 0;

              if (inst.exprMap.isNotEmpty()) {
                auto xmapData = xmapLibrary_.load(
                    inst.exprMap.toStdString());
                if (xmapData)
                  strip->setExpressionMap(xmapData);
              }

              // Resolve effective plugin UID: pluginUid is set when a
              // live strip was linked during save; vstPlugin (string)
              // always holds the dropdown selection.
              int effectiveUid = inst.pluginUid;
              if (effectiveUid == 0 && inst.vstPlugin.isNotEmpty())
                effectiveUid = inst.vstPlugin.getIntValue();

              if (effectiveUid != 0) {
                strip->pluginUid = effectiveUid;
                for (const auto &d :
                     pluginScanner_.getKnownPluginList().getTypes()) {
                  if (d.uniqueId == effectiveUid) {
                    auto stateBlock = inst.pluginState;
                    strip->loadPlugin(
                        d, mixer_.getFormatManager(),
                        [strip, stateBlock](bool success) {
                          if (success && stateBlock.getSize() > 0 &&
                              strip->pluginInstance) {
                            strip->pluginInstance->setStateInformation(
                                stateBlock.getData(),
                                (int)stateBlock.getSize());
                            strip->refreshPluginStateCache();
                          }
                        });
                    break;
                  }
                }
              }
            };

            // Create one strip per library for this channel
            for (int libIdx = 0; libIdx < (int)sortedLibIds.size(); ++libIdx) {
              const juce::String &libId = sortedLibIds[libIdx];

              // Find the matching library instrument
              StripKey lookupKey{entityId, isSolo, instNum};
              auto lookupIt = stripLookup.find(lookupKey);
              if (lookupIt == stripLookup.end()) continue;

              const fiddle::LibraryInstrumentRow *matchedInst = nullptr;
              for (const auto &li : lookupIt->second) {
                if (li.libraryId == libId) {
                  matchedInst = &li;
                  break;
                }
              }
              if (!matchedInst) continue;

              if (libIdx == 0 && baseStrip) {
                // First library uses the base strip
                initStrip(baseStrip, *matchedInst, libId);
              } else {
                // Additional libraries: create a new strip sharing
                // the same input
                auto newStrip = std::make_unique<MixerStrip>();
                newStrip->id = juce::Uuid().toString();
                newStrip->inputPort = port;
                newStrip->inputChannel = ch;
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
      lastInstalledFingerprint_ = computeLibraryFingerprint();
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("deleteLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.isEmpty())
      return;

    juce::String libraryId = args[0].toString();
    safeCallAsync([this, libraryId]() {
      db_.deleteLibrary(libraryId);

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
      broadcastTemplateDirty();
    });
    return;
  });
  jsRouter_.registerHandler("requestExpressionMaps", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      juce::String json = xmapLibrary_.toJson();
      broadcastMessage("setExpressionMaps", juce::JSON::parse(json));
    });
    return;
  });
  jsRouter_.registerHandler("undo", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.undo()) {
        pushMixerState();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          broadcastMessage("setDirtyState", false);
          pushConfigStatus();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("redo", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.redo()) {
        pushMixerState();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          broadcastMessage("setDirtyState", false);
          pushConfigStatus();
        }
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestPluginsState", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      // Always reply with the current scanning state so the UI can show the
      // spinner (or hide it) regardless of whether plugins are loaded yet.
      broadcastMessage("isScanningPlugins", pluginScanner_.isScanning());
      if (pluginScanner_.getPluginCount() > 0) {
        juce::String json = pluginScanner_.getPluginListAsJson();
        // Use broadcastMessage so all windows (main, debug, history) get it
        broadcastMessage("setPluginList", juce::JSON::fromString(json));
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestMixerState", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { pushMixerState(false); });
    return;
  });
  jsRouter_.registerHandler("getAvailableInputs", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      juce::String json = masterList_.getChannelMapAsJson();
      broadcastMessage("setAvailableInputs", juce::JSON::parse(json));
    });
    return;
  });
  jsRouter_.registerHandler("setPlaybackDelay", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() >= 1) {
      int ms = static_cast<int>(args[0]);
      safeCallAsync([this, ms]() {
        mixer_.setPlaybackDelayMs(ms);
        pushConfigStatus();
        pushLogMessage("<b>[Mixer]</b> Playback delay set to " +
                       juce::String(ms) + " ms");
      });
    }
    return;
  });
  jsRouter_.registerHandler("getPlaybackDelay", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      int ms = mixer_.getPlaybackDelayMs();
      broadcastMessage("setPlaybackDelay", juce::var(ms));
    });
    return;
  });
  jsRouter_.registerHandler("setGroupGainDelta", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    // args[0] = JSON array of strip IDs, args[1] = delta dB
    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    float delta = static_cast<float>((double)args[1]);
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, delta]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          float old = s->gainDb.load(std::memory_order_relaxed);
          float nw = juce::jlimit(-120.0f, 6.0f, old + delta);
          subs.push_back(std::make_unique<SetGainAction>(mixer_, sid, old, nw));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group gain delta", std::move(subs), "group-gain"));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupGainAbsolute", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    float gainDb =
        juce::jlimit(-120.0f, 6.0f, static_cast<float>((double)args[1]));
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, gainDb]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          float old = s->gainDb.load(std::memory_order_relaxed);
          subs.push_back(
              std::make_unique<SetGainAction>(mixer_, sid, old, gainDb));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group gain absolute", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupPlugin", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    int pluginUid = (int)args[1];
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, pluginUid]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          int oldUid = s->pluginUid;
          subs.push_back(std::make_unique<SetPluginAction>(
              mixer_, pluginScanner_, sid, oldUid, pluginUid,
              [this]() { pushMixerState(); }));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group set plugin", std::move(subs)));
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupLibrary", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    juce::String lib = args[1].toString();
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar, lib]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          subs.push_back(
              std::make_unique<SetLibraryAction>(mixer_, sid, s->library, lib));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group set library", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("setGroupExpressionMap", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 2) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    std::string entityID = args[1].toString().toStdString();
    if (!idsVar.isArray()) {
      return;
    }

    auto data = entityID.empty() ? nullptr : xmapLibrary_.load(entityID);
    if (!entityID.empty() && !data) {
      return;
    }

    safeCallAsync([this, idsVar, entityID, data]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (auto *s = mixer_.getStrip(sid)) {
          std::string oldID;
          if (s->expressionMap)
            oldID = s->expressionMap->entityID;
          subs.push_back(std::make_unique<SetExpressionMapAction>(
              mixer_, xmapLibrary_, sid, oldID, entityID, data));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group set expression map", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("removeGroupStrips", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      return;
    }
    auto idsVar = juce::JSON::parse(args[0].toString());
    if (!idsVar.isArray()) {
      return;
    }

    safeCallAsync([this, idsVar]() {
      auto *arr = idsVar.getArray();
      std::vector<std::unique_ptr<UndoableAction>> subs;
      for (auto &idVar : *arr) {
        juce::String sid = idVar.toString();
        if (mixer_.getStrip(sid)) {
          subs.push_back(std::make_unique<RemoveStripAction>(mixer_, sid));
        }
      }
      if (!subs.empty()) {
        undoManager_.perform(std::make_unique<CompoundAction>(
            "Group remove strips", std::move(subs)));
        pushMixerState();
      }
    });
    return;
  });
  jsRouter_.registerHandler("requestBranches", [this](const juce::var& payload) {
    safeCallAsync([this]() { pushBranches(); });
    return;
  });
  jsRouter_.registerHandler("requestCurrentBranch", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      if (versionStore_) {
        auto b = versionStore_->getStorage().getBranch(currentBranchId_);
        if (b) {
          broadcastMessage("setCurrentBranch",
                           juce::var(juce::String(currentBranchId_)));
          return;
        }
      }
      broadcastMessage("setCurrentBranch", juce::var(juce::String("default")));
    });
    return;
  });
  jsRouter_.registerHandler("openHistoryWindow", [this](const juce::var& payload) {
    safeCallAsync([this]() {
      if (!historyWindow_ && versionStore_) {
        historyWindow_ = std::make_unique<HistoryWindow>(webViewBridge_.createWebOptions());
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
  jsRouter_.registerHandler("requestDagHistory", [this](const juce::var& payload) {
    safeCallAsync([this]() { pushDagHistory(); });
    return;
  });
  jsRouter_.registerHandler("createBranch", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string branchName = args[0].toString().toStdString();
      // Optional second arg: a specific version UUID to branch from.
      // If omitted, branches from the current head (existing behaviour).
      std::string fromVersionId =
          (args.size() > 1) ? args[1].toString().toStdString() : "";
      safeCallAsync([this, branchName, fromVersionId]() {
        std::string baseVersionId;
        if (fromVersionId.empty()) {
          // Commit current state and get the resulting branch head VersionId.
          stateManager_.commitCurrentState(mixer_, currentBranchId_);
          auto headOpt = versionStore_->getBranchHead(currentBranchId_);
          if (headOpt)
            baseVersionId = *headOpt;
        } else {
          baseVersionId = fromVersionId;
        }
        if (baseVersionId.empty()) {
          std::cerr << "[createBranch] No base version to branch from"
                    << std::endl;
          return;
        }
        auto newBranchId =
            versionStore_->createBranch(branchName, baseVersionId);
        if (newBranchId.empty()) {
          std::cerr << "[createBranch] Failed to create branch from version: "
                    << baseVersionId << std::endl;
          return;
        }
        // When branching from an explicit version ID, also load that
        // version's state into the mixer so the user lands on the new branch.
        if (!fromVersionId.empty()) {
          auto verOpt = versionStore_->getVersion(baseVersionId);
          if (verOpt) {
            auto stateOpt = versionStore_->getState(verOpt->stateHash);
            if (stateOpt) {
              mixer_.clear();
              undoManager_.clear();
              for (const auto &sh : stateOpt->stripHashes) {
                auto blobOpt = versionStore_->getStripBlob(sh);
                if (blobOpt) {
                  juce::String newId = mixer_.addStrip();
                  if (auto *strip = mixer_.getStrip(newId)) {
                    strip->library = blobOpt->library;
                    strip->family = blobOpt->family;
                    strip->isSolo = blobOpt->isSolo;
                    strip->inputPort = blobOpt->inputPort;
                    strip->inputChannel = blobOpt->inputChannel;
                    strip->pluginUid = blobOpt->pluginUid;
                    strip->gainDb.store(blobOpt->gainDb,
                                        std::memory_order_relaxed);
                    setupStripListener(*strip);
                    if (!blobOpt->expressionMapEntityId.empty()) {
                      auto xd =
                          xmapLibrary_.load(blobOpt->expressionMapEntityId);
                      if (xd)
                        strip->setExpressionMap(xd);
                    }
                    if (strip->pluginUid != 0) {
                      for (const auto &d :
                           pluginScanner_.getKnownPluginList().getTypes()) {
                        if (d.uniqueId == strip->pluginUid) {
                          juce::MemoryBlock sb(blobOpt->pluginState.data(),
                                               blobOpt->pluginState.size());
                          strip->loadPlugin(
                              d, mixer_.getFormatManager(),
                              [strip, sb](bool ok) {
                                if (ok && sb.getSize() > 0 &&
                                    strip->pluginInstance)
                                  strip->pluginInstance->setStateInformation(
                                      sb.getData(), (int)sb.getSize());
                              });
                          break;
                        }
                      }
                    }
                  }
                }
              }
              saveAllStripsToDB();
              mixer_.syncStripsToInstruments(masterList_);
              pushMixerState(false);
              scheduleStateRebuild();
            }
          }
        }
        currentBranchId_ = newBranchId;
        stateManager_.setCurrentBranchId(newBranchId);
        // Creating a branch from a detached version attaches us to the new
        // branch.
        isDetached_ = false;
        pushBranches();
        pushDagHistory();
        broadcastMessage("setCurrentBranch", juce::String(newBranchId));
        broadcastMessage("setDetachedHead", false);
      });
    }
    return;
  });
  jsRouter_.registerHandler("checkoutBranch", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    if (args.size() > 0 && versionStore_) {
      std::string branchId = args[0].toString().toStdString();
      safeCallAsync([this, branchId]() {
        auto b = versionStore_->getStorage().getBranch(branchId);
        if (b) {
          currentBranchId_ = branchId;
          stateManager_.setCurrentBranchId(branchId);
          auto verOpt = versionStore_->getVersion(b->second);
          if (verOpt) {
            auto stateOpt = versionStore_->getState(verOpt->stateHash);
            if (stateOpt) {
              mixer_.clear();
              undoManager_.clear();
              for (const auto &sh : stateOpt->stripHashes) {
                auto blobOpt = versionStore_->getStripBlob(sh);
                if (blobOpt) {
                  juce::String newId = mixer_.addStrip();
                  if (auto *strip = mixer_.getStrip(newId)) {
                    strip->library = blobOpt->library;
                    strip->family = blobOpt->family;
                    strip->isSolo = blobOpt->isSolo;
                    strip->inputPort = blobOpt->inputPort;
                    strip->inputChannel = blobOpt->inputChannel;
                    strip->pluginUid = blobOpt->pluginUid;
                    strip->gainDb.store(blobOpt->gainDb,
                                        std::memory_order_relaxed);

                    std::cerr << "[checkoutBranch] Loaded strip " << strip->id
                              << " gainDb=" << blobOpt->gainDb << " db"
                              << std::endl;

                    setupStripListener(*strip);

                    if (!blobOpt->expressionMapEntityId.empty()) {
                      auto xmapData =
                          xmapLibrary_.load(blobOpt->expressionMapEntityId);
                      if (xmapData)
                        strip->setExpressionMap(xmapData);
                    }

                    if (strip->pluginUid != 0) {
                      for (const auto &d :
                           pluginScanner_.getKnownPluginList().getTypes()) {
                        if (d.uniqueId == strip->pluginUid) {
                          juce::MemoryBlock stateBlock(
                              blobOpt->pluginState.data(),
                              blobOpt->pluginState.size());
                          strip->loadPlugin(
                              d, mixer_.getFormatManager(),
                              [strip, stateBlock](bool success) {
                                if (success && stateBlock.getSize() > 0 &&
                                    strip->pluginInstance) {
                                  strip->pluginInstance->setStateInformation(
                                      stateBlock.getData(),
                                      (int)stateBlock.getSize());
                                  strip->refreshPluginStateCache();
                                }
                              });
                          break;
                        }
                      }
                    }
                  }
                }
              }
              saveAllStripsToDB();
              mixer_.syncStripsToInstruments(masterList_);
              pushMixerState(false);
              scheduleStateRebuild();
              currentVersionId_ = b->second; // head of the checked-out branch
              std::cerr << "[checkoutBranch] Checked out branch id: "
                        << branchId << std::endl;
            } else {
              std::cerr
                  << "[checkoutBranch] Failed to find state for branch id: "
                  << branchId << std::endl;
            }
          } else {
            std::cerr
                << "[checkoutBranch] Failed to find version for branch id: "
                << branchId << std::endl;
          }
        } else {
          std::cerr << "[checkoutBranch] Failed to find branch id: " << branchId
                    << std::endl;
        }
        pushBranches();
        pushDagHistory();
        broadcastMessage("setCurrentBranch", juce::String(branchId));
        pushCurrentVersion();
        // Switching to a branch always clears detached state.
        isDetached_ = false;
        broadcastMessage("setDetachedHead", false);
      });
    }
    return;
  });
  jsRouter_.registerHandler("checkoutVersion", [this](const juce::var& payload) {
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
        auto stateOpt = versionStore_->getState(verOpt->stateHash);
        if (!stateOpt) {
          std::cerr << "[checkoutVersion] State not found for: " << versionHash
                    << std::endl;
          return;
        }

        // Determine whether this version is the current HEAD of its branch.
        std::string branchId = verOpt->branchId;
        auto branchHead = versionStore_->getBranchHead(branchId);
        bool versionIsHead =
            branchHead.has_value() && *branchHead == versionHash;
        isDetached_ = !versionIsHead;

        currentVersionId_ = versionHash;

        if (!isDetached_) {
          // Normal (attached) checkout — update branch tracking.
          currentBranchId_ = branchId;
          stateManager_.setCurrentBranchId(branchId);
        }
        // Detached: leave currentBranchId_ untouched so Dorico blob stays
        // anchored to the live branch.

        mixer_.clear();
        undoManager_.clear();
        for (const auto &sh : stateOpt->stripHashes) {
          auto blobOpt = versionStore_->getStripBlob(sh);
          if (blobOpt) {
            juce::String newId = mixer_.addStrip();
            if (auto *strip = mixer_.getStrip(newId)) {
              strip->library = blobOpt->library;
              strip->family = blobOpt->family;
              strip->isSolo = blobOpt->isSolo;
              strip->active = blobOpt->active;
              strip->inputPort = blobOpt->inputPort;
              strip->inputChannel = blobOpt->inputChannel;
              strip->pluginUid = blobOpt->pluginUid;
              strip->gainDb.store(blobOpt->gainDb, std::memory_order_relaxed);
              setupStripListener(*strip);
              if (!blobOpt->expressionMapEntityId.empty()) {
                auto xd = xmapLibrary_.load(blobOpt->expressionMapEntityId);
                if (xd)
                  strip->setExpressionMap(xd);
              }
              if (strip->pluginUid != 0) {
                for (const auto &d :
                     pluginScanner_.getKnownPluginList().getTypes()) {
                  if (d.uniqueId == strip->pluginUid) {
                    juce::MemoryBlock sb(blobOpt->pluginState.data(),
                                         blobOpt->pluginState.size());
                    strip->loadPlugin(
                        d, mixer_.getFormatManager(), [strip, sb](bool ok) {
                          if (ok && sb.getSize() > 0 && strip->pluginInstance)
                            strip->pluginInstance->setStateInformation(
                                sb.getData(), (int)sb.getSize());
                        });
                    break;
                  }
                }
              }
            }
          }
        }
        saveAllStripsToDB();
        mixer_.syncStripsToInstruments(masterList_);
        pushMixerState(false);
        scheduleStateRebuild(); // no-op when detached
        pushBranches();
        pushDagHistory();
        if (!isDetached_)
          broadcastMessage("setCurrentBranch", juce::String(branchId));
        pushCurrentVersion();
        broadcastMessage("setDetachedHead", isDetached_);
      });
    }
    return;
  });
  jsRouter_.registerHandler("mergeBranch", [this](const juce::var& payload) {
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
          if (targetBranchId == currentBranchId_) {
            auto verOpt = versionStore_->getVersion(result.newHeadId);
            if (verOpt) {
              auto stateOpt = versionStore_->getState(verOpt->stateHash);
              if (stateOpt) {
                mixer_.clear();
                undoManager_.clear();
                for (const auto &sh : stateOpt->stripHashes) {
                  auto blobOpt = versionStore_->getStripBlob(sh);
                  if (blobOpt) {
                    juce::String newId = mixer_.addStrip();
                    if (auto *strip = mixer_.getStrip(newId)) {
                      strip->library = blobOpt->library;
                      strip->family = blobOpt->family;
                      strip->isSolo = blobOpt->isSolo;
                      strip->inputPort = blobOpt->inputPort;
                      strip->inputChannel = blobOpt->inputChannel;
                      strip->pluginUid = blobOpt->pluginUid;
                      strip->gainDb.store(blobOpt->gainDb,
                                          std::memory_order_relaxed);
                      setupStripListener(*strip);
                      if (!blobOpt->expressionMapEntityId.empty()) {
                        auto xd =
                            xmapLibrary_.load(blobOpt->expressionMapEntityId);
                        if (xd)
                          strip->setExpressionMap(xd);
                      }
                      if (strip->pluginUid != 0) {
                        for (const auto &d :
                             pluginScanner_.getKnownPluginList().getTypes()) {
                          if (d.uniqueId == strip->pluginUid) {
                            juce::MemoryBlock sb(blobOpt->pluginState.data(),
                                                 blobOpt->pluginState.size());
                            strip->loadPlugin(
                                d, mixer_.getFormatManager(),
                                [strip, sb](bool ok) {
                                  if (ok && sb.getSize() > 0 &&
                                      strip->pluginInstance)
                                    strip->pluginInstance->setStateInformation(
                                        sb.getData(), (int)sb.getSize());
                                });
                            break;
                          }
                        }
                      }
                    }
                  }
                }
                saveAllStripsToDB();
                mixer_.syncStripsToInstruments(masterList_);
                pushMixerState(false);
                scheduleStateRebuild();
              }
            }
          }
          pushBranches();
          pushDagHistory();
        }
        broadcastMessage("mergeResult", juce::var(obj));
      });
    }
    return;
  });
  jsRouter_.registerHandler("deleteVersion", [this](const juce::var& payload) {
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
              currentBranchId_ = std::get<0>(allBranches[0]);
              stateManager_.setCurrentBranchId(currentBranchId_);
              std::cerr << "[IPC] deleteVersion: branch auto-deleted; "
                           "switched to "
                        << currentBranchId_ << std::endl;
              broadcastMessage("setCurrentBranch",
                               juce::String(currentBranchId_));
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
  jsRouter_.registerHandler("saveConfig", [this](const juce::var& payload) {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { saveConfig(); });
    return;
  });
}

juce::String MainComponent::computeLibraryFingerprint() {
  // Build a deterministic string from all library instruments.
  // Sort by libraryId + entityId + isSolo + instanceNum for stability.
  auto instruments = db_.loadAllLibraryInstruments();
  std::sort(instruments.begin(), instruments.end(),
            [](const fiddle::LibraryInstrumentRow &a,
               const fiddle::LibraryInstrumentRow &b) {
              if (a.libraryId != b.libraryId)
                return a.libraryId < b.libraryId;
              if (a.entityId != b.entityId)
                return a.entityId < b.entityId;
              if (a.isSolo != b.isSolo)
                return a.isSolo < b.isSolo;
              return a.maxInstance() < b.maxInstance();
            });

  juce::String data;
  for (const auto &inst : instruments) {
    data += inst.libraryId + "|" + inst.entityId + "|" +
            (inst.isSolo ? "S" : "T") + "|" +
            inst.instanceNumsToString() + "|" + inst.exprMap + "|" +
            juce::String(inst.pluginUid) + "\n";
  }
  return juce::String(data.hashCode64());
}

void MainComponent::broadcastTemplateDirty() {
  juce::String current = computeLibraryFingerprint();
  bool dirty = current != lastInstalledFingerprint_;
  broadcastMessage("setTemplateDirty", juce::var(dirty));
}

} // namespace fiddle
