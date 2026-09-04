# Master effect rack implementation results

Phase 3 of the audio plug-in routing proposal is implemented as a complete
Master-bus vertical slice.

## Audio path

`MixerModel` owns one permanent `MasterAudioEngine`. The existing instrument
strips continue to render into the shared stereo mix, after which that mix is
processed by a JUCE `AudioProcessorGraph` with this order:

1. stereo graph input;
2. zero or more ordered hosted effect slots;
3. master output gain;
4. stereo graph output.

The domain model owns stable slot IDs and plug-in state. JUCE graph node IDs
are transient implementation details, so rebuilding or reordering the graph
does not change persisted slot identity. Loading, failed, and missing slots
remain safe pass-through stages.

The graph reports its aggregate latency. Fiddle subtracts that latency from
its existing MIDI scheduling delay, clamped at zero, so introducing a
look-ahead limiter does not add an unaccounted timing offset.

## Commands and UI

The Master Audio command boundary supports:

- adding a compatible stereo effect;
- removing and reordering insert slots;
- bypassing or enabling a slot;
- opening the native plug-in editor;
- changing master output gain;
- requesting the current rack, meter, status, and latency state.

Mutations participate in the existing undo manager. The main mixer toolbar
opens a generously sized Master Audio panel rather than adding controls to
each narrow strip. Its effect picker searches compatible installed effects by
name, manufacturer, or category and groups results by manufacturer. Insert
rows use labeled buttons and show loading or missing status without relying on
small icons.

## Persistence and restoration

Master gain, insert order, full plug-in identity, bypass state, and binary
plug-in state are stored in the live SQLite configuration. The same data is
part of version-DAG global state and version 4 of the Dorico state blob.
Versions 1 through 3 remain readable and restore the historical default of an
empty rack at 0 dB.

Periodic dirty-state polling compares the current program and automatable
parameter values rather than raw `getStateInformation()` bytes. Some VST3
effects include changing runtime data in that opaque blob even while idle;
treating those bytes as user edits repeatedly enabled Save and created needless
database versions. Explicit saves and real parameter changes still capture the
complete opaque plug-in state.

Restoration is wired through:

- ordinary Fiddle restart;
- Dorico `setStateInformation` restoration;
- branch checkout;
- historical version checkout;
- branch creation from an earlier version;
- reload of a merged target branch.

If a saved plug-in is unavailable, the rack retains a visible missing slot and
its saved state instead of silently deleting it.

## Automated verification

The stable suite currently contains 25 tests and passes in full. Phase-specific
coverage includes:

- offline ordered effect processing, bypass, reorder, gain, state capture, and
  graph latency;
- Master Audio JavaScript command routing and malformed payload rejection;
- compatible-effect filtering, grouping, search, and gain bounds in the UI;
- version-DAG identity changes for master inserts, bypass, and plug-in state;
- durable SQLite round-tripping of Master global state;
- state-blob version 4 master restoration and version 3 compatibility;
- rejection of volatile idle plug-in state as a false project change;
- branch-menu visibility and stable-ID selection behavior;
- the earlier JUCE graph and reusable hosted-slot tests.

The Svelte production build and complete C++ build also pass.

## Manual acceptance check

Use one real stereo EQ or limiter to verify the final host-specific behavior:

1. add it from **Master Audio**, open its editor, and change an obvious setting;
2. play audio and verify bypass, enable, ordering, master gain, and meter behavior;
3. restart Fiddle and confirm the plug-in, order, bypass, and edited state return;
4. save and reopen the Dorico project and confirm the same restoration;
5. for a look-ahead plug-in, confirm the displayed Master latency changes and
   playback remains aligned.

After that smoke test, Phase 4 can move instrument rendering into per-strip
graphs and introduce strip insert racks.
