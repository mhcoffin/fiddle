#include "WebViewBridge.h"
#include <iostream>

namespace fiddle {

WebViewBridge::WebViewBridge(MessageRouter& router, std::function<void(std::function<void()>)> asyncRunner)
    : router_(router), asyncRunner_(std::move(asyncRunner)), mainWebComponent_(createWebOptions()) {
}

juce::WebBrowserComponent::Options WebViewBridge::createWebOptions() {
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

            if (asyncRunner_) {
                asyncRunner_([this, type, payload]() { router_.handleMessage(type, payload); });
            } else {
                router_.handleMessage(type, payload);
            }
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
          "}");
}

void WebViewBridge::setup() {
  juce::File current =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  // 1. Try relative to the executable (Production/Bundle)
  uiDir_ = current.getSiblingFile("ui");

  if (!uiDir_.exists()) {
    // Try Resources folder if in a macOS bundle
    uiDir_ = current.getParentDirectory().getSiblingFile("Resources/ui");
  }

  if (!uiDir_.exists()) {
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
      uiDir_ = projectRoot.getChildFile("Source/Server/ui/dist");
    }
  }

  if (!uiDir_.exists()) {
    std::cerr
        << "[WebView] Error: UI directory not found. WebView will be empty."
        << std::endl;
  }

  webViewLoaded_ = false;
  juce::String root = juce::WebBrowserComponent::getResourceProviderRoot();
  std::cerr << "[WebView] Navigating to: " << root << "index.html" << std::endl;
  mainWebComponent_.goToURL(root + "index.html");
}

std::optional<juce::WebBrowserComponent::Resource>
WebViewBridge::getResource(const juce::String &url) {
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

  juce::File resourceFile = uiDir_.getChildFile(path);
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

juce::String WebViewBridge::escapeForJS(const juce::String &str) {
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

void WebViewBridge::broadcastJavascript(const juce::String &js, 
                                        juce::WebBrowserComponent* historyWebView, 
                                        juce::WebBrowserComponent* libraryWebView) {
  if (webViewLoaded_) {
    mainWebComponent_.evaluateJavascript(js);
  }
  if (historyWebView) {
    historyWebView->evaluateJavascript(js);
  }
  if (libraryWebView) {
    libraryWebView->evaluateJavascript(js);
  }
}

} // namespace fiddle