# Library percussion maps — design

Status: **approved and implemented** (2026-11-01).

## Where it lives

| Piece | Code |
| --- | --- |
| Model, parser, matcher | `Source/Server/PercussionMap.{h,cpp}` |
| Playback | `Source/Server/PercussionMapAnnotator.h`, inserted by `MixerStrip::rebuildAnnotatorChain` |
| Storage | `library_percussion_maps`, `library_patches.percussion_map_id`, `layers.percussion_map_id` (`LibraryRoutingRepository`); `strip_blobs.percussion_map_source` (project versions) |
| IPC | `MainComponent::setupPercussionMapHandlers` (`requestPercussionMaps`, `savePercussionMap`, `deletePercussionMap`, `choosePercussionMapFiles`, `scanDoricoPercussionMaps`, `importPercussionMaps`, `exportPercussionMap`, `loadPercussionMap`, `clearPercussionMap`, `loadPercussionMapFromFile`) |
| UI | `PercussionMaps.svelte` (Library Manager section), `PercussionMapPicker.svelte` (patch rows, Layer Details), `percussionMaps.js`, Note Inspector "Percussion:" row |
| Tests | `Source/Server/tests/PercussionMapTest.cpp` (VSL fixtures in `example-expression-maps/percussion/`), `LibraryRoutingRepositoryTest`, `ui/tests/percussionMaps.test.js` |

Not verified in Dorico or with a live sample library: Fiddle only reproduces
Dorico's documented and probed behaviour (results-01). The keyswitch field is
written as Dorico writes it (one number); several keyswitches are written
comma-separated, a format no Dorico file has shown.

## Problem

Every unpitched note reaches a layer as pitch 36 plus its decoded playing
techniques (`pt.rim`, `pt.open2`, `pt.snareOff`, `pt.tremolo`, …). That comes
from the bridge's own maps ("Orchestral unpitched percussion r2"). A sample
library wants a different pitch per stroke, and sometimes a keyswitch. In
Dorico that is the job of a **percussion map** (`DrumKitNoteMapDefinition`),
which sits beside the library's expression map. Fiddle has no percussion
maps on the library side, so unpitched layers play the wrong samples.

## What a library percussion map is

Exactly Dorico's `DrumKitNoteMapDefinition`, so vendor maps import losslessly
and the matching rule is Dorico's:

| Field | Meaning |
| --- | --- |
| `entityID`, `name`, `version` | Identity (vendor maps keep their own ID, e.g. `drumkitmap.user.vsl_sy_percussion_snare_drum`) |
| `forSingleInstrument` | Kept for round-trip; does not affect playback |
| `entries[]` | `instrumentEntityID`, `techniqueIDs` (a `+`-joined set), `pitch`, optional `keyswitches`, `name`, optional `variant` |

No velocity field. Dorico's percussion maps have none, and the layer's
expression map already scales velocity per switch.

Evidence: Dorico 6's `drumKitNoteMaps.xml` (10 maps, 522 entries) and the
user's 11 VSL Synchron Percussion `(PM).doricolib` files use exactly these
fields:

- Entries with combinations are common (the cymbal map has 18 of 32).
- Keyswitches are single numbers (VSL uses 24 and 26).
- `variant` appears only in Dorico's GM and XG maps ("electric", "soft", "2").
- Multi-instrument maps exist: VSL toms list five `…tomtom.*.alias.tom-*`
  IDs, and VSL woodblocks list six.
- Each vendor map is paired with an expression map. For example, "VSL SY
  Percussion – Snare drums, Concert toms" switches beaters and snares off
  with keyswitches, and its description tells the user to assign the
  matching percussion map too.

## Matching (per note)

Input: the chair's Dorico instrument ID and the note's technique set (as
`collectInputTechniqueIds` gives it today).

1. **Entries for this instrument.**
   - Use entries whose `instrumentEntityID` equals the chair's ID.
   - If none match, compare with any `.alias.*` suffix stripped on both
     sides. (VSL's tom map lists aliases; a chair may hold the plain ID.)
   - If still none match and the map covers only one instrument, use its
     entries anyway. The user picked this map for this layer, so a snare map
     on a "Field drum" chair still plays.
   - Entries with a `variant` are used only when the instrument has no
     entries without one.
2. **Natural is implied.**
   - Remove `pt.natural` from the note's set and from every entry's set.
   - For the six Dorico instruments with a *natural equivalent*, that
     technique also counts as natural. Hi-hat → `pt.closed`, pedal hi-hat →
     `pt.pedal`, triangle and surdo → `pt.open2`. (The other two are
     upright-bass pizzicato, which is not percussion.) This table is compiled
     in, taken from `instruments.xml`.
   - This is why Dorico's GM map, which has only `pt.closed` and `pt.open2`
     for the hi-hat, still plays a plain hi-hat note. It also covers the
     score-test finding that Dorico sends hi-hat Closed as Natural.
3. **Best subset.**
   - Candidates are entries whose (reduced) set is a subset of the note's
     (reduced) set.
   - The candidate with the most techniques wins, so an exact combination
     beats its parts (results-01, U3).
   - On a tie, the entry listed first wins.
   - The empty set is the Natural entry, so a technique the map lacks falls
     back to Natural. Example: snare `{natural, rim, accent}` → the `pt.rim`
     entry (61).
4. **No candidate at all** (the map has no Natural entry for this
   instrument):
   - The note is withheld.
   - The Note Inspector says "no percussion-map entry for
     ⟨instrument⟩ ⟨techniques⟩".
   - Passing pitch 36 through would usually trigger an unrelated sample.

The result is cached per (technique set), like the expression-map resolver.

## Playback: how it composes with the layer's expression map

Dorico shows that both maps see every technique (results-01, U1). The
percussion map picks the pitch and the expression map does everything else.
Fiddle does the same: a new `PercussionMapAnnotator` goes into the layer's
annotator chain, **before** the expression map:

```
[Lua plugins] → PercussionMapAnnotator → ExpressionMapAnnotator (or Passthrough)
```

- **Pitch.** `note_number` becomes the entry's pitch. The expression map
  then sees that pitch: its `pitchRange` conditions and `transpose` act on
  the library pitch.
- **Keyswitches.**
  - They are momentary, as Dorico sends them (U4): note-on (velocity 127)
    at the note's own time, ahead of the note in send order, and note-off
    with the note's note-off.
  - They do not take part in the expression map's latched-keyswitch
    bookkeeping, which releases a latched key only when the next switch
    changes.
  - This needs two plain instruction types, `NOTE_ON` and `NOTE_OFF`, in
    `MidiInstruction`. Today `KEY_SWITCH` always latches.
- **Note-off.**
  - The annotator remembers each sounding note's mapped pitch, keyed by note
    ID, channel and incoming pitch. At note end it restores that pitch and
    adds the keyswitch note-offs.
  - So a technique change during the note, or a later note, cannot strand a
    note-off. The expression map's own `sounding_` table keys on the mapped
    pitch, which stays consistent.
- **Dynamics, switches, durations.** These stay with the layer's expression
  map:
  - Hits take velocity and rolls take CC1, per that map's volume types. The
    bridge already marks each note's dynamics mode.
  - Snares off and beaters are add-on switches, as in VSL's map.
- **No expression map on the layer.** The percussion map still applies, and
  dynamics pass through (Passthrough).
- **No percussion map on the layer.** Behaviour is unchanged: pitch 36 and
  the techniques pass through.
- **Rolls.**
  - Dorico sustains a roll only when the chosen entry carries the roll.
    Otherwise it plays repeated 100 ms strokes at the matched pitch
    (results-01, finding 2).
  - Fiddle always receives one sustained roll note from the bridge. If the
    matched entry carries a roll technique (`pt.roll`, `pt.tremolo`,
    `pt.buzzRoll`, …), the layer plays one sustained note, exactly as Dorico
    would.
  - If it does not (a library map with no roll entry), v1 still plays one
    sustained note at the matched pitch and flags "roll without a roll
    entry" in the Note Inspector. Generating strokes is a follow-up.
- **Note Inspector.** The decision record gains the map name, the winning
  entry (name, techniques), the pitch and the keyswitches.

## Storage and selection

Follows the expression-map pattern (layers reference a map by entity ID, the
library patch supplies the default, the layer can override it). One
difference: percussion maps are **Fiddle-owned**, because they must be
creatable and editable in Fiddle.

- **SQLite (`LibraryRoutingRepository`).**
  - New table `library_percussion_maps(entity_id PRIMARY KEY, name, xml,
    revision, updated_at)`. The canonical content is the
    `DrumKitNoteMapDefinition` XML, so import and export are lossless.
  - New columns `library_patches.percussion_map_id` and
    `layers.percussion_map_id`, next to `expression_map_id`.
- **Library patch default.** A patch can name a percussion map. A layer
  created from the patch copies it, like `expression_map_id`. The same
  layer-differs-from-patch comparison applies, and so does refresh from the
  library.
- **Layer override (fader strip).** The `LayerDetailsPanel` has a percussion
  map picker below the expression map picker. It is shown when the layer's
  chair is an unpitched instrument (`bridgeSectionForInstrument(...) ==
  UnpitchedPercussion`). It offers Choose, Clear and Load from file, and is
  undoable on the project stack.
- **Strip.**
  - `MixerStrip` gains `percussionMap` (the compiled map) and
    `instrumentEntityId`. Both are set in `instantiateLayer` from the chair
    row and the layer row.
  - `rebuildAnnotatorChain` inserts the annotator when both are present.
  - The project snapshot (`VersionTypes`) records the percussion map's
    entity ID and XML, so restoring an old version plays the map it had.
- **Library edits propagate.** Saving a map recompiles it, and every live
  strip referencing that entity ID gets the new version (the same path as
  refresh from the library).

## Library Manager

A new **Percussion Maps** section:

- **List.** Name, entity ID, instruments covered, entry count, and which
  patches use the map.
- **New.** Starts with one Natural entry for a chosen instrument. Fiddle
  generates the entity ID (`drumkitmap.user.fiddle.<slug>`).
- **Duplicate** and **Delete.** Delete is refused while a patch or layer
  references the map, and the refusal lists them.
- **Edit.**
  - Rename the map.
  - Entry table with these columns: instrument (from Dorico's unpitched
    instruments), techniques (multi-select from the technique catalog),
    pitch (number + note name), keyswitches, entry name.
  - Add, remove and reorder rows. Order matters for ties.
  - Validation: pitch and keyswitches 0–127, and no duplicate technique set
    per instrument and variant.
- **Import.**
  - Pick one or more `.doricolib` files. Every `DrumKitNoteMapDefinition`
    in them is imported; expression maps in the same file are ignored.
  - An existing entity ID asks before replacing; a newer `version` is
    offered as an update.
  - A shortcut lists the `(PM)` files under Dorico's `DefaultLibraryAdditions`
    (where the user's VSL maps live) for one-click import.
- **Export.** Writes a `.doricolib` holding the map, for use in Dorico
  directly.
- **Patch editor.** A percussion map picker beside the expression map
  picker.
- **Undo.** Edits go on the library undo stack (`libraryUndoManager_`), like
  catalog saves.

## Tests

- **`PercussionMapTest` (C++, `stable` label).**
  - Parser and serializer: Dorico GM, a VSL snare fixture and a VSL tom
    fixture copied into `Source/Server/tests/fixtures`; round-trip identity.
  - Matching:
    - exact combination
    - best subset
    - ties by order
    - Natural fallback
    - natural equivalent (GM hi-hat plain note → 42)
    - alias stripping
    - single-instrument fallback
    - variant preference
    - no entry → withheld
- **Annotator and chain.**
  - Pitch is mapped, and momentary keyswitches go out on note-on and
    note-off.
  - Composition with an `ExpressionMapAnnotator`:
    - a beater add-on keyswitch plus the stroke pitch
    - a roll keeps its CC1 dynamics and pitchRange on the mapped pitch
  - The note-off restores the right pitch after a technique change.
- **`LibraryRoutingRepositoryTest`.** Table, columns, migration, and layer
  defaults from the patch.
- **UI `node:test`.** Map model validation, entry editing, and picker
  visibility on unpitched chairs.

## Out of scope (follow-ups)

- Generating roll strokes when the matched entry has no roll technique.
- Drum-set kits (several instruments on one chair, by pitch).
- Velocity per entry.
