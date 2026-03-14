# Fiddle Project — Agent Instructions

## ⛔ NEVER use browser tabs, Playwright, or CDP to debug the UI

FiddleServer's UI runs inside a native **macOS WKWebView**, not Chrome. There is no CDP endpoint. Opening `localhost:9223` in a browser will always fail with `ERR_CONNECTION_REFUSED`.

## ✅ Use the TCP bridge to run JavaScript in the WebView

```bash
# Main window (Mixer / Setup)
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "<JS expression>"

# History window (open automatically on launch; port 9224)
node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js --port 9224 "<JS expression>"
```

Full docs: `.agent/skills/fiddle-webview-bridge/SKILL.md`

---

## Build sequence

Always `pnpm build` **before** `cmake --build` when any `.svelte`/`.js` file changed.

Always make sure the build is complete and correct before notifying the user.

Full docs: `.agent/skills/build-all/SKILL.md`



---

## IPC patterns (C++ ↔ JS)

Full docs: `.agent/skills/fiddle-ipc/SKILL.md`
