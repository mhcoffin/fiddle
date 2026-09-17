# Post-fader sends

## Quick smoke test

1. In **View → Bus Manager**, create a bus named Reverb with no strips selected
   (creating from a selection changes those strips' direct outputs).
2. In the **Audio Buses** bank, open that bus's Audio FX panel, add a reverb,
   and set the plug-in's mix to **100% wet**.
3. Open an instrument strip's **FX / Sends** panel. Under Post-fader sends,
   choose Reverb and Add send. New sends start enabled at −12 dB.
4. Play in Dorico. Adjust the send level: the dry sound remains, while the
   reverb changes. Lower the strip fader: both dry sound and the new signal
   feeding the reverb decrease. Disable the send: the dry path remains and
   already-fed reverb tails decay.
5. Try strip solo, return solo, strip mute, and return mute. Try Undo/Redo,
   deleting the return and undoing, then saving and reopening the project.

Send levels use a knob with an editable dB readout. Drag vertically; hold Shift
for ten-times finer pointer adjustment. Arrow keys adjust by 0.5 dB (Shift:
0.1 dB), Page Up/Down by 5 dB, Home selects silence, and End selects +12 dB.
Type a value and press Enter or leave the field to commit; Escape cancels a text
edit. `-inf` or `−∞` selects silence. One drag, held adjustment key, or text
commit is one Undo step. The reusable `Knob.svelte` has no native/IPC dependency;
the panel supplies its gain curve, formatting, change callback, and history
boundaries.

## Signal and control semantics

- Tap: instrument → pre-fader inserts → strip fader → post-fader inserts →
  direct output **and** sends. A send does not process the instrument again.
- Each strip has at most one send to any given bus. Destination, enable, and
  gain (−120 to +12 dB; −120 is silence) are independently editable. Changing
  the destination preserves the level and enabled state.
- Sends are strip-to-bus only; buses feed Master. No bus-to-bus feedback or
  nested routing is introduced. Existing buses work both as submixes and FX
  returns; dedicated wet-only buses are usually easiest to manage.
- Strip mute, inactive layers, and strip-solo suppression stop new input to
  both dry and send paths. A return's mute/fader affects its output, not the
  source's dry signal. Return processors keep processing, so their tails can
  decay when a send is disabled or removed.
- Soloing a group retains sends from sources whose direct output is that
  group. Contributions from other groups are suppressed, even on a shared
  return. Soloing a return alone auditions its wet mix. Previously accumulated
  tails inside a shared effect cannot be separated by source.
- Existing stopped-transport idle policy is unchanged: it may suspend DSP
  after silence, and has a five-second maximum tail window. Long stopped
  reverb/delay tails are still subject to that limit. Printing's transport-end
  behavior is also unchanged; sends are included in the normal Master print.

## Implementation notes

The published audio graph resolves send IDs to bus pointers and preallocates
all routing buffers. Strip rendering is identical for serial and parallel
workers; only the coordinator sums the branches, in stable strip order.
RouteDelay copies and sums contiguous spans using JUCE AudioBuffer operations
(vectorized by JUCE), with no per-block allocations or routing-string lookups.

Each source's MIDI lead includes the longest destination latency. Faster
branches receive a sample delay equal to the difference. Disabled sends remain
in the latency topology, so toggling enable does not retime the dry path.
Unchanged delay histories survive graph publication and gain/enable edits;
device reprepare clears them. Topology/latency changes can still cause a
transition while queued MIDI/audio catches up; make those edits while stopped.

Ordered send definitions live in versioned strip blobs (`sends_json`). Old
projects default to no sends and retain their existing hashes. Restore validates
the full send configuration before changing the live mixer. Bus deletion
removes its sends; Undo retains and restores their IDs/settings and the existing
bus/effect instances. Missing effects use the existing visible, state-retaining
pass-through placeholders; mute a return if that fallback is undesirable.

Automated coverage includes serial/parallel routing, multiple paths with real
sample delays and variable block lengths, solo/mute, return tails, history,
SQLite migration and reopen, missing-effect recovery, and built-UI controls.
