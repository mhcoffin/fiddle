# Fiddle Project — Agent Instructions

## UI testing

- **Live app:** the UI is a native macOS WKWebView with no CDP endpoint. Run
  JavaScript in it through the TCP bridge (main window port 9223, History 9224):
  `node .agent/skills/fiddle-webview-bridge/scripts/validate-webview.js "<JS>"`.
  See `.agent/skills/fiddle-webview-bridge/SKILL.md`.
- **Built-UI regression tests:** `Source/Server/ui/browser-tests/*.mjs` run with
  Playwright against the built `dist/`; see `docs/automated-testing.md`.

---

## Score tests

A run ID like `basic-rhythm-20261021-143200` names a score test run: use the
`score-test-run` skill (`.claude/skills/score-test-run/SKILL.md`). See
`tests/score/README.md`.

---

## Build sequence

Always `pnpm build` **before** `cmake --build` when any `.svelte`/`.js` file changed.

Always make sure the build is complete and correct before notifying the user.

Full docs: `.agent/skills/build-all/SKILL.md`



---

## IPC patterns (C++ ↔ JS)

Full docs: `.agent/skills/fiddle-ipc/SKILL.md`
