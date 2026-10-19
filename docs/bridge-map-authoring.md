# Bridge map authoring and project history

Implemented 2026-10-16. Open **View → Windows → Technique Catalog → Bridge maps**.
No catalog scan is needed. The workspace offers Strings r3, Woodwinds r1 and
Brass r1 starting maps, `.doricolib` import/export, an advanced XML editor, and
versioned port/channel assignments. The templates retain their inspection names
and descriptions: their source files and historical captures remain frozen.
The compiler/decoder now exists; production Dorico acceptance is still pending.

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

1. Assign revisions to the port/channels shown in **Chairs**, then select
   **Chairs → Install Playback Template**. The installer writes the pinned maps
   and references them in both Dorico's endpoint configuration and preset library.
   Restart Dorico and apply the **Fiddle** playback template. Manual map
   export/import/assignment is also supported. Updating library files does not
   update a map already embedded in a Dorico project.
2. Export that assigned map from Dorico. Select **Verify assigned map** for the
   corresponding Fiddle input and choose the export. This compares contents and
   records your confirmation of the assignment; Fiddle has no API that reads
   Dorico's actual assignment automatically.
3. Start playback so Dorico sends the map's Init and current commands. The
   decoder requires known state for every conduit before accepting notes.
   Stopping clears received state; the next playback must reconstruct it.

Verification is session-only. Opening a score, checking out a version, or
changing the bridge registry clears it. A mismatching export revokes a previous
check. Assigned channels with unverified or incomplete state withhold musical
notes. Unknown command values invalidate their dimension rather than falling
back to Natural. Command-note releases are consumed as physical releases and
do not clear a sticky articulation. Unassigned inputs retain legacy decoding.

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
2. Note the destinations in Chairs. Under **Technique Catalog → Bridge maps**,
   add Strings, Woodwinds and Brass, and assign them to those destinations.
   Use the displayed ports/channels rather than assuming contiguous allocation.
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
5. Export each assigned expression map from Dorico separately, and use
   **Verify assigned map** in Fiddle for its corresponding input. A successful
   installation does not perform this check. If an export does not match, retain
   it for inspection rather than bypassing the check. Opening/reopening a score
   clears prior verification, so verify after the new score has connected.
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

## Supported compiler schema 1

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

The fingerprint includes all selected-map fields, attributes and ordering,
decoder schema, and declared range. Format-only whitespace is normalized.
This is deliberately conservative: a Dorico rewrite that changes ordering or
metadata may require a new reviewed revision even if MIDI behavior appears equal.
Matching fingerprints establish matching export contents, not a live host
handshake or proof of audible playback.

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
