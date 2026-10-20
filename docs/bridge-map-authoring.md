# Bridge maps: section profiles, installation and project history

**Workflow (2026-10-19).** Fiddle has one bridge map per orchestral section:
strings r4, woodwinds r2, brass r2 and pitched percussion r1 (all wire schema 2).
**Chairs → Install Playback Template** chooses the map for every chair from its
Dorico instrument (`BridgeSections.h`), writes the maps into the Fiddle playback
template, and pins the four revisions with the project's branch/version. Every
other chair keeps the universal Fiddle map. Users do not assign ports, channels
or maps. A branch keeps the revisions it pinned even when the app ships newer
built-ins; changing a map is future editor work.
**View → Windows → Technique Catalog → Bridge maps** is read-only. It shows the
four section maps (with Dorico export) and each routed channel's live status.
The compiler/decoder exists; production Dorico acceptance is still pending.

The manual workspace described below (starting-map buttons, per-port
assignment, advanced XML publishing, export verification, bundle import) was
removed from the UI on 2026-10-19. `BridgeMapCommands` keeps the publish,
import and verify operations for tests and a future editor.

## Source and revisions

The stored Dorico expression-map XML is authoritative. Publishing validates it
and derives a decoder from its actual On/Off actions, canonical PT IDs, explicit
length conditions, dynamics policy and musical range. Mutex groups affect
Dorico's selection/cancellation rules; they do not define decoder dimensions.
The decoder groups commands by CC or keyswitch conduit. In particular, it does
not infer length from CC102 modulo five.

Edit the selected map under **Advanced XML draft**, then **Publish new revision**.
Publication creates new Dorico map and mutex-group IDs and increments the revision. Existing
revisions and assignments remain intact. Assign the new revision explicitly
when ready. Undo/Redo includes publication and assignments. Draft text is not
saved until published. A structured technique/dimension editor is future work.

Each assigned input supports **one Dorico voice per port/channel**. Port numbers
in this UI are 1–16; internal ports are 0–15. Channels are 1–16 throughout.
Port 1/channel 1 is reserved for the metronome. Assignments precede layer fan-out,
so every layer listening to that input receives the same decoded intent.

## Cooperating with Dorico

1. Select **Chairs → Install Playback Template**. The installer assigns each
   section chair its map, writes the pinned maps, and references them in both
   Dorico's endpoint configuration and preset library.
   Restart Dorico and apply the **Fiddle** playback template. Updating library files does not
   update a map already embedded in a Dorico project.
2. Start playback so Dorico sends the map's Init and current commands. For a
   schema-2 map, Init includes the **revision beacon** (KS126/KS127). If it
   matches the assigned revision, the input is verified for that playback; the
   Bridge maps status shows **Beacon matches**. A mismatch means Dorico is
   playing a different map: notes on that input are withheld and the row shows
   the received and expected beacon. This is evidence from the live MIDI stream,
   not from the exported file.
3. Optionally, or for schema-1 maps, export the assigned map from Dorico and
   select **Verify assigned map**. The comparison uses the map's *semantic*
   fingerprint, so Dorico reordering rows or renaming the map does not fail it.
   You attest that the export is the assigned map. A beacon mismatch overrides
   this attestation.

Stopping clears received state, including the beacon; the next playback must
reconstruct it. Attestation is session-only: opening a score, checking out a
version, or changing the bridge registry clears it, and a mismatching export
revokes it. A note plays once its input is verified and the base articulation
has arrived. Add-on dimensions Dorico has not sent count as unspecified, which
is what Init sends. Unknown command values invalidate their dimension rather
than falling back to Natural. Command-note releases are consumed as physical
releases and do not clear a sticky articulation. Unassigned inputs retain
legacy decoding. Bridge maps reports, per input, the last reason a note was
withheld and counts of withheld notes, invalid commands, stale Offs ignored and
early Offs suppressed. Consumed commands also appear in the debug MIDI feed,
flagged `bridgeCommand`.

Stop playback and release auditioned notes before editing, assigning or
verifying. Keep Auto mutual exclusion and Apply Stage Template Settings off in
the bridge map, with Init enabled and explicit mutex groups. The compiler checks
these map settings. Project-wide playback options, generated trills/ornaments,
and downstream instrument behavior are separate concerns; this workspace does
not certify them or change Dorico preferences. Use explicit successor techniques
where the score requires a return after a temporary technique; automatic returns
still need host acceptance within the supported one-voice scope.

## Template smoke test

The template installer is now connected to project bridge assignments. The
following is the first host smoke test, not a claim that it has already passed.

1. Save work and reopen the rebuilt Fiddle app. Use a separate test branch/project
   with three chairs: violin, flute and horn. Use Solo players for flute and horn;
   use the same solo/section violin choice when creating the Dorico score. Assign
   a working sound layer and instrument expression map to each chair. A chair
   without a sound layer will not produce audio.
2. Nothing to assign: the installer derives each chair's map from its instrument.
3. With Dorico closed, select **Chairs → Install Playback Template**. Installation
   stages a complete package, retains older immutable bridge files, and restores
   replaced files if publication fails. Changing a pin marks the template as
   needing reinstallation. The older ensemble/library rebuild commands direct
   projects with bridge documents to Chairs rather than reallocating inputs.
4. Open Dorico and create a small score with the three matching players. Apply
   the **Fiddle** playback template. Check that violin uses Strings, flute uses
   Woodwinds and horn uses Brass at the expected destinations. Use the normal
   Fiddle plugin and one voice per channel. Keep the current Fiddle test branch
   selected so the new score connects to that setup.
5. Start playback once and check that each input in **Bridge maps** reports
   **Beacon matches**. A successful installation does not perform this check.
   If a beacon mismatches, Dorico is playing another map on that input: export
   it from Dorico, keep the file for inspection, and use **Verify assigned map**
   to compare it rather than bypassing the check.
6. Write four bars of repeated notes in a comfortable register. Use ordinary
   quarter notes, short staccato notes, a half note and a whole note; keep one
   voice in each part. Play from the beginning, stop/restart, and start at bar 3.
   Check audio, note lengths and the incoming/selected techniques in Fiddle.
7. Once the base test works, try violin non-vibrato → con vibrato and horn muted
   → open, with explicit successor markings. Choose articulations supported by
   the sound layer when judging audio. Compare decoded PTs separately from
   whether the particular library implements the requested sound. Natural is
   not a blanket add-on reset.
8. Save the score and Fiddle version, reopen, verify the maps again, then replay.
   The pinned source/revision should restore without referring to an external
   candidate file. Record failures by input, bar/note, received commands and
   decoded PTs, including whether starting from the beginning changes the result.

No acceptance evidence is implied by the automated installer tests: Dorico's
initialization, export normalization and playback behavior remain to be observed.

## Supported compiler schemas 1 and 2

The compiler accepts one flattened map with independent CC102–119 and positive
velocity-coded keyswitch actions outside the declared musical range. The base
writes CC102 and has one explicit note-length condition per code; add-ons own
one conduit each, with consistent reset values. Init must set Natural and clear
every add-on conduit. Ambiguous codes, duplicate switch IDs, conflicting map
identities, unsupported fields/actions, inheritance, pitch/velocity filters,
non-neutral timing/length/velocity modifiers, monophonic behavior and playback
overrides are rejected. CC1 or velocity dynamics and note-velocity attack are
supported. The observed base flags 0/1 are preserved as Dorico selection metadata.
Rejection leaves the existing published project unchanged.

**Schema 2** is declared by Init actions on KS126 and KS127 (both or neither).
Their velocities must equal the beacon derived from `entityID:version`
(SHA-256, first four bytes, mod 127², encoded as two velocities 1–127).
Publishing through Fiddle restamps the beacon whenever it rewrites the identity,
and also when an imported or edited map's beacon is stale. In schema 2 every
add-on Off must equal On + 64 (On values below 64); the reset is CC 0 or KS
velocity 1, written by Init. No add-on may use KS126/127, and the musical range
must end below 126.

The content fingerprint includes all selected-map fields, attributes and
ordering, decoder schema, and declared range; format-only whitespace is
normalized. It identifies a stored revision (project keys, installed file
names), and schema-1 fingerprints are unchanged from before. A separate
**semantic fingerprint** covers only what Dorico or the decoder act on: identity
and version, base rows (techniques, length, flags, dynamics, action), add-on
techniques and actions, mutex groups and Init. It ignores display text,
formatting and row order, and is what **Verify assigned map** compares. Neither
proves audible playback; the beacon is the live check.

## What reaches instruments

Besides notes and bridge commands, the plug-in maps every CC 0–119 plus channel
pressure and pitch bend through VST3 MIDI mapping. Channel-mode messages
120–127 are excluded. The 21 original controllers keep their parameter IDs and
remain automatable; the added ones are hidden parameters, a choice still to be
checked in Dorico. Non-bridge CCs pass to every layer. Pitch bend and pressure
take the annotator path: a layer map whose `pitchBendRange` differs from the
bridge's ±2 semitones gets bend rescaled. Live CC1 dynamics are re-expressed on
the controller(s) the current base switch's `volumeType` names, while notes
sound, when that base uses CC dynamics.

Each layer then plays its own (library) expression map the way Dorico would
play that map directly:

- **Switch choice.** Dorico's resolution order (exact base, base + add-ons,
  two of three, technique priority, fallback, Natural) runs on the decoded
  techniques. A base switch only applies to notes inside its `velocityRange` and
  `pitchRange`, judged on Dorico's velocity and pitch before the switch's
  velocity factor or transpose. The priority tier knows Dorico's canonical
  harmonic, pizzicato and mute IDs (`pt.naturalHarmonic1`, `pt.cupMute`,
  `pt.harmonMuteStemIn`, …) as well as the older lowercase spellings.
- **On and Off actions.** A switch's On actions are sent when the selected
  switch (base or add-on set) changes, not for repeated notes. Its Off actions
  are sent at that change, before the new switch's On actions, for each base or
  add-on that is no longer selected. A note ending never sends Off actions, so an
  overlapped note ending cannot undo its successor's switch. Keyswitches go at
  the note's time, ahead of it in send order; there is no extra fixed lead.
- **Channel switches.** A `kChannelSwitch` action sends no message of its own.
  It moves the note, the switch's other actions, its dynamics and live
  controllers to that channel; the note-off follows its note-on there. Fiddle
  reads `param1` as an absolute 1-based MIDI channel. A switch without a channel
  action plays on the layer's input channel.
- **Timing.** `millisecondsBefore` plus `ticksBefore` (converted at the live
  tempo, assuming 480 ticks per quarter note) play the note and its switch
  actions that much earlier; with several switches selected the largest lead
  wins. The note-off moves too only with `applyMillisecondsBeforeToEndOffsets`,
  and live controllers only with `applyMillisecondsBeforeToControllers`.
  Fiddle's playback delay supplies the lookahead.
- **Durations.** Dorico has already applied the project's playback options
  (note, staccato, staccatissimo, legato, tenuto and marcato duration) to the
  durations it sends, because bridge maps carry no overrides. A layer map's
  `playbackOptionsOverrides` therefore scale a duration by override ÷ Dorico
  default (90, 50, 25, 105, 100 and 75%), and categories the map doesn't override
  are left alone. Fiddle cannot see the project's own options, so it assumes
  they are Dorico's defaults. The switch's `lengthFactor` then applies as usual.
- **Initialization.** When Dorico's transport starts, each layer sends its
  map's enabled `initSwitchData` actions on its input channel and forgets the
  switch state left from the previous playback, so the first note sends its
  switch again.
- **Not emulated.** `monophonic` (Dorico's monophonic legato) is parsed and
  shown but not applied: overlapping notes reach the instrument exactly as
  Dorico sent them through the bridge.

## Persistence and portability

Project settings store the complete source XML, schema, fingerprints, ranges and
endpoint assignments. These settings participate in content hashes, commits,
checkout, SQLite persistence and the existing full-state blob format. Older
projects without bridge documents retain their existing settings serialization
and hashes. Divergent changes to bridge settings on both branches require manual
reconciliation before merging; the version store does not choose a decoder.

**Export project bundle** saves a portable `.fiddlebridge` containing bridge
documents and assignments. Import explicitly replaces the project's bridge
registry and is undoable. It does not include mixer state, plugins or history.
Import recompiles sources and checks fingerprints before changing live state.

The native plugin's Dorico state still identifies the exact Fiddle branch/version;
it does not embed a complete portable Fiddle snapshot. Reopening a Dorico project
on the same installation restores the pinned revision from Fiddle's version
database. A `.dorico` alone is not yet a self-contained Fiddle backup: retain the
Fiddle database and export bridge bundles when transferring projects. Embedding
a recoverable full project snapshot in host state remains future work.

The current Assigned-map audit/Repair plan tabs still inspect the legacy decoder.
They do not yet audit these per-input compiled revisions. Template installation
uses project pins; production host acceptance remains separate work.
