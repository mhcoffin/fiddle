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
  addAndMakeVisible(webComponent);

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

  if (!scriptFile.exists()) {
    // 2. Try Source Tree Fallback (Development)
    juce::File projectRoot = exeFile;
    for (int i = 0; i < 10; ++i) {
      if (projectRoot.getChildFile("Source").isDirectory())
        break;
      projectRoot = projectRoot.getParentDirectory();
    }
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

  noteTracker.setCallbacks(
      {[this](const fiddle::Note &n) {
         subnoteGenerator.onNoteStarted(n);
         // Call into script
         scriptEngine->execute("void processNote(Note@)", (void *)&n);

         juce::MessageManager::callAsync([this, n]() {
           webComponent.evaluateJavascript(juce::String::formatted(
               "updateNoteState(%llu, %d, %d, 'started')", n.id(),
               n.note_number(), n.channel()));
         });
       },
       [this](const fiddle::Note &n) {
         subnoteGenerator.onNoteEnded(n);
         juce::MessageManager::callAsync([this, n]() {
           webComponent.evaluateJavascript(juce::String::formatted(
               "updateNoteState(%llu, 0, 0, 'ended')", n.id()));
         });
       }});

  subnoteGenerator.setCallbacks({[this](const fiddle::Subnote &s) {
    // Call into script
    scriptEngine->execute("void processSubnote(Subnote@)", (void *)&s);

    pushSubnoteToWebView(s);
    juce::MessageManager::callAsync([this, s]() {
      webComponent.evaluateJavascript(juce::String::formatted(
          "updateNoteState(%llu, 0, 0, 'subnote')", s.id()));
    });
  }});

  server = std::make_unique<fiddle::MidiTcpServer>();
  server->onMessageReceived([this](const fiddle::MidiEvent &event) {
    std::cerr << "[MidiTcpServer] Received MIDI event on channel "
              << event.channel() << std::endl;
    noteTracker.processEvent(event);
    pushEventToWebView(event);

    // Update time tracking
    lastSampleTime = event.timestamp_samples();
    lastSystemTime = juce::Time::getMillisecondCounter();
  });

  server->onConnectionChanged([this](bool connected, juce::String host) {
    juce::MessageManager::callAsync([this, connected, host]() {
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
  if (lastSystemTime == 0)
    return;

  // Estimate current sample time based on system time elapsed
  uint32_t now = juce::Time::getMillisecondCounter();
  uint32_t elapsedMs = now - lastSystemTime;
  uint64_t elapsedSamples =
      static_cast<uint64_t>(elapsedMs * (44100.0 / 1000.0));

  subnoteGenerator.tick(lastSampleTime + elapsedSamples);
}

void MainComponent::paint(juce::Graphics &g) {
  g.fillAll(
      getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::resized() { webComponent.setBounds(getLocalBounds()); }

} // namespace fiddle
