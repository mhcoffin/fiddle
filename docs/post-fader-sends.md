# Pre- and post-fader sends

## Quick smoke test

1. In **View → Bus Manager**, create a bus named Reverb with no strips selected
   (creating from a selection changes those strips' direct outputs).
2. In the **Audio Buses** bank, open that bus's Audio FX panel, add a reverb,
   and set the plug-in's mix to **100% wet**.
3. Open an instrument strip's **Post-fader** settings button. Under Sends, choose Reverb
   and Add send. New sends start enabled at −12 dB with a **Post-fader** tap.
4. Play in Dorico. Adjust the send level: the dry sound remains, while the
   reverb changes. Lower the strip fader: both dry sound and the new signal
   feeding the reverb decrease. Disable the send: the dry path remains and
   already-fed reverb tails decay.
5. Try strip solo, return solo, strip mute, and return mute. Try Undo/Redo,
   deleting the return and undoing, then saving and reopening the project.

### Pre-fader smoke test

1. While stopped, change a send's **Send tap** selector to **Pre-fader**.
2. Play and lower the strip fader to −∞. Its dry output disappears but the
   send continues at the level set by the send knob. Raising/lowering the strip
   fader must not affect that send.
3. Mute the strip: new input to the return stops (existing return tails may
   decay). Unmute and check strip solo/return solo as with post-fader sends.
4. Check **Show on strip**, then change the tap while stopped. The promoted
   control moves between the pre/post sections; its level, destination, enabled
   state, and promotion are retained. Undo/Redo and save/reopen retain the tap.
5. If practical, put an obvious effect in the strip's post-fader rack: a pre
   send excludes it, while a post send includes it. Pre-fader effects affect
   both. Automated tests cover sample-level compensation with delayed effects,
   so this does not need to be judged by ear.

Send levels use a knob with an editable dB readout. Drag vertically; hold Shift
for ten-times finer pointer adjustment. Arrow keys adjust by 0.5 dB (Shift:
0.1 dB), Page Up/Down by 5 dB, Home selects silence, and End selects +12 dB.
Type a value and press Enter or leave the field to commit; Escape cancels a text
edit. `-inf` or `−∞` selects silence. One drag, held adjustment key, or text
commit is one Undo step. The reusable `Knob.svelte` has no native/IPC dependency;
the panel supplies its gain curve, formatting, change callback, and history
boundaries.

Check **Show on strip** beside a send in Channel Audio to promote its destination
name, knob, and editable level into the mixer strip. Unchecking hides only the
shortcut; it does not disable the send. Promotion is saved with the project and
supports Undo/Redo, and survives destination/level changes and bus-deletion Undo.
Destination names follow bus renames; disabled sends are marked `(off)`.

The strip groups controls into labeled Instrument, Pre-fader, Post-fader, and
Output sections, each on a subtle lighter background. Small settings buttons
beside the Pre-fader/Post-fader headings open Channel Audio. In Large mode,
insert names are editor buttons with a separate bypass control; empty racks
show quiet “No inserts” text instead of a dashed box.
The settings buttons show an insert count (e.g. “2 FX”, or just “2” in Compact
mode). Counts omit sends, disappear for empty racks, and use a subdued accent
only when at least one insert is loaded and not bypassed; otherwise they are
gray. The accessible button label also describes the active/bypassed state.
Send destinations have a right-arrow routing cue. The small knob and dB readout
sit side by side. Large strips put the destination on that same row; narrower
strips put it above the control pair so names and numbers have room.

The mixer reserves matching pre- and post-fader send-band heights across layers,
chair sum strips, audio buses and Master, keeping the faders aligned. Each band
is absent when none of the displayed layers has a promoted send at that tap.
Up to two send rows per section are shown at once; longer lists scroll within
that space. Inserts are not promotable.

## Signal and control semantics

- Path: instrument → pre-fader inserts → **pre-fader sends** → strip fader →
  post-fader inserts → direct output **and post-fader sends**. Sends copy the
  signal without consuming it or processing the instrument a second time.
- Pre-fader sends are independent of the strip gain, including −∞. They do
  not bypass strip mute, inactive-layer state, or strip-solo suppression.
- Each strip has at most one send to any given bus. Destination, enable, and
  gain (−120 to +12 dB; −120 is silence), and pre/post tap are independently
  editable. Changing the destination preserves the other settings.
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

Pre-fader capture happens inside the strip gain node, immediately before gain
is applied, into graph-owned stereo storage allocated only for strips with pre
sends. No buffer allocation, extra plug-in processing, or routing lookup is
added to the audio callback; the copy uses JUCE's vectorized buffer operations.
Its route compensation includes the post-fader latency that this tap skips.
Strip rack edits and latency notifications refresh this topology as well as bus
edits. Bypassed strip/bus inserts are zero-latency pass-throughs; their effective
rack latency is updated accordingly. Bypass/tap changes can retime the paths
and should be made while stopped; this does not add a crossfaded bypass path.

Ordered send definitions live in versioned strip blobs (`sends_json`). Old
projects default to no sends and retain their existing hashes. Restore validates
the full send configuration before changing the live mixer. Bus deletion
removes its sends; Undo retains and restores their IDs/settings and the existing
bus/effect instances. Missing effects use the existing visible, state-retaining
pass-through placeholders; mute a return if that fallback is undesirable.
The optional boolean `preFader` defaults to false and is omitted for post-fader
sends, preserving existing send hashes. Invalid tap types reject the restore
before changing live state.

Automated coverage includes serial/parallel routing, multiple paths with real
sample delays and variable block lengths, solo/mute, return tails, history,
SQLite migration and reopen, missing-effect recovery, and built-UI controls.
