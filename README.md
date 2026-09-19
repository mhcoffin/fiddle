# Fiddle

## Open the current app (macOS)

Double-click **Fiddle.app** at the top of this repository, or run:

```sh
./fiddle.sh
```

Both use `build/release/FiddleServer_artefacts/Release/FiddleServer.app`.
After a rebuild, save your work and quit Fiddle normally before opening it again;
opening an already-running app does not reload its executable.
`./fiddle.sh path` prints the exact location; `./fiddle.sh reveal` shows it in Finder.

The launcher no longer builds, force-quits the server, bumps versions, or installs
the Dorico plugin as a side effect.

## Build and test

```sh
./fiddle.sh build       # Build the Release server, UI, and bundled MCP resources
./rebuild_all.sh        # Configure/build all default Release targets incrementally
./scripts/test.sh       # Build and run the stable tests
```

CMake builds and bundles the Svelte UI automatically using pnpm. These commands
do not launch the app or install the VST3 plugin. Plugin installation is a
separate, deliberate step with Dorico closed.

## Build directory map

| Location | Purpose |
| --- | --- |
| `build/release/` | Current Release app, plugin, and tests; normal development default |
| `build/_deps/` | Shared dependency sources still used by Release and sanitizer builds |
| `build/safety-tsan/` | ThreadSanitizer diagnostic build |
| `build/ci/` | CI test build, created when needed |
| Other files directly in `build/` | Older Debug build retained in place because this tree also holds active dependencies; not the app to launch |
| `archive/builds/` | Retired top-level build experiments, preserved locally rather than deleted |

Keep new build configurations under `build/<configuration>/`, not at the repository
root. Do not delete the whole `build/` tree as a routine rebuild step.
See [archive notes](archive/README.md), [testing](docs/automated-testing.md),
[audio diagnostics](docs/audio-diagnostics.md), and [stem printing](docs/stem-printing.md).
