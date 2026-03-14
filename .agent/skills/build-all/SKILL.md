---
name: build-all
description: Build all pieces of the Fiddle project correctly. Use whenever you need to compile C++, rebuild the Svelte UI, or install the VST3 plugin. Explains the exact sequence and what each step does.
---

# FiddleServer Build System

## Overview

The Fiddle project has three independently buildable pieces, all rooted at `/Users/mhc/fiddle`:

| Piece | Source | Output |
|---|---|---|
| **Svelte UI** | `Source/Server/ui/` | `Source/Server/ui/dist/` |
| **C++ Server** | `Source/Server/*.cpp` | `build/FiddleServer_artefacts/Debug/FiddleServer.app` |
| **VST3 Plugin** | `Source/NativePlugin/*.cpp` | `build/FiddleNative_artefacts/VST3/Fiddle.vst3` |

> [!IMPORTANT]
> The Svelte `dist/` is **automatically copied into the `.app` bundle** by the CMake `FiddleServerUI` target on every `cmake --build`. You **must run `pnpm build` first** so that CMake has a fresh dist to copy. If you only rebuild C++ without running `pnpm build`, the bundle gets the old UI.

## Correct Build Sequence

### Quick rebuild (UI-only change)

When only `.svelte`, `.ts`, or `.css` files changed:

```bash
cd /Users/mhc/fiddle

# 1. Rebuild Svelte
cd Source/Server/ui && pnpm build && cd ../../..

# 2. Re-run cmake to sync dist into the .app bundle
cmake --build build --config Debug --target FiddleServerUI
```

No C++ recompile needed — the `FiddleServerUI` target copies `dist/` into the app.

### Full rebuild (C++ or UI changed)

```bash
cd /Users/mhc/fiddle

# 1. Rebuild Svelte UI
cd Source/Server/ui && pnpm build && cd ../../..

# 2. Build everything (FiddleServer + FiddleServerUI + plugin + tests)
cmake --build build --config Debug
```

CMake will:
- Run `FiddleServerUI` to sync the UI dist (always runs, even if C++ is unchanged)
- Incrementally compile any changed C++ files
- Link the new binary

### Full rebuild with VST3 plugin install

Use the existing `fiddle.sh` script, which also installs the plugin and launches the server:

```bash
cd /Users/mhc/fiddle
./fiddle.sh
```

This does: `pnpm build` → CMake `FiddleNative` → `FiddleServer` → copies `Fiddle.vst3` to `~/Library/Audio/Plug-Ins/VST3/` → launches the app.

## Component Details

### Svelte UI
- **Source**: `Source/Server/ui/src/`
- **Build**: `cd Source/Server/ui && pnpm build`
- **Output**: `Source/Server/ui/dist/` (index.html + hashed assets)
- **Install**: CMake `FiddleServerUI` target copies `dist/` → `.app/Contents/MacOS/ui/`
- **Package manager**: `pnpm` (not `npm`)

### C++ FiddleServer
- **Source**: `Source/Server/*.cpp`
- **Build**: `cmake --build build --config Debug`
- **Output**: `build/FiddleServer_artefacts/Debug/FiddleServer.app`
- **Launch**: `open build/FiddleServer_artefacts/Debug/FiddleServer.app`
  or `./build/FiddleServer_artefacts/Debug/FiddleServer.app/Contents/MacOS/FiddleServer`

### VST3 Plugin
- **Source**: `Source/NativePlugin/*.cpp`
- **Build**: `cmake --build build --config Debug --target FiddleNative`
- **Output**: `build/FiddleNative_artefacts/VST3/Fiddle.vst3`
- **Install**: `cp -R build/FiddleNative_artefacts/VST3/Fiddle.vst3 ~/Library/Audio/Plug-Ins/VST3/`
  After install, **Dorico must be restarted** to pick up the new plugin.

### Unit Tests
- **VersionStoreTest**: `cmake --build build --target VersionStoreTest && ./build/VersionStoreTest`
- **ExpressionMapTest**: `cmake --build build --target ExpressionMapTest && ./build/ExpressionMapTest`

## Common Pitfalls

| Symptom | Cause | Fix |
|---|---|---|
| Old UI shown after Svelte edit | Forgot to cmake after `pnpm build` | Run `cmake --build build --target FiddleServerUI` |
| **UI JS globals missing / IPC broken after C++/JS refactor** | **Ran `cmake --build` without running `pnpm build` first — the `.app` bundle gets the old `dist/`** | **Always run `pnpm build` before `cmake --build` when any `.svelte`/`.js` file changed** |
| Stale `.app` bundle running | Didn't kill old process | `pkill -f FiddleServer` before relaunching |
| **New JS/CSS not loading after rebuild (old asset hash in log)** | **macOS WKWebView has TWO persistent caches for `juce://` resources** | **Clear both, then relaunch:**<br>`rm -rf ~/Library/Caches/com.antigravity.fiddleserver`<br>`rm -rf ~/Library/WebKit/com.antigravity.fiddleserver` |
| `createdAt` / new field missing from UI JSON | Binary is stale | Check binary timestamp: `ls -la build/FiddleServer_artefacts/Debug/FiddleServer.app/Contents/MacOS/FiddleServer` |
| `InMemoryVersionStorage` is abstract | Added virtual method to `IVersionStorage` but not to the test mock | Implement the new method in `Source/Server/tests/InMemoryVersionStorage.h` |
| Dorico not seeing plugin changes | Plugin installed but Dorico not restarted | Quit and relaunch Dorico |

## Verifying the Built Bundle

Check that the `.app` bundle's `ui/index.html` references the latest asset hash:

```bash
cat build/FiddleServer_artefacts/Debug/FiddleServer.app/Contents/MacOS/ui/index.html
```

The `src=./assets/index-*.js` hash should match the hash in `Source/Server/ui/dist/assets/`.

To verify `createdAt` (or any new JSON key) is in the compiled binary:

```bash
strings build/FiddleServer_artefacts/Debug/FiddleServer.app/Contents/MacOS/FiddleServer | grep createdAt
```
