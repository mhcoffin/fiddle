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

Only restart the user's app when requested. First check it has no unsaved
changes and isn't playing (the MCP `fiddle_get_status`, or the agent-control
`status` method: `dirty`, `transportPlaying`). Then run `./fiddle.sh restart`,
which quits Fiddle normally and relaunches it in the background with
`--relaunched`, exactly as the app's Restart Fiddle does. Opening a running app
will not reload a new binary.

Don't relaunch with a plain `./fiddle.sh open` or `open`: that brings Fiddle
forward at once, so it saves wherever Stage Manager puts its windows as their
places, overwriting the user's layout. After `restart`, Fiddle waits in the
Stage Manager strip until the user brings it forward, then its windows return
to their saved places (`LaunchWindowPlacement.h`).

Do not force-kill Fiddle or delete WebView caches as a routine build step.

If a launched Fiddle seems hung while loading plug-ins, macOS is probably
asking for folder access, and the user has to answer the prompt. The build
signs FiddleServer with a stable certificate so that permission lasts across
builds; see `docs/code-signing.md`.

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
