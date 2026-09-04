# Audio Plug-ins, Routing, and Bus Architecture

Status: **Accepted for implementation**
Date: 2026-09-02

## 1. Purpose

Fiddle currently hosts one VST instrument per mixer strip and mixes every strip
directly into a stereo return sent back to Dorico. This proposal extends Fiddle
into a small but conventional audio workstation mixer with:

- insert audio effects on instrument strips;
- group buses for submix processing;
- aux/FX buses for shared reverbs and delays;
- a permanent master bus for final processing;
- routing, metering, persistence, undo, and project restoration;
- a spacious, panel-oriented user interface rather than dense miniature
  controls.

The proposal deliberately follows familiar Cubase and general DAW concepts,
while limiting the initial routing rules so that Fiddle remains predictable and
maintainable.

## 2. Executive recommendation

Use JUCE `AudioProcessorGraph` as the realtime audio renderer, but do **not**
make it the persistent project model.

Fiddle should maintain its own stable channel, bus, plug-in-slot, send, and
routing objects. A graph builder will translate that model into transient JUCE
nodes and connections. JUCE node IDs must never be saved in a project.

The finished signal model should support both inserts and buses:

```text
Scheduled MIDI
      |
      v
VST instrument -> pre-fader inserts -> channel stage
                                          | \
                                          |  +-> aux send -> FX bus ----+
                                          |                             |
                                          +-> direct out -> group bus --+-> master -> Dorico
```

These facilities serve different purposes:

- strip inserts: corrective EQ, compression, saturation, instrument-specific
  processing;
- group buses: Strings, Brass, Percussion, layered-instrument submixes, glue
  compression;
- aux/FX buses: shared halls, rooms, delays, and other parallel effects;
- master inserts: limiting, metering, room correction, and final processing.

The initial implementation should remain stereo and VST3-only. Arbitrary bus
graphs, feedback routing, sidechains, surround, automation, and multi-output
instruments should be deferred.

## 3. Current Fiddle architecture

### 3.1 Instrument strips

`MixerStrip` currently combines several responsibilities:

- Dorico MIDI input assignment;
- note annotation, Lua processing, and expression-map processing;
- realtime MIDI scheduling;
- ownership of one `AudioPluginInstance`;
- instrument editor lifetime;
- plug-in state capture;
- gain, mute, solo, and metering;
- direct summation into the host output buffer.

The current plug-in is necessarily treated as a generator. `MixerStrip` clears
its temporary audio buffer, sends scheduled MIDI to the plug-in, and then adds
the resulting audio directly to the shared mix. There is no point at which an
audio effect can receive the output of another processor.

### 3.2 Current audio graph

`MixerModel::ActiveAudioGraph` is currently an immutable list of raw
`MixerStrip` pointers. The audio callback iterates that list and asks each strip
to add audio to a common output buffer.

This publication mechanism is good realtime engineering, but the published
object is not yet an audio-routing graph. It has no intermediate buffers, bus
nodes, sends, or topology.

### 3.3 Current group controls

The group-master faders shown beside layered strips are control surfaces, not
audio paths. They calculate aggregate power in the UI and modify the child
strip gains. In Cubase terminology they are closer to an automatically managed
VCA/link relationship than to a Group Channel.

This distinction should remain explicit. A VCA changes controls; a group bus
receives and processes audio. Fiddle should not silently change the existing
linked-fader behavior into bus behavior.

### 3.4 Plug-in discovery

The scanner already records name, manufacturer, category, validity, and input
and output channel counts. JUCE's `PluginDescription` also provides
`isInstrument`, but Fiddle does not currently cache or publish it.

The catalog should be extended so that:

- the VSTi selector normally shows instruments only;
- effect selectors normally show non-instruments with audio inputs;
- invalid plug-ins remain visible in diagnostics but cannot be selected;
- a deliberate "Show all compatible plug-ins" escape hatch handles incorrect
  vendor metadata.

### 3.5 Persistence

The current state format stores one plug-in UID and one binary state blob per
strip. It has no representation for ordered insert slots, repeated instances of
the same plug-in, buses, sends, output destinations, or master processing.

Audio routing therefore requires a versioned state extension, not merely new
database columns. There are no substantive projects using the current format;
the existing projects are test data and can be rebuilt. The first audio-routing
schema may therefore start clean rather than carrying a backward-migration
layer.

### 3.6 Audio return

Fiddle's shared-memory return to Dorico is currently two-channel. The internal
mixer should therefore use 32-bit floating-point stereo paths initially.
Keeping the internal model explicit about channel layout will allow future
expansion without promising it now.

## 4. Goals and non-goals

### 4.1 Goals

1. Host ordered chains of VST3 audio effects.
2. Allow effects on instrument strips, group buses, aux buses, and Master.
3. Provide one direct output destination per channel.
4. Provide parallel sends to aux buses.
5. Preserve realtime safety during playback and graph editing.
6. Compensate differing plug-in latencies at summing points.
7. Preserve plug-in state, ordering, routing, and bus state in Fiddle versions
   and Dorico project state.
8. Make every topology edit undoable.
9. Provide a familiar DAW vocabulary without reproducing every Cubase feature.
10. Keep the main mixer readable at Comfortable and Large strip sizes.

### 4.2 Initial non-goals

- audio-unit, AAX, or CLAP hosting;
- surround or immersive buses;
- multiple output buses returned separately to Dorico;
- VST instrument multi-output routing;
- external audio inputs;
- arbitrary nested buses;
- feedback loops;
- sidechain connections;
- parameter automation lanes;
- control-room features;
- out-of-process plug-in crash isolation;
- freezing, bouncing, or offline rendering.

The persistent model should leave room for several of these, especially
sidechains and additional output layouts, without exposing them prematurely.

## 5. User-facing terminology

Use conventional terms consistently:

| Term | Meaning in Fiddle |
|---|---|
| Instrument strip | Existing strip containing MIDI interpretation and one VST instrument |
| Insert | Audio effect placed serially in a channel path |
| Effect rack | Ordered inserts belonging to a strip or bus |
| Group bus | Receives the complete direct output of one or more channels |
| Aux or FX bus | Receives parallel sends while the dry/direct path continues |
| Send | A level-controlled copy of a channel signal feeding an aux bus |
| Return | The output of an aux/FX bus entering the master mix |
| Master | Permanent final stereo bus sent to Dorico |
| Linked group | Existing VCA-like control relationship among layered strips |

The UI may display "FX Bus" because that will be familiar to a Cubase user.
Internally, `AuxBus` is the clearer type name.

## 6. Persistent domain model

The domain model is the source of truth. It must be usable without JUCE graph
objects and must support serialization, comparison, undo, versioning, and
validation.

### 6.1 Stable identifiers

Use stable IDs for all repeatable or referenceable objects:

- `BusId`: UUID;
- `PluginSlotId`: UUID;
- `SendId`: UUID;
- instrument-strip identity: the existing stable
  `(port, channel, libraryId)` identity;
- Master: one reserved well-known ID.

A plug-in UID identifies a plug-in class, not an instance. It cannot identify a
slot because the same compressor may appear several times in one project.

### 6.2 Plug-in identity

Persist more than the current integer UID:

```text
PluginIdentity
  formatName
  uniqueId
  fileOrIdentifier
  manufacturer
  name
```

The format and unique ID are primary. File identifier, manufacturer, and name
provide diagnostics and a conservative recovery path when a plug-in has moved.
Fiddle must never silently substitute a different plug-in merely because its
display name matches.

### 6.3 Plug-in slot

```text
PluginSlotState
  id: PluginSlotId
  identity: PluginIdentity
  bypassed: bool
  position: preFader | postFader
  stateBlobHash: optional hash
  displayName: cached diagnostic name
```

Binary plug-in state should be stored as a separate content-addressed blob and
referenced by hash. Effects will multiply the amount of binary state; separating
it avoids duplicating unchanged state across Fiddle versions.

If a plug-in is unavailable, keep a `MissingPluginSlot` containing all metadata
and the original state blob. The slot should be visibly disabled but restorable
after the plug-in is installed.

### 6.4 Instrument strip additions

Extend each strip with:

```text
preFaderInserts: ordered PluginSlotId list
postFaderInserts: ordered PluginSlotId list
directOutput: Master or BusId
sends: ordered SendState list
pan: float, reserved initially
```

The existing VST instrument remains a distinct instrument slot, not the first
member of the audio-effect rack. This prevents an effect from being mistaken
for a MIDI instrument and keeps library restoration semantics intact.

### 6.5 Send state

```text
SendState
  id: SendId
  destination: BusId
  levelDb: float
  enabled: bool
  tap: preFader | postFader
```

The first user-facing version may create post-fader sends by default, but the
state format should support both positions from the beginning.

### 6.6 Bus state

```text
AudioBusState
  id: BusId
  name: string
  role: group | aux
  order: integer
  colour: optional user colour
  gainDb: float
  muted: bool
  soloed: bool
  preFaderInserts: ordered PluginSlotId list
  postFaderInserts: ordered PluginSlotId list
  directOutput: Master
```

Group and aux buses can share one implementation. Their roles impose different
routing rules and UI defaults.

### 6.7 Master state

Master is permanent and cannot be deleted or rerouted:

```text
MasterBusState
  gainDb: float
  muted: bool
  preFaderInserts: ordered PluginSlotId list
  postFaderInserts: ordered PluginSlotId list
```

Master post-fader slots are useful for a final limiter or metering plug-in. The
UI should make the pre/post divider visible without requiring the user to
understand it before adding a normal insert.

## 7. Routing rules

The model should reject illegal routing before asking JUCE to build it.

### 7.1 Initial legal routes

- Instrument strip direct output -> Group bus or Master.
- Group bus direct output -> Master.
- Instrument strip send -> Aux bus.
- Group bus send -> Aux bus, possibly deferred until the first aux milestone.
- Aux bus output -> Master.
- Master -> Dorico stereo return.

### 7.2 Initial illegal routes

- Any route to an instrument strip.
- Any route from Master.
- Aux-to-aux sends.
- Group-to-group nesting.
- Aux output to Group.
- A bus routed to itself.
- Any connection that creates a cycle.

Restricting the topology gives the user a familiar mixer without requiring a
general patch bay. Nested groups can be added later with explicit cycle
detection if a real use case emerges.

### 7.3 Removal behavior

Removing a bus is a compound operation:

1. close its plug-in editors;
2. reroute direct outputs that targeted it to Master;
3. remove sends that targeted it;
4. remove its plug-in slots and graph nodes;
5. remove the bus;
6. commit the whole change as one undoable transaction.

The confirmation dialog must summarize how many direct routes and sends will
be affected. Undo must restore the exact routing and plug-in state.

## 8. Signal order

### 8.1 Instrument strip

```text
scheduled MIDI
  -> VST instrument
  -> pre-fader inserts
  -> pre-fader send taps
  -> fader/mute/pan stage
  -> post-fader inserts
  -> post-fader send taps
  -> output meter
  -> direct output
```

Pan may remain fixed at center in the first implementation, but its location in
the signal model should be settled now.

### 8.2 Group bus

```text
sum of direct inputs
  -> pre-fader inserts
  -> optional pre-fader send taps
  -> fader/mute/pan stage
  -> post-fader inserts
  -> optional post-fader send taps
  -> output meter
  -> Master
```

### 8.3 Aux/FX bus

```text
sum of sends
  -> pre-fader inserts
  -> return fader/mute stage
  -> post-fader inserts
  -> output meter
  -> Master
```

An aux effect such as a reverb should normally be configured 100% wet in its
own editor. A host-level wet/dry control is useful but should be a later feature;
duplicating dry signal on an aux return is an easy source of level and phase
mistakes.

### 8.4 Master

```text
sum of direct and returned buses
  -> pre-fader inserts
  -> master fader stage
  -> post-fader inserts
  -> output meter
  -> safety diagnostics
  -> shared-memory stereo return
```

Fiddle should not silently hard-clip the floating-point signal. The master meter
must show overloads clearly. Any optional safety limiter must be explicit and
user-controlled.

## 9. JUCE `AudioProcessorGraph` design

### 9.1 Why it is appropriate

The JUCE version already used by Fiddle provides:

- owned processor nodes;
- legal connection checks;
- topological processing order;
- automatic summation of multiple inputs;
- asynchronous or batched graph rebuilds;
- render-sequence exchange between control and audio threads;
- node bypass;
- delay insertion that aligns paths with different processor latencies.

These are exactly the difficult facilities that a custom bus renderer would
otherwise need to reproduce and test.

### 9.2 Why it is not the project model

JUCE graph node IDs are runtime details. The graph owns processor objects and
may be rebuilt as plug-ins load or routing changes. Persisting its node IDs
would couple saved projects to incidental construction order and complicate
persistence and restoration.

Fiddle's model should be translated into a graph by an `AudioGraphBuilder`.
The builder maintains a transient map:

```text
Fiddle stable ID -> AudioProcessorGraph::NodeID
```

That map exists only while the graph generation is active.

### 9.3 Proposed processor nodes

Fiddle will need several small custom `AudioProcessor` implementations:

1. `ScheduledMidiSourceProcessor`

   Owns or references the existing lock-free MIDI scheduler and emits the MIDI
   buffer for exactly one instrument strip. It has a MIDI output and no audio
   output.

2. Instrument plug-in node

   The VST instrument is a normal graph node. Connect the strip's scheduled
   MIDI source to its MIDI input. Use its primary stereo output initially.

3. Effect plug-in node

   A normal VST3 effect node with stereo input and output.

4. `ChannelGainProcessor`

   Applies gain, mute, future pan, and short transition ramps. Frequently
   changed values are atomic parameters; moving a fader must not rebuild the
   graph.

5. `MeterProcessor`

   Measures the final channel or bus output after post-fader inserts. Meter
   publication is lightweight and separate from full mixer-state updates.

6. `SendGainProcessor`

   Applies send enable and send level to one graph branch. Its values are
   atomic and do not require topology changes.

7. `MissingPluginProcessor`

   For a missing effect, passes audio unchanged. For a missing instrument,
   emits silence. It preserves graph legality and allows later restoration.

8. Graph audio-output node

   A JUCE `AudioGraphIOProcessor` writes the final stereo graph output into the
   buffer already passed to `MixerModel::processBlock`.

### 9.4 Graph construction

Topology changes occur only on the JUCE message thread:

1. validate the domain model;
2. instantiate any required plug-in asynchronously;
3. negotiate and prepare its bus layout;
4. batch node and connection changes with `UpdateKind::none`;
5. request one graph rebuild;
6. publish updated UI state;
7. reclaim removed editors and processors away from the audio callback.

The current `RealtimeObjectPublisher` remains useful for immutable control-side
snapshots and object reclamation, but JUCE's graph render-sequence exchange
should become the actual audio topology mechanism.

### 9.5 Graph transitions

Adding, removing, bypassing, or replacing processing can create a discontinuity
even if the topology swap itself is thread-safe. The affected channel stage
should apply a short ramp around destructive graph changes.

Recommended initial behavior:

- load a new plug-in asynchronously while the old path continues;
- ramp the affected path down over approximately 10 ms;
- commit the new topology;
- ramp back up over approximately 10 ms.

This should be tested and adjusted by ear. Do not allocate the ramp or transition
buffers in `processBlock`.

## 10. Plug-in hosting details

### 10.1 Bus-layout negotiation

The current instrument host prepares a buffer based on reported channel counts
but does not establish a general input/output layout.

Before adding an effect node, Fiddle should ask the processor to accept stereo
input and stereo output. The initial compatibility rules should be:

- accept native stereo-in/stereo-out effects;
- accept mono-in/stereo-out only after an explicit, tested adapter exists;
- reject instruments in effect slots by default;
- reject effects with no usable audio input;
- show incompatible plug-ins in diagnostics with a reason;
- defer sidechain and surround buses.

### 10.2 Editors

`PluginEditorWindow` must be generalized from one editor per strip to one editor
per `PluginSlotId`.

The title should include both channel and plug-in, for example:

```text
Strings Bus - Pro-C 2
```

Removing a slot or bus closes its editor before removing the graph node. Hiding
an editor does not unload the plug-in.

### 10.3 State changes and dirty tracking

The current listener and fallback polling logic should move into a reusable
hosted-slot object. Dirty tracking must identify the plug-in instance, not only
the class UID.

State capture must remain off the audio callback. Cached plug-in state is
updated on:

- initial load;
- project restore;
- listener notification;
- fallback polling for plug-ins that do not notify correctly;
- explicit save as a final consistency pass where safe.

### 10.4 Bypass

Use JUCE node bypass where possible. Preserve the host's requested bypass state
separately from a plug-in's own internal bypass parameter so state restoration
is deterministic.

Bypassing an effect must pass audio unchanged and preserve latency alignment.
Bypassing an instrument produces silence unless a future alternate source is
defined.

### 10.5 Missing and invalid plug-ins

Project load must never discard a slot because the binary is missing or failed
to scan. Keep the original identity, state blob, bypass state, and position.

The UI should offer:

- Rescan;
- Locate/replace plug-in;
- Remove missing slot;
- diagnostic details.

Replacement is explicit and undoable.

## 11. Realtime requirements

The audio callback must not:

- acquire project-model or database mutexes;
- allocate memory;
- destroy plug-ins or editors;
- serialize plug-in state;
- write logs;
- parse JSON;
- wait for a graph rebuild.

Fader, mute, solo, send-level, bypass, and meter data should use atomics or
single-producer/single-consumer structures suitable for frequent updates.

Topology objects and plug-in instances must be reclaimed on the message thread
only after the audio renderer can no longer reference them.

All graph buffers and custom processor scratch storage are sized in
`prepareToPlay` or during graph construction. Tests should cover a block size
larger than the initially reported device block size and either resize safely
off-thread or fail to silence without writing out of bounds.

## 12. Latency and Fiddle's lookahead

Latency has two related but distinct meanings:

1. **Path alignment:** parallel paths must arrive at a summing point together.
   JUCE's graph can insert delays based on node latency to achieve this.

2. **Overall render timing:** a lookahead compressor or limiter delays the
   final sound. Fiddle should prevent this from extending the user-selected
   playback delay unnoticed.

The engine should monitor `AudioProcessorGraph::getLatencySamples()` and convert
it to milliseconds. Scheduled MIDI target times should be advanced by the
overall graph latency so that audible output still lands at the requested
Fiddle delay.

```text
scheduled plugin time = intended audible time - graph latency
```

If total graph latency approaches or exceeds the configured playback delay,
Fiddle must warn the user and show the minimum safe delay. It must not schedule
events into the past without explanation.

When a plug-in reports a latency change, its listener must request a graph
rebuild on the message thread. Tests must verify that dry/direct and aux paths
remain sample-aligned after such a change.

## 13. Tails, transport, mute, and solo

### 13.1 Effect tails

Aux and group processors must continue receiving silent blocks after input
stops so reverbs and delays can decay naturally. The engine should consult
`getTailLengthSeconds()` for diagnostics but continue processing while the audio
device is active unless a later CPU-suspension system is explicitly designed.

### 13.2 Mute

Default semantics:

- strip mute closes the channel stage;
- post-fader sends stop receiving new input;
- pre-fader sends continue;
- an existing aux reverb tail continues;
- bus mute silences the bus return but does not destroy its plug-ins.

### 13.3 Solo

Solo behavior becomes more complex once buses exist. Initial rules should
match normal DAW expectations:

- soloing an instrument strip keeps its direct destination path audible;
- aux buses receiving that strip remain audible automatically (solo-safe
  returns);
- unrelated strips and their sends are suppressed;
- Master always remains audible;
- explicit bus solo can be deferred until group and aux behavior has dedicated
  tests.

The existing `anySoloed` loop is insufficient for a graph. The control model
should compute an audibility mask for channels and buses and publish atomic
stage gains without changing topology.

## 14. Persistence and versioning

### 14.1 Proposed state extension

Extend the content-addressed project state with:

- strip blobs containing insert racks, direct output, and sends;
- ordered bus blobs;
- a master blob;
- separately hashed plug-in binary-state blobs;
- stable slot and bus IDs;
- an audio-state schema version.

Conceptually:

```text
FiddleState
  global state
  ordered strip hashes
  ordered bus hashes
  master hash

StripBlob
  existing MIDI/instrument fields
  insert slot hashes
  direct output
  sends

BusBlob
  stable ID, role, name, order
  fader/mute/solo
  insert slot hashes
  output

PluginSlotBlob
  stable slot ID
  plug-in identity
  bypass and position
  binary-state hash
```

### 14.2 Referential integrity

Every restore and merge must validate routing references atomically.

If a direct-output bus is missing, route to Master and issue a visible warning.
If a send destination is missing, disable or omit that send and issue a warning.
Never leave a dangling runtime graph connection.

### 14.3 Transition from the current state

No backward migration from the pre-audio-routing state format will be
implemented. Fiddle has no substantive projects using that format; current
projects are test data and can be rebuilt quickly.

The first audio-routing state version will therefore establish a clean baseline:

- existing test projects and databases may be reset or regenerated;
- newly created strips initially have empty effect racks and route to Master;
- newly created strips have no sends or user buses;
- Master is created at 0 dB with no inserts;
- linked group controls retain their current behavior.

The state must still carry an explicit audio schema version. Once substantive
projects exist, later incompatible changes will require deliberate migration or
a clearly documented compatibility policy.

### 14.4 Dorico project portability

Normal and Super Save behavior must include all audio topology and plug-in state
required by the applicable save mode. A project containing a missing plug-in is
still a valid project: the saved slot and binary state must survive unchanged.

## 15. Commands and undo

Do not let UI message handlers manipulate the graph directly. Continue the
current separation:

```text
Svelte UI -> typed message handler -> command service -> undo action
          -> domain model -> graph builder -> published UI state
```

Required undoable operations include:

- add/remove/rename/reorder bus;
- add/remove/replace/reorder insert;
- bypass/unbypass insert;
- move insert between pre- and post-fader sections;
- change direct output;
- add/remove send;
- change send destination or pre/post position;
- remove bus and all resulting route repairs;
- change master or bus gain;
- explicit missing-plug-in replacement.

Continuous fader and send-level drags should coalesce into one undo action.
Opening or closing an editor is not project state and is not undoable.

Internal parameter changes made in a plug-in editor should mark Fiddle dirty.
Fiddle need not duplicate the plug-in's own parameter-level undo history in the
first implementation.

## 16. UI and interaction proposal

### 16.1 Design principle

Do not display a miniature insert rack, send knobs, routing selectors, and
bypass icons on every narrow strip. The strip should summarize; a generously
proportioned panel should edit.

Use text labels, large click targets, strong focus states, and full plug-in names.
Avoid icon-only controls except universally understood close, search, and drag
affordances, and give those accessible labels and tooltips.

### 16.2 Instrument-strip summary

Add one full-width row near the existing VSTi section:

```text
Audio FX · 2
```

Possible states:

- `Audio FX` when empty;
- `Audio FX · 2` when two inserts are present;
- `Audio FX · 2 · Bypassed` if the rack is globally bypassed later;
- warning colour and text if a slot is missing.

Do not put one tiny button per effect on the strip. Routing and sends can remain
inside the expanded panel unless a later usability test demonstrates a need for
one additional summary line.

### 16.3 Channel Audio panel

Clicking the summary row opens a resizable panel or sheet approximately
720-960 px wide where space permits. It should not be constrained to the
current narrow MIDI inspector width.

Recommended layout:

```text
+--------------------------------------------------------------+
| Channel Audio: Violin 1 - BBCSO                         Close |
+--------------------------------------------------------------+
| INSTRUMENT                                                   |
| BBC Symphony Orchestra                         [Edit]         |
+--------------------------------------------------------------+
| PRE-FADER INSERTS                                            |
| 1  Pro-Q 3                       [Bypass] [Edit] [Remove]      |
| 2  Pro-C 2                       [Bypass] [Edit] [Remove]      |
| [+ Add effect]                                               |
+--------------------------------------------------------------+
| FADER                                                        |
+--------------------------------------------------------------+
| POST-FADER INSERTS                                           |
| [+ Add effect]                                               |
+--------------------------------------------------------------+
| SENDS                                                        |
| Hall Reverb     enabled   -18.0 dB   Post                    |
| [+ Add send]                                                 |
+--------------------------------------------------------------+
| OUTPUT                                                       |
| Strings Bus                                                  |
+--------------------------------------------------------------+
```

Rows should be roughly 44-48 px high at 100% zoom. Prefer labeled buttons such
as `Bypass`, `Edit`, and `Remove` over tiny glyphs. Reordering may use a generous
drag handle plus keyboard Move Up/Move Down actions.

### 16.4 Effect picker

Reuse the successful searchable expression-map interaction pattern:

- a large search field focused on open;
- grouping by manufacturer or category;
- search across plug-in name, manufacturer, and category;
- keyboard navigation;
- clear compatibility and invalid-state messages;
- recently used effects at the top after the basic picker is stable.

The selected plug-in's native editor continues to open in its own window.

### 16.5 Bus presentation

Add a distinct, collapsible **Audio Buses** bank rather than interspersing buses
among orchestral families. Bus strips may be wider than instrument strips and
need only:

- bus name and `Group` or `FX` role;
- meter and fader;
- mute and, later, solo;
- `Audio FX · n` summary button;
- clear overload or missing-plug-in status.

Master is always present at the end of this bank.

Creating and managing buses should open a large **Bus Manager** panel. It should
support Add Group Bus, Add FX Bus, rename, reorder, colour, and remove. Bus
creation should optionally offer a convenience action:

```text
Create group bus from selected strips
```

This creates the bus and reroutes the selected strips as one undoable command.

### 16.6 Relationship to linked group controls

The existing group-master control should remain visually distinct from audio
buses and should be described as controlling layered-strip balance. It may
eventually be labeled or documented as a linked/VCA control.

Do not automatically create an audio bus for every visual instrument group.
That would add many invisible routing objects and change established gain
semantics. Offer an explicit convenience command instead.

### 16.7 Panel behavior

Recommended defaults:

- only one large Channel Audio or Bus Manager panel open in the main WebView;
- retain the selected channel while opening a native plug-in editor;
- Escape closes the topmost Fiddle panel, not the plug-in editor;
- remember the panel width;
- preserve keyboard focus when a plug-in picker closes;
- support Comfortable and Large UI settings without truncating action labels;
- avoid a modal panel during normal fader work if a resizable non-modal sheet
  can provide the same clarity.

A separate Cubase-like Audio Console window is a possible later evolution, but
the first implementation does not require another top-level window.

## 17. IPC and UI state

The mixer-state JSON should grow deliberately rather than exposing JUCE graph
internals.

Publish stable view models such as:

```text
ChannelAudioView
  stripId
  instrument summary
  ordered inserts
  sends
  output destination
  loading/missing/error state

BusView
  id, name, role, order
  gain/mute/solo/meters
  insert summary

AudioCatalogView
  plugin identity and metadata
  instrument/effect classification
  compatibility and validity
```

Meter updates should be a lightweight periodic message separate from full
topology/state serialization. Adding a plug-in must not cause the UI to receive
binary plug-in state.

## 18. Testing strategy

The audio engine must be testable without installed commercial plug-ins.

### 18.1 Test processors

Create deterministic JUCE test processors:

- impulse instrument;
- constant or sine instrument;
- fixed-gain effect;
- polarity inverter;
- fixed-latency pass-through;
- simple tail generator;
- stateful effect with serializable state;
- failing or incompatible processor.

### 18.2 Graph and DSP tests

Verify sample-by-sample where appropriate:

- instrument -> Master direct path;
- ordered insert processing;
- bypassed insert transparency;
- group-bus summation;
- post-fader send level;
- pre-fader send independence from channel fader;
- aux return summed with dry path;
- mute and solo semantics;
- effect tails after input stops;
- stereo channel preservation;
- overload metering;
- latency alignment across parallel dry and wet paths;
- graph latency changes followed by rebuild;
- illegal routes rejected before graph construction.

### 18.3 Realtime tests

- no heap allocation in steady-state `processBlock` for Fiddle processors;
- no graph or project locks from the audio callback;
- repeated graph edits while audio runs;
- remove a bus while its effects have active tails;
- remove a plug-in while its editor is open;
- asynchronous load completing after its owning channel was deleted;
- repeated prepare/release cycles and changing block sizes;
- object reclamation only after audio-thread quiescence.

### 18.4 Persistence tests

- clean initialization of the first audio-routing schema;
- predictable rejection or reset of unsupported pre-audio test state;
- round trip of multiple repeated effect instances;
- slot ordering and pre/post position;
- bus ordering and naming;
- send and output references;
- missing plug-in state preservation;
- graph-state hash changes for every audible setting;
- unchanged binary plug-in state deduplicated by hash;
- Dorico state round trip;
- merge or restore with a missing referenced bus repaired predictably.

### 18.5 Command and undo tests

Every topology mutation should have an execute/undo/redo test. Compound bus
removal must restore all affected routes and sends exactly.

### 18.6 UI tests

- effect search, grouping, and compatibility filtering;
- insert add/remove/reorder/bypass;
- focus restoration and keyboard movement;
- missing plug-in presentation;
- large labels at all strip-size and zoom combinations;
- panel resizing and persistence;
- no tiny unlabeled action controls;
- multi-selected strips routed as one compound operation.

### 18.7 Manual plug-in matrix

Maintain a small repeatable smoke-test matrix using available plug-ins:

- one zero-latency EQ;
- one compressor with lookahead latency;
- one reverb with a tail;
- one limiter on Master;
- one plug-in that reports state changes correctly;
- one that requires fallback polling;
- one missing-plug-in restore scenario.

## 19. Delivery plan

### Phase 0: approve the design (complete)

- The proposal has been reviewed and accepted.
- The implementation decisions are recorded in section 21.
- Milestone acceptance criteria are recorded in the phases below.

### Phase 1: audio-graph spike (complete)

Build a non-UI prototype using `AudioProcessorGraph` and only deterministic test
processors:

- one scheduled MIDI source;
- one instrument node;
- one insert;
- one send branch;
- one aux return;
- one master effect;
- stereo output into an offline buffer;
- latency-compensated summing.

Acceptance criterion: deterministic graph tests pass under repeated topology
rebuilds, and no Fiddle processor allocates during steady-state rendering.

Do not alter project persistence in this disposable spike unless the graph
approach is accepted.

Result: the spike met its acceptance criterion. Its conclusions, limitations,
and the retained regression coverage are recorded in
[`audio-graph-spike-results.md`](audio-graph-spike-results.md).

### Phase 2: hosted plug-in abstraction and catalog (complete)

- Extract reusable hosted plug-in slots from `MixerStrip`.
- Cache and publish `isInstrument` and compatibility metadata.
- Generalize editor and state-listener ownership by `PluginSlotId`.
- Add missing-slot representation.
- Establish stereo bus-layout negotiation.

Acceptance criterion: instrument selection still works, and one real VST3 audio
effect can be instantiated, edited, bypassed, and state-restored in an isolated
test host.

Implementation result: the reusable host, real-effect acceptance test, and
existing Fiddle instrument workflow all pass. See
[`hosted-plugin-slot-results.md`](hosted-plugin-slot-results.md).

### Phase 3: Master effect rack vertical slice (implementation complete)

- Add permanent Master state and graph stage.
- Add master insert commands, undo, persistence, and Channel Audio panel.
- Route the existing strip mix through the graph to the Dorico return.

Master is the smallest user-visible effect path and validates the entire stack
without yet changing strip routing.

Acceptance criterion: a limiter or EQ on Master survives restart and Dorico
project restoration, and graph latency is reflected in render timing.

Implementation result: the permanent stereo graph, ordered effect rack,
commands and undo, versioned persistence, Dorico state-blob restoration,
latency compensation, and Master Audio panel are implemented. Automated
coverage is recorded in
[`master-effect-rack-results.md`](master-effect-rack-results.md); the final
real-plug-in restart and Dorico restore checks remain part of manual release
testing.

### Phase 4: instrument-strip inserts (in progress)

- Move each instrument render path into the graph.
- Add pre/post insert racks to strips.
- Add per-channel audio panel and effect picker.
- Preserve existing gain, mute, solo, expression-map, and group-link behavior.

Acceptance criterion: different strips can host different ordered effect racks
without cross-talk, and all current mixer tests continue to pass.

Phase 4A foundation: every instrument now renders into an independent stereo
JUCE graph before being summed into Master. The graph currently contains the
strip fader; mute, solo, and library activation produce zero graph output while
the instrument path continues processing. This establishes the insertion point
for an ordered rack without changing the Master path. See
[`strip-audio-path-results.md`](strip-audio-path-results.md).

### Phase 5: group buses

- Add bus model, Bus Manager, bus strip bank, and output routing.
- Implement convenience creation from selected strips.
- Implement compound removal and undo.

Acceptance criterion: multiple strips can route through a compressed group bus
to Master with correct mute, solo, meter, state, and latency behavior.

### Phase 6: aux/FX buses and sends

- Add post-fader sends first.
- Add aux returns and solo-safe behavior.
- Add pre-fader send option after post-fader behavior is stable.

Acceptance criterion: several strips share one reverb, retain dry output, and
restore exact send levels and reverb state.

### Phase 7: hardening and polish

- CPU diagnostics and overload reporting;
- graph-change ramps and failure recovery;
- broader plug-in compatibility matrix;
- improved missing-plug-in replacement;
- accessibility and keyboard review;
- documentation and user workflow examples.

## 20. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Plug-in crashes FiddleServer | Preserve current scan validity handling; consider out-of-process hosting only as a later project |
| Graph edits click or drop audio | Asynchronous load, batched rebuild, short affected-path ramps, stress tests |
| Vendor reports wrong category/layout | Compatibility probe plus explicit Show All escape hatch |
| Plug-in latency shifts playback | JUCE path compensation plus overall graph-latency scheduling adjustment |
| Saved projects lose unavailable effects | Persistent missing slots with untouched state blobs |
| Routing model becomes incomprehensible | One direct output, aux-only sends, no nesting or feedback initially |
| UI becomes dense | One summary row per strip; large searchable pickers and audio panels |
| State size grows rapidly | Content-address and deduplicate binary plug-in state |
| Bus deletion corrupts routes | Domain validation and compound transactional undo |
| Realtime lifetime bugs | Message-thread mutation, graph render-sequence exchange, deferred reclamation, stress tests |

## 21. Confirmed decisions

The following recommendations are accepted as the implementation defaults:

1. **Current linked group controls**

   Decision: retain them as VCA-like linked controls. Do not convert them
   implicitly into audio buses.

2. **First user-visible milestone**

   Decision: Master effect rack first, then strip inserts, then buses.
   This validates hosting and persistence with the least routing disruption.

3. **Audio editing surface**

   Decision: a large resizable panel in the main window initially. A
   separate Audio Console window can follow if routine use demonstrates a need.

4. **Backward migration**

   Decision: do not implement it. There are no substantive Fiddle projects to
   preserve; current test projects and databases can be rebuilt quickly. Begin
   with a clean, explicitly versioned audio-routing schema.

5. **Insert positions**

   Decision: persist pre/post position from the start. Default normal
   channel inserts to pre-fader and final Master dynamics/metering to post-fader.

6. **Initial bus nesting**

   Decision: no nested group buses. Add only after concrete workflows
   justify the additional complexity.

7. **Host wet/dry control**

   Decision: defer it. Use the plug-in's own mix control, with aux effects
   configured wet-only.

8. **Pan**

   Decision: reserve its place in the model and channel stage, but do not
   expand this project solely to add a pan UI before effects and buses work.

## 22. Recommended immediate next step

Implement Phase 1 as a narrowly scoped audio-graph spike with fake processors
and offline tests. The spike should answer these questions before production
refactoring begins:

- Can scheduled MIDI be isolated per instrument node cleanly?
- Does JUCE graph latency compensation behave correctly for Fiddle's dry/send
  topology?
- Can graph rebuilds occur during continuous rendering without blocking the
  audio callback?
- Can Fiddle's current editor/state ownership be generalized without unsafe raw
  pointers?
- What exact scheduling adjustment is required to keep effect latency inside
  Fiddle's configured playback delay?

If the spike succeeds, retain its tests and discard or refactor its prototype
code into the production `AudioEngine` deliberately.
