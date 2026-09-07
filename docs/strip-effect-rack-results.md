# Instrument-strip effect racks

Phase 4B completes the first per-instrument audio-processing path. Each layer
strip now owns two ordered effect racks in its existing stereo
`AudioProcessorGraph`:

1. graph input;
2. pre-fader effects;
3. strip fader, including mute, solo, and library-activation audibility;
4. post-fader effects;
5. graph output to the shared Master path.

The main strip stays intentionally uncluttered. Its full-width **Audio FX**
button shows the insert count and opens a wide Channel Audio panel. The panel
identifies the layer and instrument, displays total strip latency, and provides
searchable compatible-effect selection. Each insert can be reordered, moved
across the fader, bypassed, edited, or removed.

Rack operations use typed WebView messages and undoable C++ commands. Plug-in
editor changes participate in the same dirty-state detection used by instrument
and Master plug-ins. Ordered plug-in identity, bypass state, and binary state
are stored in the live database, version DAG strip blobs, and the Dorico
compatibility state blob. Older state without the optional rack field restores
as an empty rack. The state-blob binary version remains 4 because the new value
is an additive member of the existing per-strip JSON envelope.

Processor latency is reported by each graph and subtracted from that strip's
scheduled MIDI time. Layers with different effect chains therefore reach the
common Master path together, while the existing Master compensation continues
to apply to every strip.

Automated coverage includes:

- pre-fader versus post-fader processing order;
- bypass transparency, rack movement, latency, and independent graph state;
- rack serialization and plug-in-state round trips;
- typed command routing and malformed-payload rejection;
- version-store and Dorico state-blob persistence;
- Channel Audio panel controls and effect filtering.

## Manual acceptance check

Use one obvious real-time effect (an EQ is ideal) and, if available, one effect
with look-ahead latency:

1. Start Fiddle and Dorico, load a working multi-layer chair, and play it.
2. On one layer, open **Audio FX** and add the EQ before the fader. Confirm only
   that layer changes and other chairs/layers continue normally.
3. Bypass and enable the EQ, edit an obvious parameter, then close and reopen
   its editor. Confirm the setting remains.
4. Add a second effect after the fader. Reorder within a rack and move an effect
   before/after the fader; verify the displayed order and sound follow.
5. Exercise the strip fader, mute, solo, and a library activation toggle. Check
   for clicks, stuck notes, cross-talk, or unexpected level changes.
6. If using a look-ahead effect, confirm Channel Audio reports non-zero latency
   and layered instruments remain acceptably aligned.
7. Undo and redo add, move, bypass, and remove operations.
8. Save in Fiddle, restart the server, and verify effect order, location,
   bypass, and edited state are restored.
9. Save the Dorico project in a distinct Fiddle version, switch away, then
   reopen the project. Verify the same strip racks and sound return.

Once this passes with the installed plug-ins, Phase 5 can add group buses on
top of the established strip and Master graph vocabulary.
