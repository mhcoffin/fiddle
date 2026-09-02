# Phase 2 Hosted Plug-in Slot Results

**Status:** Complete  
**Date:** 2026-09-02

## Implemented

`HostedPluginSlot` now owns the responsibilities that were previously embedded
in `MixerStrip`:

- asynchronous processor creation with stale-request and destroyed-owner
  protection;
- a stable `PluginSlotId` and explicit instrument/effect role;
- processor, scratch-buffer, editor, and listener lifetime;
- cached state capture and restoration;
- bypass state;
- stereo effect bus-layout negotiation while preserving native multi-output
  instrument layouts;
- compatibility diagnostics;
- empty, loading, loaded, missing, and failed states;
- deferred processor destruction until realtime readers have exited.

`MixerStrip` delegates its existing instrument-host operations to one
instrument-role slot. Its public JSON remains backward compatible and now also
publishes slot ID, status, bypass, and compatibility data.

## Realtime listener improvement

The old listener could call an allocating asynchronous UI helper from a plug-in
callback, which may occur on the audio thread. The hosted slot now only sets an
atomic notification flag. `MainComponent` consumes those flags from its regular
message-thread timer, refreshes cached state, and performs persistence/UI work
there.

## Catalog metadata

The scanner now publishes:

- `isInstrument`;
- stereo input/output availability;
- compatibility as an instrument and as an effect;
- whether compatibility needs confirmation after instantiation;
- a reason when a plug-in is known to be incompatible.

Some VST3 descriptions report zero channels until instantiated. Those entries
are treated as provisional rather than rejected; the hosted slot makes the
final decision by asking the live processor to accept stereo buses.

The scanner cache now stores `isInstrument`. Existing cache tables receive the
new column and a one-time category-based backfill; a later rescan replaces that
inference with the plug-in's reported value.

## Missing plug-ins

All centralized restore paths now use the same helper. If a saved UID is absent
from the scanned catalog, Fiddle retains an explicit missing instrument slot,
including the UID and serialized state, instead of silently presenting an
empty slot. The future Audio Console can therefore offer rescan, replace, and
remove actions without losing state.

## Verification

The deterministic hosted-slot test covers:

- instrument/effect compatibility classification;
- stereo-layout acceptance and rejection;
- state capture and restoration;
- bypass state;
- allocation-free listener notification handoff;
- missing-slot state preservation;
- failed replacement leaving the working processor active;
- delayed reclamation while an audio read guard is held.

A locally installed real effect, **Vienna Ensemble Pro Audio Input**, also
passed the isolated host path:

- discovered as a stereo VST3 effect;
- instantiated at 48 kHz with a 256-sample block;
- state captured and restored;
- bypass processing left the input signal unchanged;
- native editor opened and closed;
- instance unloaded and reclaimed cleanly.

A startup regression exposed an important distinction between effects and
instruments: forcing a multi-output instrument down to a two-channel bus layout
left Vienna Synchron Player writing to outputs that the VST3 host had disabled.
The resulting null channel pointer crashed the audio thread. Instrument slots
now preserve the processor's native active buses, allocate scratch storage for
all of them, and continue to mix only the primary stereo pair. An isolated
Vienna Synchron Player processing test passed with all 22 active channels, and
a full saved-project startup remained stable. A deterministic four-channel
instrument test guards the layout behavior.

Startup now also finishes cached-catalog restoration before beginning the
background catalog refresh. This removes concurrent duplicate additions and
the associated JUCE `KnownPluginList` assertions.

The complete C++ build succeeds and all 19 CTest tests pass.

## Manual completion check

Before committing Phase 2, run Fiddle normally and verify one existing
instrument strip:

1. its current VST instrument loads and produces sound;
2. the Edit button opens the instrument editor;
3. changing a plug-in parameter marks the project dirty;
4. removing and reassigning the instrument still works;
5. restarting Fiddle restores the instrument and its state.

This smoke test passed on 2026-09-02 after correcting the multi-output startup
regression described above.

No audio-effect controls or production audio graph have been added to the main
UI yet. Those begin with the Master effect-rack vertical slice in Phase 3.
