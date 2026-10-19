---
name: fiddle-webview-bridge
description: Run JavaScript in the running FiddleServer's WKWebView UI (main window on port 9223, History window on 9224) to read DOM/app state or drive controls. Use for live-app UI checks; use browser-tests/*.mjs for built-UI regression tests.
---

# Fiddle WebView JS bridge

FiddleServer's UI runs in a native macOS WKWebView, which has no Chrome DevTools
endpoint. `JsTestBridge` (`Source/Server/JsTestBridge.cpp`) is the scripted way to
reach the live app: it evaluates a JavaScript string on the JUCE message thread
and returns the result.

## Protocol

Raw TCP, not HTTP. Request: 4-byte little-endian length + UTF-8 script.
Response: 4-byte little-endian length + result. Objects and primitives come back
JSON-stringified; errors start with `ERROR:`.

| Window | Port | Available |
| --- | --- | --- |
| Main (Mixer / Setup) | 9223 | Whenever the app is running |
| History | 9224 | Only while the History window exists: restored at launch if it was open last session, otherwise after `openHistoryWindow()` |

The bridge listens on `127.0.0.1` only and has no authentication. The client
must run on the same Mac as the app; an agent in a VM or cloud container cannot
reach it through its own `localhost`.

## Usage

Run from the repository root. `--port` must come before the script.

```bash
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "document.title"
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js --port 9224 "<JS>"
```

Examples:

```bash
# Count mixer strips
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js \
  "document.querySelectorAll('.mixer-strip').length"

# Open History, wait for it to load, then query it
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js \
  "window.__JUCE__.backend.openHistoryWindow(); 'OK'"
sleep 4
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js --port 9224 \
  "Array.from(document.querySelectorAll('.timestamp')).map(e => e.textContent.trim()).join(' | ')"

# Click, then read the result in a separate call so the UI can update
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js \
  "document.querySelector('.branch-btn').click(); 'Clicked'"
```

The script times out after 6 s. A refused connection means the app is not
running, the target window does not exist, or the client is not on the Mac.

## Other ways to test the UI

- **Built-UI regressions:** `Source/Server/ui/browser-tests/*.mjs` use Playwright
  and Chrome against the built `dist/` with a fixture backend and no live
  Fiddle state. See `docs/automated-testing.md`.
- **App state without the DOM:** the MCP control tools (`docs/mcp-control.md`).
