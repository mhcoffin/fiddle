---
name: build-all
description: Build Fiddle's Release server, UI, tests, and VST3 plugin using the canonical build tree.
---

# Fiddle build and launch

Run from the repository root. The canonical build tree is `build/release`.
Do not use the old Debug app directly under `build/` or any archived build.

## Server and UI

```sh
./fiddle.sh build
```

CMake automatically rebuilds the Svelte UI with pnpm and syncs it into the app;
it also builds/bundles the MCP resources. No separate manual UI build is needed.
The app is `build/release/FiddleServer_artefacts/Release/FiddleServer.app`.
The top-level `Fiddle.app` symlink points there. `./fiddle.sh path` prints the path.

For all default targets, run `./rebuild_all.sh`. Both build commands are
incremental and do not launch the app or install the plugin.

## Tests

```sh
./scripts/test.sh
```

See `docs/automated-testing.md` for labels and browser regressions.

## Launch and installation

Only restart the user's app when requested. Save work and quit normally before
running `./fiddle.sh open`; opening a running app will not reload a new binary.
Do not force-kill Fiddle or delete WebView caches as a routine build step.

The plugin is built separately with:

```sh
cmake --build build/release --config Release --target FiddleNative
```

Locate the actual `.vst3` output under `build/release/FiddleNative_artefacts/`.
Building does not install it. Only replace the installed plugin when authorized,
with Dorico closed; then restart Dorico to load it. `fiddle.sh` never installs
plugins or bumps the project version.

## Directory safety

Keep new configurations under `build/`. `build/_deps` supplies dependency sources
to active builds: never delete or relocate the entire `build/` tree for a routine
rebuild. Historical top-level build trees live in `archive/builds/`; their old
CMake caches are not relocatable. See `README.md` and `archive/README.md`.
