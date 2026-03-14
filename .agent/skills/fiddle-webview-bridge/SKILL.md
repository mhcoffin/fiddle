---
name: fiddle-webview-bridge
description: Automates UI testing and DOM verification for FiddleServer's native macOS WKWebView by communicating over a custom C++ TCP bridge on port 9223 (main window) and 9224 (History window).
---

# Fiddle WebView JS Bridge

## ⚠️ CRITICAL: Do NOT Use Chrome, Playwright, or CDP

FiddleServer's UI runs inside a **native macOS `WKWebView`**, not a Chrome/Chromium window.

> [!CAUTION]
> **Never** use `open_browser_url`, the Playwright browser subagent, `http://localhost:9223/json/list`, or any Chrome DevTools Protocol (CDP) approach. These will always fail with `ERR_CONNECTION_REFUSED` — there is no Chrome process, only a native Apple WebKit view that does not expose CDP.

The **only** way to run JavaScript in FiddleServer's UI is through the custom TCP bridge described below.

---

## How It Works

FiddleServer runs a custom C++ TCP server (`JsTestBridge`) that:
1. Accepts connections on a local port
2. Receives a 4-byte LE length-prefixed JavaScript string
3. Evaluates it on the JUCE message thread inside the `WKWebView`
4. Returns the result as a length-prefixed string

**Ports:**
| Window | Port |
|---|---|
| Main window (Mixer / Setup) | `9223` |
| History window | `9224` (only exists after History window is opened) |

---

## Usage

The `validate-webview.js` Node.js script handles the TCP framing. Always run from the repo root `/Users/mhc/fiddle`.

```bash
# Main window (default port 9223)
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "<JS expression>"

# History window — --port MUST come BEFORE the JS argument
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js --port 9224 "<JS expression>"
```

### Output format
```
[Port 9223] Evaluating JavaScript: document.title
--- RESULT ---
OK:"Fiddle"
--------------
```
Objects and primitives are JSON-stringified. Errors begin with `ERROR:`.

---

## Examples

**Verify the app is running:**
```bash
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "document.title"
```

**Count mixer strips:**
```bash
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "document.querySelectorAll('.mixer-strip').length"
```

**Open the History window, then query it (two separate commands):**
```bash
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "window.__JUCE__.backend.openHistoryWindow(); 'OK'"
sleep 4
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js --port 9224 \
  "document.querySelectorAll('.timestamp').length + ' rows'"
```

**Inspect multiple DOM values:**
```bash
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js \
  "Array.from(document.querySelectorAll('.timestamp')).map(e => e.textContent.trim()).join(' | ')"
```

**Click a button (check DOM in a follow-up command):**
```bash
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js \
  "document.querySelector('.branch-btn').click(); 'Clicked'"
```

---

## Common Mistakes

| Mistake | Why it fails |
|---|---|
| `open_browser_url('http://localhost:9223')` | Not an HTTP server — raw TCP socket only |
| Playwright / browser subagent connecting to port 9223/9224 | Not a CDP endpoint; always refuses |
| Querying port 9224 before History window is open | Port 9224 only exists after `openHistoryWindow()` is called |
| `--port 9224` placed after the JS arg | `--port` **must** come before the JS argument |

---

## Files
- `SKILL.md`: This file.
- `scripts/validate-webview.js`: Node.js TCP client handling length-prefixed framing.
