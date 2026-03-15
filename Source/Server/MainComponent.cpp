#include "MainComponent.h"
#include "DoricoConfigGenerator.h"
#include "ExpressionMapParser.h"
#include "FiddleConfig.h"
#include "ScriptBindings.h"
#include "midi_event.pb.h"
#include <chrono>
#include <cstdint>
#include <functional>
#include <google/protobuf/text_format.h>
#include <memory>
#include <scriptstdstring.h>
#include <string>

namespace fiddle {

juce::WebBrowserComponent::Options MainComponent::createWebOptions() {
  return juce::WebBrowserComponent::Options{}
      .withNativeIntegrationEnabled(true)
      .withResourceProvider(
          [this](const juce::String &url) { return getResource(url); })
      .withNativeFunction(
          "dispatchMessage",
          [this](
              const juce::Array<juce::var> &args,
              juce::WebBrowserComponent::NativeFunctionCompletion completion) {
            if (args.isEmpty() || !args[0].isObject()) {
              completion(true);
              return;
            }
            auto *obj = args[0].getDynamicObject();
            juce::String type = obj->getProperty("type").toString();
            juce::var payload = obj->getProperty("payload");

            safeCallAsync(
                [this, type, payload]() { handleJsMessage(type, payload); });
            completion(true);
          })
      .withUserScript(
          "if (window.__JUCE__ && window.__JUCE__.initialisationData "
          "&& window.__JUCE__.initialisationData.__juce__functions) {"
          "  var funcs = "
          "window.__JUCE__.initialisationData.__juce__functions;"
          "  funcs.forEach(function(name) {"
          "    if (window.__JUCE__.backend && "
          "!window.__JUCE__.backend[name]) {"
          "      window.__JUCE__.backend[name] = function() {"
          "        var args = Array.prototype.slice.call(arguments);"
          "        window.__JUCE__.backend.emitEvent('__juce__invoke', "
          "{"
          "          name: name, params: args, resultId: Date.now()"
          "        });"
          "      };"
          "    }"
          "  });"
          "}")

      // ── Mixer native functions ──

      // ── Branch / Version native functions ──

      ;
}

MainComponent::MainComponent(const juce::String &configName)
    : webComponent(createWebOptions()) {
  setupWebView();

  // Add webComponent with zero-size bounds so the native WKWebView peer
  // is created immediately and starts loading in parallel with C++ init.
  // Zero-size means the splash screen remains visible underneath.
  addAndMakeVisible(webComponent);
  webComponent.setBounds(0, 0, 0, 0);

  // Create debug window eagerly (hidden by default).
  // Native functions are registered inside DebugWindow's own constructor
  // to avoid JUCE Options move-semantics losing registrations.
  {
    juce::String root = juce::WebBrowserComponent::getResourceProviderRoot();

    DebugWindowCallbacks cbs;
    cbs.onSignalReady = []() {
      // No extra work needed — DebugWindow sets debugReady_ internally
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
          juce::String call = "setPluginList('" + escapeForJS(json) + "')";
          pushToDebugWindow(call);
        }
      });
    };

    debugWindow_ = std::make_unique<DebugWindow>(
        [this](const juce::String &url) { return getResource(url); },
        std::move(cbs));
    debugWindow_->loadDebugPage(root);

    // Wire geometry saving (actual restore happens in init step 7
    // after the database is opened).
    debugWindow_->setGeometrySaver([this]() { saveDebugWindowGeometry(); });
  }

  setSize(800, 600);

  // Config name is always "default" — the concept is internal only.
  configName_ = "default";

  // Kick off phased async initialization so the splash screen can paint
  // between steps.
  addInitMessage("Starting up...");
  safeCallAsync([this]() { runInitStep(0); });

  // Initialize the bridge for UI testing on the main window using port 9223
  jsTestBridge_ = std::make_unique<JsTestBridge>(webComponent, 9223);
}

void MainComponent::addInitMessage(const juce::String &msg) {
  initMessages_.add(msg);
  repaint();
}

void MainComponent::runInitStep(int step) {
  switch (step) {

  case 0:

    // Scan for expression maps in Dorico directories
    addInitMessage("Scanning expression maps...");
    xmapLibrary_.scanDefaultDirectories();
    safeCallAsync([this]() { runInitStep(1); });
    return;

  case 1:

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
    safeCallAsync([this]() { runInitStep(2); });
    return;

  case 2:

    // Ensemble is now loaded from DB after db_.open() (init step 7+).
    // This step is kept as a placeholder for the splash screen message.
    addInitMessage("Loading instrument selections...");
    safeCallAsync([this]() { runInitStep(3); });
    return;

  case 3:

  {
    addInitMessage("Initializing script engine...");
    scriptEngine = std::make_unique<ScriptEngine>();
    ScriptBindings::RegisterFiddleAPI(scriptEngine->getEngine());

    // Set up print callback for scripts
    SetPrintCallback([this](const std::string &msg) {
      pushLogMessage("<b>[Script]</b> " + juce::String::fromUTF8(msg.c_str()),
                     false);
    });

    scriptEngine->setMessageCallback(
        [this](const std::string &msg, bool isError) {
          pushLogMessage(juce::String::fromUTF8(msg.c_str()), isError);
        });

    // Load default script (robust path resolution)
    juce::File exeFile =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile);

    juce::File scriptFile = exeFile.getSiblingFile("scripts/default_fiddle.as");

    if (!scriptFile.exists()) {
      scriptFile = exeFile.getParentDirectory().getSiblingFile(
          "Resources/scripts/default_fiddle.as");
    }

    // Source Tree Fallback (Development)
    juce::File projectRoot = exeFile;
    for (int i = 0; i < 10; ++i) {
      if (projectRoot.getChildFile("Source").isDirectory())
        break;
      projectRoot = projectRoot.getParentDirectory();
    }

    if (!scriptFile.exists()) {
      scriptFile = projectRoot.getChildFile("scripts/default_fiddle.as");
    }

    if (scriptFile.exists()) {
      std::cerr << "[Scripting] Loading script: "
                << scriptFile.getFullPathName() << std::endl;
      if (scriptEngine->loadScript(scriptFile)) {
        pushLogMessage(
            "<b>[Scripting]</b> Loaded default_fiddle.as successfully");
      } else {
        std::cerr << "[Scripting] Error: Failed to load script" << std::endl;
      }
    } else {
      pushLogMessage(
          "<b>[Scripting]</b> Could not find script. Tried executable "
          "sibling and source root.",
          true);
    }
    safeCallAsync([this]() { runInitStep(4); });
    return;
  }

  case 4:

  {
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
    safeCallAsync([this]() { runInitStep(5); });
    return;
  }

  case 5:

    // Set up note/MIDI callbacks, server, and start listening
    addInitMessage("Starting MIDI server...");
    {
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
             scriptEngine->execute("void processNote(Note@)", (void *)&n);

             double triggerTimeMs = juce::Time::getMillisecondCounterHiRes() +
                                    mixer_.getPlaybackDelayMs();
             // JUCE MidiMessage takes channels 1-16. n.channel() from Dorico
             // is also 1-based (1-16), so pass directly (no +1).
             juce::MidiMessage msg = juce::MidiMessage::noteOn(
                 (int)n.channel(), (int)n.note_number(),
                 (juce::uint8)n.start_velocity());
             // Route the message to the MixerModel.
             // n.port() from Dorico is 0-based (matches strip inputPort directly).
             // n.channel() from Dorico is 1-based (1-16); strip inputChannel
             // is 0-based (0-15), so subtract 1.
             int notePort = (int)n.port();
             std::cerr << "[MainComponent] Routing Note ON (port=" << notePort
                       << ", ch=" << (int)n.channel() - 1 << ")" << std::endl;
             mixer_.routeNoteEvent(notePort, (int)n.channel() - 1, msg,
                                   triggerTimeMs);

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

             double triggerTimeMs = juce::Time::getMillisecondCounterHiRes() +
                                    mixer_.getPlaybackDelayMs();
             // JUCE MidiMessage takes channels 1-16 to build valid MIDI byte
             // payload
             // JUCE MidiMessage: channel 1-16, n.channel() is already 1-based.
             juce::MidiMessage msg = juce::MidiMessage::noteOff(
                 (int)n.channel(), (int)n.note_number(), (juce::uint8)0);

             int noteOffPort = (int)n.port();
             std::cerr << "[MainComponent] Routing Note OFF (port=" << noteOffPort
                       << ", ch=" << (int)n.channel() - 1 << ")" << std::endl;
             mixer_.routeNoteEvent(noteOffPort, (int)n.channel() - 1, msg,
                                   triggerTimeMs);

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
             juce::var eventVar = juce::JSON::fromString(
                 midiEventToJson(event, absoluteSamples, oldCCVal));
             safeCallAsync([this, eventVar]() {
               broadcastMessage("pushMidiEvent", eventVar);
             });
           }});

      subnoteGenerator.setCallbacks(
          {[this](const fiddle::Subnote &s) {
             // Call into script
             scriptEngine->execute("void processSubnote(Subnote@)", (void *)&s);

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
        // Force a log to the UI so we can see the flow
        pushLogMessage("<b>[Server]</b> Received Event Case: " +
                       juce::String((int)event.event_case()) +
                       " Ch: " + juce::String(event.channel()));

        noteTracker.processEvent(event);
        pushEventToWebView(event);

        // Forward CC events to VST plugins
        if (event.has_cc()) {
          int ch = event.channel();
          // event.port() from Dorico is 0-based (matches strip inputPort directly).
          // event.channel() from Dorico is 1-based; subtract 1 for strip inputChannel.
          int port = (int)event.port();
          int ccNum = event.cc().controller_number();
          int ccVal = event.cc().controller_value();

          // CC 120 (All Sound Off) or CC 123 (All Notes Off) → panic all strips
          if (ccNum == 120 || ccNum == 123) {
            mixer_.allNotesOff();
            std::cerr << "[MainComponent] Panic: CC " << ccNum
                      << " → allNotesOff on all strips" << std::endl;
          } else {
            // Route CC to matching strips (ch is 1-based from protobuf, mixer
            // uses 0-based)
            // ch from Dorico is 1-based; JUCE controllerEvent also uses 1-16.
            juce::MidiMessage ccMsg =
                juce::MidiMessage::controllerEvent(ch, ccNum, ccVal);
            mixer_.routeCCEvent(port, ch - 1, ccMsg);
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
          juce::String pluginConfigName =
              juce::String(event.load_config().config_path());
          juce::String pluginConfigVersion =
              juce::String(event.load_config().config_version());

          safeCallAsync([this, pluginConfigName, pluginConfigVersion]() {
            // Config name is always "default" — just push our status
            pushLogMessage("<b>[Host]</b> Plugin connected");

            // If plugin has a matching version, adopt it; otherwise push ours
            if (pluginConfigVersion.isNotEmpty() &&
                pluginConfigVersion != configVersion_) {
              // Plugin has a different version — check if we should restore it
              if (db_.hasConfigVersion(configName_, pluginConfigVersion)) {
                db_.loadConfigVersion(configName_, pluginConfigVersion);
                configVersion_ = pluginConfigVersion;
                stateManager_.setConfigVersion(configVersion_);
                loadStripsFromDB();
                mixer_.syncStripsToInstruments(masterList_);
                pushMixerState(false);
                scheduleStateRebuild();
                pushLogMessage("<b>[Host]</b> Restored version: " +
                               pluginConfigVersion);
              }
            }

            if (onConfigChanged)
              onConfigChanged(configName_, configVersion_);
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

            // Save as a named config version
            db_.saveConfig(configName.isNotEmpty() ? configName
                                                   : "Dorico Import");

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
                strip->inputPort = rs.inputPort;
                strip->inputChannel = rs.inputChannel;
                strip->pluginUid = rs.pluginUid;
                strip->gainDb.store(rs.gainDb, std::memory_order_relaxed);
                setupStripListener(*strip);

                // Restore expression map
                if (!rs.expressionMapEntityID.empty()) {
                  auto xmapData = xmapLibrary_.load(rs.expressionMapEntityID);
                  if (xmapData)
                    strip->expressionMap = xmapData;
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
    safeCallAsync([this]() { runInitStep(6); });
    return;

  case 6:
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
    safeCallAsync([this]() { runInitStep(7); });
    return;

  case 7:

  {
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
                std::make_unique<HistoryWindow>(createWebOptions());
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

    // Restore setup window if it was previously visible.
    {
      auto sws = db_.loadWindowSettings("setup");
      if (sws.visible) {
        safeCallAsync([this, sws]() {
          if (!setupWindow_) {
            setupWindow_ =
                std::make_unique<SetupWindow>(createWebOptions());
            setupWindowLoaded_ = false;
            juce::String root =
                juce::WebBrowserComponent::getResourceProviderRoot();
            setupWindow_->getWebView().goToURL(root +
                                               "index.html?view=setup");
            setupWindow_->setGeometrySaver(
                [this]() { saveSetupWindowGeometry(); });
            if (sws.width > 0 && sws.height > 0) {
              setupWindow_->restoreGeometry(sws.x, sws.y, sws.width,
                                            sws.height, true);
            } else {
              setupWindow_->setVisible(true);
            }
            std::cerr << "[SetupWindow] Restored from saved settings"
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

    // Restore latest version timestamp for the default config.
    {
      auto configs = db_.listConfigs();
      for (const auto &c : configs) {
        if (c.name == configName_) {
          configVersion_ = c.version;
          std::cerr << "[MainComponent] Restored version: " << configVersion_
                    << std::endl;
          break;
        }
      }
      // Fall back to most recent config regardless of name (migration)
      if (configVersion_.isEmpty() && !configs.empty()) {
        configVersion_ = configs[0].version;
        std::cerr << "[MainComponent] Fallback version from '"
                  << configs[0].name << "': " << configVersion_ << std::endl;
      }
      if (onConfigChanged)
        onConfigChanged(configName_, configVersion_);
    }

    // Initialize shadow state manager (creates shared memory file)
    stateManager_.initialize();
    stateManager_.setConfigName(configName_);
    stateManager_.setConfigVersion(configVersion_);

    // Pre-cache config_status for immediate push to clients.
    if (server && configName_.isNotEmpty()) {
      fiddle::MidiEvent msg;
      msg.set_timestamp_samples(0);
      auto *cs = msg.mutable_config_status();
      cs->set_config_name(configName_.toStdString());
      cs->set_config_version(configVersion_.toStdString());
      cs->set_dirty(false);
      cs->set_delay_ms(1000); // default, updated later
      server->setCachedConfigStatus(msg);
    }
    safeCallAsync([this]() { runInitStep(8); });
    return;
  }

  case 8:
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
    return;

  default:
    return;
  } // switch
}

void MainComponent::saveConfig() {
  if (configName_.isEmpty()) {
    pushLogMessage("<b>[Config]</b> Cannot save: no config name set");
    return;
  }
  saveAllStripsToDB();
  // Explicit save: create a new version in DB
  auto newVersion = db_.saveConfig(configName_);
  if (newVersion.isNotEmpty()) {
    configVersion_ = newVersion;
    stateManager_.setConfigVersion(configVersion_);
    stateManager_.clearDirty();
    undoManager_.markSavePoint();
    broadcastMessage("setDirtyState", false);
    pushLogMessage("<b>[Config]</b> Saved '" + configName_ +
                   "' v=" + newVersion);
    pushConfigStatus();
    if (onConfigChanged)
      onConfigChanged(configName_, configVersion_);
    // Reset plugin state hashes so unchanged state doesn't re-trigger dirty
    pluginStateHashes_.clear();
  }

  // Commit a new DAG version on the currently-checked-out branch so the
  // History window reflects the save.
  if (versionStore_ && !currentBranchId_.empty()) {
    stateManager_.commitCurrentState(mixer_, currentBranchId_);
    // Track the new head as the current version.
    auto headOpt = versionStore_->getBranchHead(currentBranchId_);
    if (headOpt)
      currentVersionId_ = *headOpt;
    // pushBranches() must come before pushDagHistory() so the frontend's
    // headHashes set is up-to-date when it processes the new version list.
    pushBranches();
    pushDagHistory();
    pushCurrentVersion();
  }

  // Rebuild state blob without re-marking dirty
  stateManager_.scheduleRebuild([this]() -> juce::MemoryBlock {
    return stateManager_.buildStateBlob(mixer_);
  });
}

void MainComponent::saveConfigAs(const juce::String &newName) {
  // Save as a named config in the database
  configName_ = newName;
  stateManager_.setConfigName(configName_);
  saveAllStripsToDB();
  auto newVersion = db_.saveConfig(configName_);
  if (newVersion.isNotEmpty()) {
    configVersion_ = newVersion;
    stateManager_.setConfigVersion(configVersion_);
    stateManager_.clearDirty();
    pushConfigStatus();
  }
}

void MainComponent::pushConfigStatus() {
  if (!server)
    return;

  fiddle::MidiEvent msg;
  msg.set_timestamp_samples(0);
  auto *cs = msg.mutable_config_status();
  cs->set_config_name(configName_.toStdString());
  cs->set_config_version(configVersion_.toStdString());
  cs->set_dirty(stateManager_.isDirty());
  cs->set_delay_ms(mixer_.getPlaybackDelayMs());

  // Cache for immediate push on future client connections
  server->setCachedConfigStatus(msg);
  server->sendToClient(msg);
}

void MainComponent::clearForNewConfig() {
  // Clear mixer strips and undo history
  mixer_.clear();
  undoManager_.clear();

  // Clear the DB strips table so we start fresh
  db_.clearStrips();

  // Sync with instruments to create default empty strips
  mixer_.syncStripsToInstruments(masterList_);

  // Update UI
  pushMixerState(false);
  scheduleStateRebuild();

  std::cerr << "[MainComponent] Cleared for new config" << std::endl;
}

void MainComponent::loadConfigByName(const juce::String &name) {
  // Load a named config from the database
  mixer_.clear();
  undoManager_.clear();

  if (db_.loadConfig(name)) {
    loadStripsFromDB();
  }

  configName_ = name;
  configVersion_ = {};
  stateManager_.setConfigName(configName_);
  stateManager_.setConfigVersion(configVersion_);
  if (onConfigChanged)
    onConfigChanged(configName_, configVersion_);

  pushMixerState(false);
  mixer_.syncStripsToInstruments(masterList_);
  pushMixerState(false);
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
      strip->inputPort = row.inputPort;
      strip->inputChannel = row.inputChannel;
      strip->gainDb.store(row.gainDb, std::memory_order_relaxed);

      // Wire up plugin change listener for dirty detection
      setupStripListener(*strip);

      // Restore expression map
      if (!row.expressionMapEntityID.empty()) {
        auto data = xmapLibrary_.load(row.expressionMapEntityID);
        if (data) {
          strip->expressionMap = data;
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
    std::cerr << "[MainComponent] State saved successfully to SQLite ("
              << configName_ << ")" << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[MainComponent] Exception during config save: " << e.what()
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

  // Persist setup window geometry on quit.
  if (setupWindow_) {
    fiddle::WindowSettings ws;
    ws.windowId = "setup";
    auto bounds = setupWindow_->getBounds();
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = setupWindow_->isVisible();
    db_.saveWindowSettings(ws);
  }

  stopTimer();
  deviceManager.removeAudioCallback(this);
  server.reset();
}

void MainComponent::setupWebView() {
  juce::File current =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  // 1. Try relative to the executable (Production/Bundle)
  uiDir = current.getSiblingFile("ui");

  if (!uiDir.exists()) {
    // Try Resources folder if in a macOS bundle
    uiDir = current.getParentDirectory().getSiblingFile("Resources/ui");
  }

  if (!uiDir.exists()) {
    // 2. Try Source Tree Fallback (Development)
    juce::File projectRoot;
    juce::File searchDir = current;
    for (int i = 0; i < 10; ++i) {
      if (searchDir.getChildFile("Source").isDirectory()) {
        projectRoot = searchDir;
        break;
      }
      searchDir = searchDir.getParentDirectory();
    }

    if (projectRoot != juce::File()) {
      uiDir = projectRoot.getChildFile("Source/Server/ui/dist");
    }
  }

  if (!uiDir.exists()) {
    std::cerr
        << "[WebView] Error: UI directory not found. WebView will be empty."
        << std::endl;
  }

  webViewLoaded = false;
  juce::String root = juce::WebBrowserComponent::getResourceProviderRoot();
  std::cerr << "[WebView] Navigating to: " << root << "index.html" << std::endl;
  webComponent.goToURL(root + "index.html");
}

void MainComponent::pushMixerState(bool markDirty) {
  if (!webViewLoaded)
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
  if (!webViewLoaded) {
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

juce::String MainComponent::escapeForJS(const juce::String &str) {
  juce::String out;
  out.preallocateBytes((size_t)str.length() + 256);
  for (auto c : str) {
    switch (c) {
    case '\\':
      out += "\\\\";
      break;
    case '\'':
      out += "\\'";
      break;
    case '"':
      out += "\\\"";
      break;
    case '\r':
      out += "\\r";
      break;
    case '\n':
      out += "\\n";
      break;
    default:
      out += c;
      break;
    }
  }
  return out;
}

std::optional<juce::WebBrowserComponent::Resource>
MainComponent::getResource(const juce::String &url) {
  juce::String path = (url == "/" || url == "") ? "index.html" : url;
  if (path.startsWith("/"))
    path = path.substring(1);

  // Strip query parameters and fragment identifiers
  int qMark = path.indexOf("?");
  if (qMark >= 0)
    path = path.substring(0, qMark);
  int hash = path.indexOf("#");
  if (hash >= 0)
    path = path.substring(0, hash);
  if (path.isEmpty())
    path = "index.html";

  juce::File resourceFile = uiDir.getChildFile(path);
  std::cerr << "[WebView] getResource: " << url << " -> "
            << resourceFile.getFullPathName()
            << (resourceFile.exists() ? " (FOUND)" : " (NOT FOUND)")
            << std::endl;

  if (resourceFile.existsAsFile()) {
    juce::MemoryBlock mb;
    if (resourceFile.loadFileAsData(mb)) {
      juce::String mimeType = "application/octet-stream";
      if (path.endsWith(".html"))
        mimeType = "text/html; charset=utf-8";
      else if (path.endsWith(".js"))
        mimeType = "text/javascript";
      else if (path.endsWith(".css"))
        mimeType = "text/css";
      else if (path.endsWith(".svg"))
        mimeType = "image/svg+xml";
      else if (path.endsWith(".png"))
        mimeType = "image/png";

      std::vector<std::byte> data;
      data.resize(mb.getSize());
      std::memcpy(data.data(), mb.getData(), mb.getSize());
      return juce::WebBrowserComponent::Resource{std::move(data),
                                                 mimeType.toStdString()};
    }
  }

  return std::nullopt;
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
    historyWindow_ = std::make_unique<HistoryWindow>(createWebOptions());
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

void MainComponent::toggleSetupWindow() {
  // Lazily create the Setup window on first toggle.
  if (!setupWindow_) {
    setupWindow_ = std::make_unique<SetupWindow>(createWebOptions());
    setupWindowLoaded_ = false;
    juce::String root =
        juce::WebBrowserComponent::getResourceProviderRoot();
    setupWindow_->getWebView().goToURL(root + "index.html?view=setup");
    setupWindow_->setGeometrySaver(
        [this]() { saveSetupWindowGeometry(); });

    // Restore saved geometry (if any).
    auto sws = db_.loadWindowSettings("setup");
    if (sws.width > 0 && sws.height > 0) {
      setupWindow_->restoreGeometry(sws.x, sws.y, sws.width, sws.height,
                                    false /* we toggle below */);
    }
  }

  if (setupWindow_) {
    bool wasVisible = setupWindow_->isVisible();
    bool nowVisible = !wasVisible;
    setupWindow_->setVisible(nowVisible);
    if (nowVisible) {
      setupWindow_->toFront(true);
    }

    auto bounds = setupWindow_->getBounds();
    fiddle::WindowSettings ws;
    ws.windowId = "setup";
    ws.x = bounds.getX();
    ws.y = bounds.getY();
    ws.width = bounds.getWidth();
    ws.height = bounds.getHeight();
    ws.visible = nowVisible;
    db_.saveWindowSettings(ws);
  }
}

bool MainComponent::isSetupWindowVisible() const {
  return setupWindow_ && setupWindow_->isVisible();
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

void MainComponent::saveSetupWindowGeometry() {
  if (!setupWindow_)
    return;
  fiddle::WindowSettings ws;
  ws.windowId = "setup";
  auto bounds = setupWindow_->getBounds();
  ws.x = bounds.getX();
  ws.y = bounds.getY();
  ws.width = bounds.getWidth();
  ws.height = bounds.getHeight();
  ws.visible = setupWindow_->isVisible();
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
    webComponent.setBounds(getLocalBounds());
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
        std::cerr << "[AudioDiag] Peak after mixer: " << peak
                  << " dB=" << juce::Decibels::gainToDecibels(peak)
                  << std::endl;
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

void MainComponent::broadcastJavascript(const juce::String &js) {
  if (webViewLoaded) {
    webComponent.evaluateJavascript(js);
  }
  if (historyWindow_ && historyWindowLoaded_) {
    historyWindow_->getWebView().evaluateJavascript(js);
  }
  if (setupWindow_ && setupWindowLoaded_) {
    setupWindow_->getWebView().evaluateJavascript(js);
  }
  // Include debug window so broadcastMessage reaches the Plugins/Timeline panels
  if (debugWindow_) {
    debugWindow_->evaluateJavascript(js);
  }
}

void MainComponent::broadcastMessage(const juce::String &type,
                                     const juce::var &data) {
  auto *envelope = new juce::DynamicObject();
  envelope->setProperty("type", type);
  envelope->setProperty("data", data);
  juce::String json = juce::JSON::toString(juce::var(envelope), true);
  juce::String js =
      "window.__dispatchFromCpp && window.__dispatchFromCpp(" + json + ")";
  broadcastJavascript(js);
}

void MainComponent::handleJsMessage(const juce::String &type,
                                    const juce::var &payload) {
  if (type == "signalReady") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();
    juce::String viewMode = "mixer";
    if (args.size() > 0) {
      viewMode = args[0].toString();
    }

    juce::WebBrowserComponent *targetWebComponent = &webComponent;
    bool isHistoryWindow = false;

    if (viewMode == "history" && historyWindow_) {
      targetWebComponent = &(historyWindow_->getWebView());
      historyWindowLoaded_ = true;
      isHistoryWindow = true;
      std::cerr << "[WebView] Handshake: History window ready" << std::endl;
    } else if (viewMode == "setup" && setupWindow_) {
      targetWebComponent = &(setupWindow_->getWebView());
      setupWindowLoaded_ = true;
      isHistoryWindow = true; // Reuse flag to skip main-window-only init
      std::cerr << "[WebView] Handshake: Setup window ready" << std::endl;
    } else {
      webViewLoaded = true;
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

    bool isSetupWindow = (viewMode == "setup");

    // Send Version
    if (auto *app = juce::JUCEApplication::getInstance()) {
      broadcastMessage("setServerVersion",
                       juce::var(app->getApplicationVersion()));
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
    }

    // Restore saved main window mode.
    if (!isHistoryWindow) {
      auto mws = db_.loadWindowSettings("main");
      juce::String mode = mws.mode;
      if (masterList_.isEmpty() && mode != "setup")
        mode = "setup";
      broadcastMessage("setMainMode", juce::var(mode));
    }

    if (isHistoryWindow && !isSetupWindow) {
      safeCallAsync([this]() {
        pushDagHistory();
        pushBranches();
        pushCurrentVersion();
      });
    }

    // Push setup data to the setup window
    if (isSetupWindow) {
      safeCallAsync([this]() {
        juce::String instrJson = instrumentBrowser_.getInstrumentsAsJson();
        broadcastMessage("setDoricoInstruments",
                         juce::JSON::fromString(instrJson));
        juce::String selJson = masterList_.getSlotsAsJson();
        broadcastMessage("setSelectedInstruments",
                         juce::JSON::fromString(selJson));
        juce::String mapJson = masterList_.getChannelMapAsJson();
        broadcastMessage("setInstrumentMap", juce::JSON::fromString(mapJson));
      });
    }

    // Push branches to main window
    if (!isHistoryWindow) {
      pushBranches();
    }

    // Reveal the WebView now that it's fully loaded
    if (!isHistoryWindow) {
      initComplete_ = true;
      webComponent.setBounds(getLocalBounds());
      repaint();
    }
    return;
  }
  if (type == "setMode") {
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
  }
  if (type == "nativeLog") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() > 0)
      std::cerr << "[JS NativeLog] " << args[0].toString() << std::endl;
    return;
  }
  if (type == "requestSetupData") {
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
  }
  if (type == "saveSelectedInstruments") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() < 1) {
      safeCallAsync([this]() {
        webComponent.evaluateJavascript("setSaveResult('Error: no data')");
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
        webComponent.evaluateJavascript("setSaveResult('" + escapeForJS(msg) +
                                        "')");
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
      juce::String mapCall2 =
          "setInstrumentMap('" + escapeForJS(mapJson2) + "')";
      safeCallAsync(
          [this, mapCall2]() { webComponent.evaluateJavascript(mapCall2); });

      // Push updated mixer state to UI
      pushMixerState();
    } else {
      safeCallAsync([this]() {
        webComponent.evaluateJavascript("setSaveResult('Error: Invalid JSON')");
      });
    }
    return;
  }
  if (type == "scanPlugins") {
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
      juce::String call = "setPluginList('" + escapeForJS(json) + "')";
      webComponent.evaluateJavascript(call);
      pushToDebugWindow(call);
    });
    return;
  }
  if (type == "rescanPlugins") {
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
      juce::String call = "setPluginList('" + escapeForJS(json) + "')";
      webComponent.evaluateJavascript(call);
      pushToDebugWindow(call);
    });
    return;
  }
  if (type == "addMixerStrip") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      auto action = std::make_unique<AddStripAction>(mixer_);
      undoManager_.perform(std::move(action));
      pushMixerState();
    });
    return;
  }
  if (type == "duplicateStripInput") {
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
  }
  if (type == "removeMixerStrip") {
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
  }
  if (type == "setStripLibrary") {
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
  }
  if (type == "setStripInput") {
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
  }
  if (type == "setStripGain") {
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
  }
  if (type == "setStripMute") {
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
  }
  if (type == "setStripSolo") {
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
  }
  if (type == "setStripPlugin") {
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
  }
  if (type == "showStripEditor") {
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
  }
  if (type == "loadExpressionMap") {
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
  }
  if (type == "clearExpressionMap") {
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
  }
  if (type == "loadExpressionMapFromFile") {
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
                             s->expressionMap = data;
                             s->expressionMapPath = file.getFullPathName();
                           }

                           safeCallAsync([this]() { pushMixerState(); });
                         });
    return;
  }
  if (type == "requestExpressionMaps") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      juce::String json = xmapLibrary_.toJson();
      broadcastMessage("setExpressionMaps", juce::JSON::parse(json));
    });
    return;
  }
  if (type == "undo") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.undo()) {
        pushMixerState();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          webComponent.evaluateJavascript(
              "if(window.setDirtyState)setDirtyState(false)");
          pushConfigStatus();
        }
      }
    });
    return;
  }
  if (type == "redo") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      if (undoManager_.redo()) {
        pushMixerState();
        if (undoManager_.isAtSavePoint()) {
          stateManager_.clearDirty();
          webComponent.evaluateJavascript(
              "if(window.setDirtyState)setDirtyState(false)");
          pushConfigStatus();
        }
      }
    });
    return;
  }
  if (type == "requestPluginsState") {
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
  }
  if (type == "requestMixerState") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { pushMixerState(false); });
    return;
  }
  if (type == "getAvailableInputs") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      juce::String json = masterList_.getChannelMapAsJson();
      broadcastMessage("setAvailableInputs", juce::JSON::parse(json));
    });
    return;
  }
  if (type == "setPlaybackDelay") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    if (args.size() >= 1) {
      int ms = static_cast<int>(args[0]);
      safeCallAsync([this, ms]() {
        mixer_.setPlaybackDelayMs(ms);
        if (configName_.isNotEmpty())
          pushConfigStatus();
        pushLogMessage("<b>[Mixer]</b> Playback delay set to " +
                       juce::String(ms) + " ms");
      });
    }
    return;
  }
  if (type == "getPlaybackDelay") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() {
      int ms = mixer_.getPlaybackDelayMs();
      broadcastMessage("setPlaybackDelay", juce::var(ms));
    });
    return;
  }
  if (type == "setGroupGainDelta") {
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
  }
  if (type == "setGroupGainAbsolute") {
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
  }
  if (type == "setGroupPlugin") {
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
  }
  if (type == "setGroupLibrary") {
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
  }
  if (type == "setGroupExpressionMap") {
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
  }
  if (type == "removeGroupStrips") {
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
  }
  if (type == "requestBranches") {
    safeCallAsync([this]() { pushBranches(); });
    return;
  }
  if (type == "requestCurrentBranch") {
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
  }
  if (type == "openHistoryWindow") {
    safeCallAsync([this]() {
      if (!historyWindow_ && versionStore_) {
        historyWindow_ = std::make_unique<HistoryWindow>(createWebOptions());
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
  }
  if (type == "requestDagHistory") {
    safeCallAsync([this]() { pushDagHistory(); });
    return;
  }
  if (type == "createBranch") {
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
                        strip->expressionMap = xd;
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
  }
  if (type == "checkoutBranch") {
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
                        strip->expressionMap = xmapData;
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
  }
  // ── checkoutVersion ────────────────────────────────────────────────────────
  // args[0] = version UUID.  Loads that version's state into the mixer.
  // If the version is NOT the head of its branch, enters "detached HEAD" mode:
  //   - currentBranchId_ is left unchanged so Dorico state stays anchored.
  //   - scheduleStateRebuild() is a no-op while detached.
  //   - The UI shows a badge and disables Save.
  if (type == "checkoutVersion") {
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
              strip->inputPort = blobOpt->inputPort;
              strip->inputChannel = blobOpt->inputChannel;
              strip->pluginUid = blobOpt->pluginUid;
              strip->gainDb.store(blobOpt->gainDb, std::memory_order_relaxed);
              setupStripListener(*strip);
              if (!blobOpt->expressionMapEntityId.empty()) {
                auto xd = xmapLibrary_.load(blobOpt->expressionMapEntityId);
                if (xd)
                  strip->expressionMap = xd;
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
  }
  // ── mergeBranch ────────────────────────────────────────────────────────────
  // args[0] = sourceBranchId, args[1] = targetBranchId.
  // Merges source onto target.  If target is the current branch, reloads mixer.
  if (type == "mergeBranch") {
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
                          strip->expressionMap = xd;
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
  }
  // ── squashVersion ──────────────────────────────────────────────────────────
  // args[0] = version hash to squash.
  if (type == "deleteVersion") {
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
  }
  if (type == "saveConfig") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    safeCallAsync([this]() { saveConfig(); });
    return;
  }
  if (type == "cancelSetup") {
    // Hide the setup window and persist the closed state
    safeCallAsync([this]() {
      if (setupWindow_) {
        setupWindow_->setVisible(false);
        saveSetupWindowGeometry();
      }
    });
    return;
  }
  if (type == "openSetupWindow") {
    safeCallAsync([this]() {
      // If already open, just bring to front
      if (setupWindow_ && setupWindow_->isVisible()) {
        setupWindow_->toFront(true);
        return;
      }
      toggleSetupWindow();
    });
    return;
  }
  if (type == "saveSetup") {
    juce::Array<juce::var> args;
    if (payload.isArray())
      args = *payload.getArray();

    // Trigger the save flow — target the setup window's WebView
    safeCallAsync([this]() {
      if (setupWindow_ && setupWindowLoaded_) {
        setupWindow_->getWebView().evaluateJavascript(
            "if(window.triggerSaveSetup)window.triggerSaveSetup()");
      } else {
        // Fallback: try main window
        webComponent.evaluateJavascript(
            "if(window.triggerSaveSetup)window.triggerSaveSetup()");
      }
    });
    return;
  }
  std::cerr << "[IPC] Unknown message type: " << type << std::endl;
}

} // namespace fiddle
