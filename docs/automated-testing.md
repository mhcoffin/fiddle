# Automated testing

Status: initial testing foundation implemented, September 2026.

Validated locally: all 45 stable CTest entries pass, and FiddleServer/UI build.
The mixer integration executable contains seven scenarios. Both it and the
version-store tests pass 20 consecutive runs. GitHub execution remains to be
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

This tests the production save format and stored-version retrieval, not the
entire MainComponent restore workflow or Dorico's actual save callbacks.

## Next stages

1. Extract the remaining application-level restore orchestration into a
   directly testable service. Recreate the mixer from a saved version and
   compare rendered audio, including missing plug-in handling.
2. Add rendered UI interaction tests, replacing fragile source checks where
   practical. Exercise selection, button enablement, dialogs, and undo.
3. Build a small host for the actual Fiddle VST3, with an isolated server
   profile (database, state/audio files, TCP and UI endpoints, and scanning
   disabled). Drive host transport/MIDI and save/restore callbacks, and assert
   the returned audio and project identity. The current `FiddleMock` is an
   older diagnostic utility, not this host or the acceptance runner.
4. Add an opt-in local vendor-plug-in matrix. Use installed licensed plug-ins;
   keep it separate from CI's deterministic, redistributable processors.
5. Add a short Dorico suite using fixed scores and verified desktop control.
   AI may operate Dorico, but captured MIDI, audio, and saved-state assertions
   should decide pass/fail. Verify playback-template allocation and expression
   maps, repeated transport stops, and save/reopen restoration.

Retain human listening and usability checks for significant changes. Routine
regression checks should not depend on a person carrying out a checklist.
