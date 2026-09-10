# Automated testing

Status: testing foundation and application restore service implemented, September 2026.

Validated locally: all 53 stable CTest entries pass in Release, and the updated
Release FiddleServer/UI and FiddleNative build. Render-ahead requires both rebuilt
binaries; the previous native audio protocol is incompatible.

Audio diagnostics coverage includes deterministic block-budget timing, variable
block sizes, overrun/gap accounting, device restart, bounded queue saturation and
concurrent reads, plus the actual native audio consumer using an isolated mmap.
The relay integration test verifies one-second telemetry delivery on a background
thread alongside MIDI. UI tests cover absent/stale readings and bounded report
history. See [audio diagnostics](audio-diagnostics.md) for listening-test use.
The mixer integration executable contains twenty scenarios, including meter-only
snapshots that never query plug-in programs or capture state. UI tests verify that
meter updates cannot overwrite controls and the timer cannot rebuild full state.
Undo coverage includes shared history identities/gesture barriers, atomic mixer
changes, and project-setting restoration. The rendered browser fixture also
checks compensated mute, authoritative gain updates, Undo/Redo controls and
text-field shortcuts; see [repair status](ui-undo-repair-status.md).
Per-plugin timing tests cover bounded queues, block/rate changes, render/gap
accounting, and collection without program queries or state capture. An integration
scenario covers instruments and strip/bus/Master FX across re-preparation. Audio
settings tests use temporary preference files and a fake device backend, never
CoreAudio, to verify unchanged defaults, explicit settings, restart restoration,
and fallback without overwriting an unavailable saved device.
Render-ahead coverage adds virtual-time stall/recovery and concurrent ring tests,
plus a production-worker test for note timing and mmap generation replacement.
See [render-ahead design and rollout](audio-render-ahead.md).
The safety pass adds variable-block sample/MIDI continuity, undo disposal during
an in-flight render, concurrent inspector/map editing, cached-state snapshots,
and concurrent hosted-state capture. See [audio-safety-results.md](audio-safety-results.md)
for the control-boundary policy and optional ThreadSanitizer build.
Both it and the
version-store tests have passed 20 consecutive runs. GitHub execution remains to be
verified after pushing.

The regular regression suite runs without Dorico, an audio device, sample
libraries, or a human listener. Run it from any directory:

```sh
bash /Users/mhc/fiddle/scripts/test.sh
```

The script configures the build, builds the aggregate `FiddleTests` target,
then runs tests labelled `stable`. It reuses the existing build directory.
Set `FIDDLE_TEST_BUILD_DIR` for a separate build and `FIDDLE_TEST_JOBS` to
control compilation parallelism. A new build directory defaults to CMake's
default build type; CI explicitly configures Release.

For an already-built, focused run:

```sh
ctest --test-dir build --output-on-failure -R '^mixer_integration$'
ctest --test-dir build --output-on-failure -R '^mixer_integration$' --repeat until-fail:20
```

## What runs automatically

- All CMake executable targets ending in `Test` are built by `FiddleTests`.
  Add new native test targets before the aggregate-target declaration.
- Every `Source/Server/ui/tests/*.test.js` file is registered automatically.
  Node is required, so a missing installation cannot silently skip UI tests.
- Every CTest test has a bounded timeout.
- GitHub CI builds the same aggregate target, runs the stable suite, and keeps
  JUnit results and CTest failure logs as downloadable artefacts.
- Harmonic analysis remains separately labelled `known-failure`, with the
  existing expected-failure check. It is excluded from the stable suite.

The existing UI suite contains both real pure-function tests and source-text
contract checks. The latter do not prove that a button is clickable, that an
overlay behaves correctly, or that a rendered layout is usable.

### Optional rendered chair-layout regression

After building the UI, run:

```sh
node Source/Server/ui/browser-tests/chairLayout.mjs
```

This requires Playwright and an installed Chrome. Set `FIDDLE_PLAYWRIGHT_MODULE`
to an external Playwright package directory if it is not locally installed.
`FIDDLE_TEST_BROWSER_CHANNEL` can select another supported Chromium channel.
It launches an isolated headless browser and serves the built UI on an
OS-assigned loopback port, injecting fixture data through the normal UI message
receiver. It never connects to the running Fiddle server or its database.

The test changes the actual strip-size selector, measures single/multiple/empty
chair widths, verifies that headers and layer faders align, and clicks Add Layer
and Cancel at every size. Long names must not widen chairs. This optional test
also checks loaded/unassigned/missing instrument controls, editor and assignment
messages, library visibility, and the selector-to-editor transition. It
checks Edit/Hide updates from editor visibility notifications (including a
simulated native-window close), unchanged Save enablement, and equal 32-pixel
VSTi/Audio FX button heights. It
also opens Audio Performance with 30 fixture plugins, verifies row ordering and
fixed header/footer controls, copies a report with plugin/device metadata, and
checks the Audio Settings dispatch. It
is separate from CTest/CI until browser dependencies are provisioned there.
Set `FIDDLE_LAYOUT_SCREENSHOT_DIR` to an existing directory to capture each size.

## Production mixer integration

`MixerIntegrationTest` links production mixer, hosted-slot, effect-rack,
command/undo, state capture, version-store, and SQLite code. It constructs
deterministic MIDI-gated instruments and effects through normal processor
ownership and processing paths. It checks:

- two strips sum through a group effect exactly once, alongside a direct strip;
- bus mute/solo, strip mute/solo/activation, and Master gain affect the samples;
- invalid output destinations leave valid routing intact;
- the UI's Remove message reaches the real command service and removes the bus;
- repeated removal/undo/redo restores the same bus, position, routes, and effect;
- chair-layer removal/undo/redo restores its persisted assignment, original live
  player, FX, gain, strip order and bus route without resurrecting held notes;
- chair-layer addition/undo/redo keeps its original player and copied patch even
  after catalog edits, including Undo during asynchronous loading, missing
  players, and interleaved add/remove undo history;
- real delay processors on strip and bus paths align with direct-route samples;
- panic silences current notes and clears queued future note-ons;
- graceful stop silences notes even when the active-note tracker is incomplete,
  without allowing delayed note-ons to sound afterwards;
- captured versions retain bus order/state, strip routes, insert positions,
  bypass, and binary effect state after SQLite closes and reopens;
- saved-version selection finds the original version even after a newer save;
- state-file publication and decoding preserve the saved hash and ancestry;
- two explicitly isolated state endpoints do not read or overwrite one another.
- historical mixer saves fork from the loaded version, retain unchanged
  identity after edit/undo, and survive SQLite reopening independently of
  the original branch. See [save policy](project-save-versioning.md).
- the application restore service rebuilds chairs, layers, group buses, and
  strip/bus/Master effect racks after closing SQLite and destroying the original
  processors; the restored mixer renders the same samples and an unchanged save
  retains its version;
- saved layer/patch display metadata survives removal of the catalog patch;
- missing instruments retain state and remain silent; missing effects retain
  state and pass audio through; making them available restores the original audio;
- incomplete topology/blob references are rejected without clearing the mixer;
- superseded restores (including reused layer IDs and out-of-order completions),
  removed loading strips, and destruction of the restore service cannot publish
  stale completion callbacks or leave the completion barrier stuck;
- cancelling a queued host-state rebuild retains the previously published blob.

Each persistence run creates its own temporary directory, real SQLite database,
and state files, then removes only that directory on completion. It does not
start FiddleServer, bind its production ports, scan vendor plug-ins, open an
audio device, or write to the user's Fiddle database/cache. Existing network
tests use their own loopback test endpoints.

The MIDI server accepts an explicit bind address and port zero for an
OS-assigned test port. Its connection regression now binds only to loopback
and does not use a fixed port that could collide with another test run.
These network tests require loopback socket permission; a restricted execution
sandbox may block them even though they do not access the Internet.

### Application restore boundary

`ProjectRestoreService` now owns version-to-mixer reconstruction. MainComponent
supplies catalog lookups and UI callbacks; integration tests supply a deterministic
JUCE plug-in format through the same asynchronous format-manager/hosted-slot path.
Test processor defaults deliberately differ from saved values, so omitting state
application fails the audio comparison.

Restoration preflights blob references and routing before replacing the mixer.
Completion waits for all asynchronous plug-in requests, including missing and
cancelled loads. Session persistence and host-state rebuilding wait for completion;
a dirty Save during loading fails without creating a partial version. A clean
Dorico save can still retain its existing saved identity. Queued rebuilds recheck
whether they became invalid before publishing.

This covers the shared reconstruction path, not every MainComponent startup/import
path, actual vendor loading behavior, or Dorico's VST3 save callbacks. Those remain
integration/acceptance work below.

## Next stages

1. Add rendered UI interaction tests, replacing fragile source checks where
   practical. Exercise selection, button enablement, dialogs, and undo.
2. Build a small host for the actual Fiddle VST3, with an isolated server
   profile (database, state/audio files, TCP and UI endpoints, and scanning
   disabled). Drive host transport/MIDI and save/restore callbacks, and assert
   the returned audio and project identity. The current `FiddleMock` is an
   older diagnostic utility, not this host or the acceptance runner.
3. Add an opt-in local vendor-plug-in matrix. Use installed licensed plug-ins;
   keep it separate from CI's deterministic, redistributable processors.
4. Add a short Dorico suite using fixed scores and verified desktop control.
   AI may operate Dorico, but captured MIDI, audio, and saved-state assertions
   should decide pass/fail. Verify playback-template allocation and expression
   maps, repeated transport stops, and save/reopen restoration.

Retain human listening and usability checks for significant changes. Routine
regression checks should not depend on a person carrying out a checklist.
