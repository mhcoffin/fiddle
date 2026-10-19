# Fiddle

## Current bridge-map work

The authoritative plan is [Orchestral bridge maps](docs/orchestral-bridge-maps.md):
four section profiles using the hand-built base switches plus mutex-grouped
add-ons. Earlier universal-map and chair-specific Cartesian generation proposals
are superseded; existing diagnostics and historical test evidence are retained.
Supported bridge behavior is limited to **one Dorico voice per MIDI channel**.
With multiple voices sharing a channel, Dorico's output is treated as practically
unpredictable and exact agreement with direct Dorico playback is not promised.
The shared-voice diagnostics are historical and no longer block bridge work.
The current [strings r3 candidate](experiments/orchestral-strings-bridge/r3/README.md)
uses 32 add-on dimensions plus the base dimension (33 conduits, 19 spare),
with guitar/jazz extensions deferred to later profiles or customization.
The [woodwind and brass r1 candidates](experiments/orchestral-winds-bridge/README.md)
use 36 and 37 conduits respectively, with shared PT encodings matching strings.
The [Bridge maps workspace](docs/bridge-map-authoring.md) now imports, edits,
versions and assigns these maps, deriving each decoder from its stored source.
Dorico export verification is explicit; production host acceptance is pending.
**Chairs → Install Playback Template** now installs those pinned maps with matching
endpoint/preset references. See the [template smoke test](docs/bridge-map-authoring.md#template-smoke-test).

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
