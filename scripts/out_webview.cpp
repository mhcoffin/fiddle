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

void MainComponent::broadcastJavascript(const juce::String &js) {
  if (webViewLoaded) {
    webComponent.evaluateJavascript(js);
  }
  if (historyWindow_ && historyWindowLoaded_) {
    historyWindow_->getWebView().evaluateJavascript(js);
  }

  if (libraryManagerWindow_ && libraryManagerWindowLoaded_) {
    libraryManagerWindow_->getWebView().evaluateJavascript(js);
  }
  // Include debug window so broadcastMessage reaches the Plugins/Timeline panels
  if (debugWindow_) {
    debugWindow_->evaluateJavascript(js);
  }
}