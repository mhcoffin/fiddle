#include "MainComponent.h"
#include "DoricoConfigGenerator.h"
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

MainComponent::MainComponent()
    : webComponent(
          juce::WebBrowserComponent::Options{}
              .withNativeIntegrationEnabled(true)
              .withResourceProvider(
                  [this](const juce::String &url) { return getResource(url); })
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
              .withNativeFunction(
                  "signalReady",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    std::cerr << "[WebView] Handshake: Native function "
                                 "'signalReady' called"
                              << std::endl;

                    std::vector<std::pair<juce::String, bool>> pending;
                    {
                      std::lock_guard<std::mutex> lock(logMutex);
                      webViewLoaded = true;
                      pending.swap(logQueue);
                    }
                    for (const auto &item : pending) {
                      pushLogMessage(item.first, item.second);
                    }

                    webComponent.evaluateJavascript(
                        "addLogMessage('<i>Server started and listening for "
                        "connections...</i>')");

                    // Send Version
                    if (auto *app = juce::JUCEApplication::getInstance()) {
                      webComponent.evaluateJavascript(
                          "setServerVersion('" + app->getApplicationVersion() +
                          "')");
                    }

                    // Push channel map (port/channel → instrument) to Timeline
                    {
                      juce::String mapJson = masterList_.getChannelMapAsJson();
                      juce::String mapCall =
                          "setInstrumentMap('" + escapeForJS(mapJson) + "')";
                      webComponent.evaluateJavascript(mapCall);
                    }

                    // Push cached plugin list (if any prior scan exists)
                    if (pluginScanner_.getPluginCount() > 0) {
                      juce::String json = pluginScanner_.getPluginListAsJson();
                      juce::String call =
                          "setPluginList('" + escapeForJS(json) + "')";
                      webComponent.evaluateJavascript(call);
                    }

                    completion(true);
                  })
              .withNativeFunction(
                  "nativeLog",
                  [](const juce::Array<juce::var> &args,
                     juce::WebBrowserComponent::NativeFunctionCompletion
                         completion) {
                    if (args.size() > 0)
                      std::cerr << "[JS NativeLog] " << args[0].toString()
                                << std::endl;
                    completion(true);
                  })
              .withNativeFunction(
                  "requestSetupData",
                  [this](const juce::Array<juce::var> &,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    std::cerr << "[Setup] requestSetupData called" << std::endl;

                    // Build and cache the escaped JS call on first use
                    if (cachedInstrCall_.isEmpty()) {
                      auto t0 = std::chrono::steady_clock::now();
                      juce::String instrJson =
                          instrumentBrowser_.getInstrumentsAsJson();
                      cachedInstrCall_ = "setDoricoInstruments('" +
                                         escapeForJS(instrJson) + "')";
                      auto t1 = std::chrono::steady_clock::now();
                      std::cerr << "[Setup] Built instrument call cache: "
                                << std::chrono::duration_cast<
                                       std::chrono::milliseconds>(t1 - t0)
                                       .count()
                                << "ms, " << cachedInstrCall_.length()
                                << " chars" << std::endl;
                    }

                    juce::MessageManager::callAsync([this]() {
                      webComponent.evaluateJavascript(cachedInstrCall_);
                    });

                    // Push saved selections to the UI
                    juce::String selJson = masterList_.getSlotsAsJson();
                    juce::String selCall = "setSelectedInstruments('" +
                                           escapeForJS(selJson) + "')";
                    juce::MessageManager::callAsync([this, selCall]() {
                      webComponent.evaluateJavascript(selCall);
                    });

                    // Push channel map (port/channel → instrument) to the UI
                    juce::String mapJson = masterList_.getChannelMapAsJson();
                    juce::String mapCall =
                        "setInstrumentMap('" + escapeForJS(mapJson) + "')";
                    juce::MessageManager::callAsync([this, mapCall]() {
                      webComponent.evaluateJavascript(mapCall);
                    });

                    completion(true);
                  })
              .withNativeFunction(
                  "saveSelectedInstruments",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 1) {
                      juce::MessageManager::callAsync([this]() {
                        webComponent.evaluateJavascript(
                            "setSaveResult('Error: no data')");
                      });
                      completion(true);
                      return;
                    }
                    juce::String json = args[0].toString();
                    if (masterList_.setSlotsFromJson(json)) {
                      masterList_.saveDefault();

                      // Generate Dorico config files
                      DoricoConfigGenerator generator;
                      auto slots = masterList_.getSlots();
                      auto assignments =
                          DoricoConfigGenerator::expandSlots(slots);
                      int numChannels = masterList_.totalSlotCount();

                      auto result = generator.generateAndInstallFiles(
                          assignments, numChannels,
                          instrumentBrowser_.getInstruments());
                      juce::String msg;
                      if (result.wasOk()) {
                        msg = "OK: Installed " +
                              juce::String((int)assignments.size()) +
                              " presets (" + juce::String(numChannels) +
                              " channels)";
                      } else {
                        msg = "Error: " + result.getErrorMessage();
                      }
                      juce::MessageManager::callAsync([this, msg]() {
                        webComponent.evaluateJavascript(
                            "setSaveResult('" + escapeForJS(msg) + "')");
                      });

                      // Update channel map for Timeline
                      juce::String mapJson2 = masterList_.getChannelMapAsJson();
                      juce::String mapCall2 =
                          "setInstrumentMap('" + escapeForJS(mapJson2) + "')";
                      juce::MessageManager::callAsync([this, mapCall2]() {
                        webComponent.evaluateJavascript(mapCall2);
                      });
                    } else {
                      juce::MessageManager::callAsync([this]() {
                        webComponent.evaluateJavascript(
                            "setSaveResult('Error: Invalid JSON')");
                      });
                    }
                    completion(true);
                  })
              .withNativeFunction(
                  "scanPlugins",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (pluginScanner_.isScanning()) {
                      completion(true);
                      return;
                    }
                    pushLogMessage(
                        "<b>[Plugins]</b> Scanning for VST3 plugins...");
                    pluginScanner_.scanAsync([this]() {
                      int count = pluginScanner_.getPluginCount();
                      pushLogMessage("<b>[Plugins]</b> Scan complete: " +
                                     juce::String(count) + " plugins found");
                      juce::String json = pluginScanner_.getPluginListAsJson();
                      juce::String call =
                          "setPluginList('" + escapeForJS(json) + "')";
                      webComponent.evaluateJavascript(call);
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "loadPlugin",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 1) {
                      completion(false);
                      return;
                    }
                    int uid = (int)args[0];
                    juce::String slotId = juce::String(uid);

                    // Find the PluginDescription by uniqueId
                    juce::PluginDescription desc;
                    bool found = false;
                    for (const auto &d :
                         pluginScanner_.getKnownPluginList().getTypes()) {
                      if (d.uniqueId == uid) {
                        desc = d;
                        found = true;
                        break;
                      }
                    }

                    if (!found) {
                      pushLogMessage(
                          "<b>[Plugins]</b> Plugin not found for UID: " +
                              slotId,
                          true);
                      completion(false);
                      return;
                    }

                    // Must run on message thread — native callbacks are on
                    // WebKit's thread
                    juce::MessageManager::callAsync([this, slotId, desc]() {
                      pushLogMessage("<b>[Plugins]</b> Loading: " + desc.name +
                                     "...");

                      pluginHost_.loadPlugin(
                          slotId, desc, [this, desc](bool success) {
                            if (success) {
                              pushLogMessage("<b>[Plugins]</b> Loaded: " +
                                             desc.name);
                              juce::String json =
                                  pluginHost_.getLoadedPluginsAsJson();
                              juce::String call = "setLoadedPlugins('" +
                                                  escapeForJS(json) + "')";
                              webComponent.evaluateJavascript(call);
                            } else {
                              pushLogMessage(
                                  "<b>[Plugins]</b> Failed to load: " +
                                      desc.name,
                                  true);
                            }
                          });
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "unloadPlugin",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 1) {
                      completion(false);
                      return;
                    }
                    juce::String slotId = args[0].toString();
                    juce::MessageManager::callAsync([this, slotId]() {
                      pluginHost_.unloadPlugin(slotId);
                      pushLogMessage("<b>[Plugins]</b> Unloaded slot: " +
                                     slotId);
                      juce::String json = pluginHost_.getLoadedPluginsAsJson();
                      juce::String call =
                          "setLoadedPlugins('" + escapeForJS(json) + "')";
                      webComponent.evaluateJavascript(call);
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "showPluginEditor",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 1) {
                      completion(false);
                      return;
                    }
                    juce::String slotId = args[0].toString();
                    juce::MessageManager::callAsync(
                        [this, slotId]() { pluginHost_.showEditor(slotId); });
                    completion(true);
                  })
              // ── Mixer native functions ──
              .withNativeFunction(
                  "addMixerStrip",
                  [this](const juce::Array<juce::var> &,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    juce::MessageManager::callAsync([this]() {
                      mixer_.addStrip();
                      pushMixerState();
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "removeMixerStrip",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 1) {
                      completion(false);
                      return;
                    }
                    juce::String stripId = args[0].toString();
                    juce::MessageManager::callAsync([this, stripId]() {
                      mixer_.removeStrip(stripId);
                      pushMixerState();
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "setStripName",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 2) {
                      completion(false);
                      return;
                    }
                    juce::String stripId = args[0].toString();
                    juce::String name = args[1].toString();
                    juce::MessageManager::callAsync([this, stripId, name]() {
                      if (auto *s = mixer_.getStrip(stripId)) {
                        s->name = name;
                        pushMixerState();
                      }
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "setStripInput",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 3) {
                      completion(false);
                      return;
                    }
                    juce::String stripId = args[0].toString();
                    int port = (int)args[1];
                    int channel = (int)args[2];
                    juce::MessageManager::callAsync(
                        [this, stripId, port, channel]() {
                          if (auto *s = mixer_.getStrip(stripId)) {
                            s->inputPort = port;
                            s->inputChannel = channel;
                            pushMixerState();
                          }
                        });
                    completion(true);
                  })
              .withNativeFunction(
                  "setStripPlugin",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 2) {
                      completion(false);
                      return;
                    }
                    juce::String stripId = args[0].toString();
                    int pluginUid = (int)args[1];

                    // Find PluginDescription by uid
                    juce::PluginDescription desc;
                    bool found = false;
                    for (const auto &d :
                         pluginScanner_.getKnownPluginList().getTypes()) {
                      if (d.uniqueId == pluginUid) {
                        desc = d;
                        found = true;
                        break;
                      }
                    }

                    if (!found) {
                      completion(false);
                      return;
                    }

                    juce::MessageManager::callAsync([this, stripId, desc]() {
                      if (auto *s = mixer_.getStrip(stripId)) {
                        s->loadPlugin(desc, mixer_.getFormatManager(),
                                      [this](bool success) {
                                        if (success) {
                                          pushMixerState();
                                        }
                                      });
                      }
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "showStripEditor",
                  [this](const juce::Array<juce::var> &args,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    if (args.size() < 1) {
                      completion(false);
                      return;
                    }
                    juce::String stripId = args[0].toString();
                    juce::MessageManager::callAsync([this, stripId]() {
                      if (auto *s = mixer_.getStrip(stripId))
                        s->showEditor();
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "requestPluginsState",
                  [this](const juce::Array<juce::var> &,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    juce::MessageManager::callAsync([this]() {
                      if (pluginScanner_.getPluginCount() > 0) {
                        juce::String json =
                            pluginScanner_.getPluginListAsJson();
                        webComponent.evaluateJavascript(
                            "setPluginList('" + escapeForJS(json) + "')");
                      }
                      juce::String loadedJson =
                          pluginHost_.getLoadedPluginsAsJson();
                      webComponent.evaluateJavascript("setLoadedPlugins('" +
                                                      escapeForJS(loadedJson) +
                                                      "')");
                    });
                    completion(true);
                  })
              .withNativeFunction(
                  "requestMixerState",
                  [this](const juce::Array<juce::var> &,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    juce::MessageManager::callAsync(
                        [this]() { pushMixerState(); });
                    completion(true);
                  })
              .withNativeFunction(
                  "getAvailableInputs",
                  [this](const juce::Array<juce::var> &,
                         juce::WebBrowserComponent::NativeFunctionCompletion
                             completion) {
                    juce::MessageManager::callAsync([this]() {
                      juce::String json = masterList_.getChannelMapAsJson();
                      juce::String call =
                          "setAvailableInputs('" + escapeForJS(json) + "')";
                      webComponent.evaluateJavascript(call);
                    });
                    completion(true);
                  })) {
  setupWebView();

  // WebView fills the whole window (no JUCE tabs)
  addAndMakeVisible(webComponent);

  // Load Dorico instrument browser
  if (instrumentBrowser_.loadFromDorico()) {
    pushLogMessage(
        "<b>[Setup]</b> Loaded " +
        juce::String((int)instrumentBrowser_.getInstruments().size()) +
        " instruments from Dorico");
  } else {
    pushLogMessage("<b>[Setup]</b> Could not load Dorico instruments", true);
  }

  // Load saved master instrument list
  if (masterList_.loadDefault()) {
    pushLogMessage("<b>[Setup]</b> Loaded " + juce::String(masterList_.size()) +
                   " saved instruments");
  }

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

  // 1. Try relative to the executable (Production/Bundle)
  // On macOS, the binary is in Contents/MacOS, so we go up 3 times to get to
  // the folder containing the app BUT the CMake post-build copies it next to
  // the binary inside MacOS for convenience, OR next to the .app bundle. Let's
  // try both.

  juce::File scriptFile = exeFile.getSiblingFile("scripts/default_fiddle.as");

  if (!scriptFile.exists()) {
    // Try Resources folder if in a macOS bundle
    scriptFile = exeFile.getParentDirectory().getSiblingFile(
        "Resources/scripts/default_fiddle.as");
  }

  // 2. Try Source Tree Fallback (Development)
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
    std::cerr << "[Scripting] Loading script: " << scriptFile.getFullPathName()
              << std::endl;
    if (scriptEngine->loadScript(scriptFile)) {
      pushLogMessage(
          "<b>[Scripting]</b> Loaded default_fiddle.as successfully");
    } else {
      std::cerr << "[Scripting] Error: Failed to load script" << std::endl;
    }
  } else {
    pushLogMessage("<b>[Scripting]</b> Could not find script. Tried executable "
                   "sibling and source root.",
                   true);
  }

  // Load Expression Map from .doricolib
  juce::File doricolibFile =
      exeFile.getSiblingFile("Fiddle_Universal.doricolib");
  if (!doricolibFile.exists()) {
    // Try Resources folder if in a macOS bundle
    doricolibFile = exeFile.getParentDirectory().getSiblingFile(
        "Resources/Fiddle_Universal.doricolib");
  }
  if (!doricolibFile.exists()) {
    // Try source tree fallback
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
      obj->setProperty("program", (int)event.program_change().program_number());
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
         pushLogMessage(
             "<b>[Tracker]</b> Note ON: " + juce::String((juce::int64)n.id()) +
             " (Ch " + juce::String((int)n.channel()) + ")");
         subnoteGenerator.onNoteStarted(n);
         scriptEngine->execute("void processNote(Note@)", (void *)&n);

         double triggerTimeMs =
             juce::Time::getMillisecondCounterHiRes() + 1000.0;
         // JUCE MidiMessage takes channels 1-16 to build valid MIDI byte
         // payload
         juce::MidiMessage msg = juce::MidiMessage::noteOn(
             (int)n.channel() + 1, (int)n.note_number(),
             (juce::uint8)n.start_velocity());
         // Route the message to the MixerModel.
         // n.channel() from Dorico protobuf is 1-16, but Mixer model tracks
         // uses 0-15.
         std::cerr << "[MainComponent] Routing Note ON (port " << n.port()
                   << ", ch " << n.channel() << ")" << std::endl;
         mixer_.routeNoteEvent((int)n.port(), (int)n.channel() - 1, msg,
                               triggerTimeMs);

         juce::String json = noteToJson(n);
         juce::String call = juce::String::formatted(
             "updateNoteState(%s, 'started')", json.toRawUTF8());
         std::cerr << "[MainComponent] Call: " << call << std::endl;
         juce::MessageManager::callAsync(
             [this, call]() { webComponent.evaluateJavascript(call); });
       },
       [this, noteToJson](const fiddle::Note &n) {
         pushLogMessage("<b>[Tracker]</b> Note OFF: " +
                        juce::String((juce::int64)n.id()));
         subnoteGenerator.onNoteEnded(n);

         double triggerTimeMs =
             juce::Time::getMillisecondCounterHiRes() + 1000.0;
         // JUCE MidiMessage takes channels 1-16 to build valid MIDI byte
         // payload
         juce::MidiMessage msg = juce::MidiMessage::noteOff(
             (int)n.channel() + 1, (int)n.note_number(), (juce::uint8)0);

         std::cerr << "[MainComponent] Routing Note OFF (port " << n.port()
                   << ", ch " << n.channel() << ")" << std::endl;
         mixer_.routeNoteEvent((int)n.port(), (int)n.channel() - 1, msg,
                               triggerTimeMs);

         juce::String json = noteToJson(n);
         juce::String call = juce::String::formatted(
             "updateNoteState(%s, 'ended')", json.toRawUTF8());
         std::cerr << "[MainComponent] Call: " << call << std::endl;
         juce::MessageManager::callAsync(
             [this, call]() { webComponent.evaluateJavascript(call); });
       },
       [this, noteToJson](const fiddle::Note &n) {
         juce::String json = noteToJson(n);
         juce::String call = juce::String::formatted(
             "updateNoteState(%s, 'updated')", json.toRawUTF8());
         juce::MessageManager::callAsync(
             [this, call]() { webComponent.evaluateJavascript(call); });
       },
       [this, midiEventToJson](const fiddle::MidiEvent &event,
                               uint64_t absoluteSamples, int oldCCVal) {
         juce::String json = midiEventToJson(event, absoluteSamples, oldCCVal);
         juce::String call =
             juce::String::formatted("pushMidiEvent(%s)", json.toRawUTF8());
         juce::MessageManager::callAsync(
             [this, call]() { webComponent.evaluateJavascript(call); });
       }});

  subnoteGenerator.setCallbacks(
      {[this](const fiddle::Subnote &s) {
         // Call into script
         scriptEngine->execute("void processSubnote(Subnote@)", (void *)&s);

         pushSubnoteToWebView(s);
         juce::MessageManager::callAsync([this, id = s.id()]() {
           webComponent.evaluateJavascript(juce::String::formatted(
               "updateNoteState({id: %llu}, 'subnote')", id));
         });
       },
       [this, noteToJson](const fiddle::Note &n) {
         pushLogMessage("<b>[Watchdog]</b> Note Timed Out: " +
                        juce::String((juce::int64)n.id()));
         juce::String json = noteToJson(n);
         juce::String call = juce::String::formatted(
             "updateNoteState(%s, 'ended')", json.toRawUTF8());
         juce::MessageManager::callAsync(
             [this, call]() { webComponent.evaluateJavascript(call); });
       }});

  server = std::make_unique<fiddle::MidiTcpServer>();
  server->onMessageReceived([this](const fiddle::MidiEvent &event) {
    // Force a log to the UI so we can see the flow
    pushLogMessage("<b>[Server]</b> Received Event Case: " +
                   juce::String((int)event.event_case()) +
                   " Ch: " + juce::String(event.channel()));

    noteTracker.processEvent(event);
    pushEventToWebView(event);

    // Log CC events with expression map context
    if (event.has_cc()) {
      int ch = event.channel();
      int ccNum = event.cc().controller_number();
      int ccVal = event.cc().controller_value();
      juce::String logMsg = "<b>[CC]</b> Ch " + juce::String(ch + 1) + " CC" +
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
        juce::String jsCall = "setChannelInstrument(" + juce::String(channel) +
                              ", '" + escapeForJS(juce::String(name)) + "')";
        juce::MessageManager::callAsync(
            [this, jsCall]() { webComponent.evaluateJavascript(jsCall); });
      }
    }

    // Dynamic Server Load Command (from VST Host)
    if (event.has_load_config()) {
      juce::String path = event.load_config().config_path();

      juce::MessageManager::callAsync([this, path]() {
        juce::File targetFile(path);
        if (targetFile.existsAsFile()) {
          pushLogMessage("<b>[Host]</b> Commanded config switch to " +
                         targetFile.getFileName());

          // 1. Wipe current strips
          mixer_.clear();

          // 2. Load new config
          currentConfigFile = targetFile;
          std::vector<juce::String> configLogs =
              FiddleConfig::load(pluginScanner_, mixer_, currentConfigFile);

          // 3. Output results
          for (const auto &log : configLogs) {
            pushLogMessage(log, false);
          }

          // 4. Update UI instantly
          pushMixerState();
        } else {
          pushLogMessage("<span style=\"color: red;\"><b>[Host Error]</b> "
                         "Requested config file not found: " +
                             targetFile.getFullPathName() + "</span>",
                         true);
        }
      });
    }

    lastSampleTime = event.timestamp_samples();
    lastSystemTime = juce::Time::getMillisecondCounter();
  });

  server->onConnectionChanged([this](bool connected, juce::String host) {
    juce::MessageManager::callAsync([this, connected, host]() {
      // Send explicit status to UI
      webComponent.evaluateJavascript(
          "setConnectionState(" + juce::String(connected ? "true" : "false") +
          ")");

      if (connected) {
        webComponent.evaluateJavascript(
            "addLogMessage('<span style=\"color: #03dac6\">[Connected: " +
            host + "]</span>')");
      } else {
        webComponent.evaluateJavascript("addLogMessage('<span style=\"color: "
                                        "#cf6679\">[Disconnected]</span>')");
      }
    });
  });

  server->onRawActivity([this](juce::String msg) {
    juce::MessageManager::callAsync([this, msg]() {
      webComponent.evaluateJavascript("addLogMessage('<small>" + msg +
                                      "</small>')");
    });
  });

  startTimer(20); // 20ms tick for subnotes
  setSize(800, 600);

  // Initialize audio device for driving VST3 plugins
  juce::String err = deviceManager.initialiseWithDefaultDevices(0, 2);
  if (err.isNotEmpty()) {
    std::cerr << "[Audio] Failed to initialize device manager: " << err
              << std::endl;
  } else {
    deviceManager.addAudioCallback(this);
  }

  // Establish initial config file location (fallback or last-saved)
  currentConfigFile = FiddleConfig::getConfigPath();

  // Defer config restore to after the constructor returns.
  // loadPlugin() uses createPluginInstanceAsync() which requires the message
  // loop to be running — calling it from the constructor deadlocks because
  // we're on the message thread and the loop hasn't started yet.
  juce::MessageManager::callAsync([this]() {
    std::vector<juce::String> configLogs =
        FiddleConfig::load(pluginScanner_, mixer_, currentConfigFile);
    {
      std::lock_guard<std::mutex> lock(logMutex);
      for (const auto &log : configLogs) {
        if (webViewLoaded) {
          pushLogMessage(log, false);
        } else {
          logQueue.push_back({log, false});
        }
      }
    }
    pushMixerState();
  });
}

MainComponent::~MainComponent() {
  std::cerr << "[MainComponent] Destructor Invoked. Saving Config..."
            << std::endl;
  try {
    FiddleConfig::save(pluginScanner_, mixer_, currentConfigFile);
    std::cerr << "[MainComponent] Config saved successfully to "
              << currentConfigFile.getFullPathName() << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[MainComponent] Exception during config save: " << e.what()
              << std::endl;
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

void MainComponent::pushMixerState() {
  juce::String json = mixer_.toJson();
  juce::String call = "setMixerState('" + escapeForJS(json) + "')";
  webComponent.evaluateJavascript(call);
}

void MainComponent::pushLogMessage(const juce::String &msg, bool isError) {
  std::lock_guard<std::mutex> lock(logMutex);
  if (!webViewLoaded) {
    if (logQueue.size() < 1000) {
      logQueue.push_back({msg, isError});
    }
    return;
  }

  juce::String escaped = escapeForJS(msg);
  std::cerr << "[WebView] pushLogMessage: " << msg.substring(0, 50) << "..."
            << std::endl;

  juce::MessageManager::callAsync([this, escaped, isError]() {
    webComponent.evaluateJavascript(
        juce::String::formatted("addLogMessage('%s', %s)", escaped.toRawUTF8(),
                                isError ? "true" : "false"));
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
  juce::String msg = juce::String::formatted(
      "<b>[Subnote]</b> Note: %d ID: %llu Offset: %llu %s",
      subnote.note_number(), subnote.id(), subnote.offset_samples(),
      subnote.is_last() ? "(Final)" : "");

  pushLogMessage(msg);
}

void MainComponent::timerCallback() {
  subnoteGenerator.tick(noteTracker.getSessionSamples());

  static int hbCounter = 0;
  if (++hbCounter % 50 == 0) { // Every 1 second (20ms * 50)
    juce::MessageManager::callAsync([this, val = hbCounter / 50]() {
      webComponent.evaluateJavascript("setHeartbeat(" + juce::String(val) +
                                      ")");
    });
  }
}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() { webComponent.setBounds(getLocalBounds()); }

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
  static int tickCounter = 0;
  if (++tickCounter % 100 == 0) {
    std::cerr << "[AudioCallback] Ticked " << tickCounter << " blocks"
              << std::endl;
  }

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

  // 2. Transmit the mixed audioBuffer to Dorico via Shared Memory IPC
  audioSharedMemory_.pushAudio(audioBuffer);

  // 3. Clear the local speaker buffer so FiddleServer doesn't play directly
  // through macOS CoreAudio.
  //    This forces us to listen ONLY through the Dorico Mixer return route!
  audioBuffer.clear();
}
} // namespace fiddle
