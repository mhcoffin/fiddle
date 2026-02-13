#include "MainComponent.h"
#include "ScriptBindings.h"
#include "midi_event.pb.h"
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
                  })) {
  setupWebView();

  // Set up tabbed interface: Monitor (WebView) + Dorico Setup
  tabbedComponent.addTab("Monitor", juce::Colour(0xff1e1e1e), &webComponent,
                         false);
  tabbedComponent.addTab("Dorico Setup", juce::Colour(0xff1e1e1e), &doricoSetup,
                         false);
  addAndMakeVisible(tabbedComponent);

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
    obj->setProperty("startVelocity", (int)n.start_velocity());
    obj->setProperty("endVelocity", (int)n.end_velocity());
    obj->setProperty("startSample", (juce::int64)n.start_sample());
    obj->setProperty("durationSamples", (juce::int64)n.duration_samples());

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
}

MainComponent::~MainComponent() { server.reset(); }

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
  return str.replace("\\", "\\\\")
      .replace("'", "\\'")
      .replace("\"", "\\\"")
      .replace("\r", "\\r")
      .replace("\n", "\\n");
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

void MainComponent::resized() { tabbedComponent.setBounds(getLocalBounds()); }

} // namespace fiddle
