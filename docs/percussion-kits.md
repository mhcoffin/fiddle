# Percussion kits

Status: draft design, 2026-11-07. Not implemented. Phase 0 (a probe score) has
to answer the open questions before anything is built.

## Summary

A Dorico percussion kit is several unpitched instruments played by one player
on one channel. "Wood blocks" (`instrument.kit.wood.blocks`) is five wood
blocks on a five-line staff, played in Dorico through one expression map and
one percussion map. The percussion map decides which sample each note plays:
the instrument (which block) and the techniques (rim, roll, …) together pick a
pitch.

Fiddle can't play kits yet. A chair is one instrument, and the unpitched
bridge sends every instrument's notes at the same pitch, so on a kit's channel
Fiddle can't tell one block from another.

The plan: give every unpitched instrument **its own pitch** in the bridge's
percussion map. A single instrument works as now, and a kit works with no
kit-specific map: Fiddle turns each note's pitch back into the instrument that
played it, then each layer's library percussion map picks the sample for that
instrument, as it already does.

## An example: VSL's wood blocks

`VSL SY Percussion - Woodblocks (PM).doricolib`, in Dorico's
`DefaultLibraryAdditions/Synchron Percussion I`, is a typical vendor kit map:

- `drumkitmap.user.vsl_sy_percussion_woodblocks`, version 1,
  `forSingleInstrument` false, 36 entries, no keyswitches or variants.
- Five instruments, `instrument.percussion.other.wood.block.low`,
  `.medium.low`, `.medium`, `.medium.high` and `.high`, plus the kit's own
  ID, `instrument.kit.wood.blocks`.
- One octave per block, the same five strokes in each (Dorico pitch names,
  middle C = C3):

  | Block | Natural | Rim | Right hand | Right hand + rim | Roll / tremolo |
  | --- | --- | --- | --- | --- | --- |
  | Low | 36 (C1) | 37 | 38 | 39 | 40 |
  | Medium-low | 48 (C2) | 49 | 50 | 51 | 52 |
  | Medium | 60 (C3) | 61 | 62 | 63 | 64 |
  | Medium-high | 72 (C4) | 73 | 74 | 75 | 76 |
  | High | 84 (C5) | 85 | 86 | 87 | 88 |

- The kit's own entries repeat the medium block's (60–64), presumably a
  fallback for a note Dorico attributes to the kit rather than a block.
- Its expression map ("Velocity + Mod Wheel Dynamic") carries only dynamics.

Fiddle can already import this map and resolve its entries by instrument and
techniques (`PercussionMap::resolve`). What's missing is knowing, for each
note on the kit's channel, which block it is.

## What Fiddle does today

- **Bridge.** The unpitched percussion bridge (r2,
  [experiment](../experiments/unpitched-percussion-bridge/README.md)) has a
  percussion map of one pitch, 36, for every one of its 75 instruments, marked
  `forSingleInstrument` true. Every technique arrives through the expression
  map's dimensions; the pitch carries nothing. The decoder withholds a note on
  any other pitch.
- **Sections.** `BridgeSections.h` gives the unpitched map to single
  instruments and deliberately not to kits (`instrument.kit.*`), so a kit
  chair gets the universal map.
- **Layers.** `PercussionMapAnnotator` resolves every note with the chair's
  instrument. Notes (`midi_event.proto`) carry techniques but no instrument.
- **Chairs.** A chair is one Dorico instrument entity on one channel.

## Design

### 1. One pitch per instrument in the bridge

The unpitched bridge's percussion map gives each covered instrument a
**distinct pitch**, for Natural and for each roll kind (rolls still need an
entry to sustain, `results-01.md` finding 2). With 75 instruments and room to
grow, that fits in the MIDI range.

- The pitch table is fixed and append-only, kept explicitly in `profiles.py`
  rather than computed from list order, so adding an instrument never moves
  another's pitch.
- `forSingleInstrument` becomes false: the map now names the instrument by
  pitch. (Every covered instrument is listed, so a single-instrument chair
  still finds its own entries; phase 0 confirms Dorico behaves.)
- It's a new revision (r3, with a new beacon), because Dorico keeps a
  project's copy of a map whose ID and version it already has.
- The decoder's pitch check stays: a note on a pitch the map never sends is
  withheld.

### 2. The decoder names each note's instrument

The compiled bridge keeps the pitch → instrument table. For each note it sets
the note's **instrument** from its pitch, beside the techniques it already
decodes. That needs one new field on `fiddle::Note`
(`notation_instrument`, a Dorico entity ID). For a single-instrument chair it
is the chair's instrument.

### 3. Layers resolve by the note's instrument

`PercussionMapAnnotator` resolves with the note's instrument, falling back to
the chair's when a note has none (universal map, older bridges). Everything
else stays: a library map's entries for that instrument, Natural implied,
largest technique subset wins, single-instrument maps fall back to their own
entries. So on a wood-blocks kit chair:

- a layer with the VSL wood-blocks map plays each block from its octave;
- a layer with a single-block map plays every block on that block (its
  fallback), which is a reasonable sketch sound;
- a note for an instrument a multi-instrument map doesn't cover is withheld,
  and the Note Inspector says which instrument.

The Note Inspector shows the note's instrument, so you can see which block a
note was.

### 4. Kit chairs

- `BridgeSections.h` maps `instrument.kit.*` to Unpitched Percussion, so a kit
  chair gets the unpitched bridge and its layers show the percussion map
  picker.
- A kit chair is one chair, one channel, like any other. Fiddle doesn't need
  to know the kit's components: any unpitched instrument Dorico puts in the
  kit arrives at its own pitch. That covers custom kits too.
- The percussion map picker treats a map as covering a kit chair when it
  lists the kit's ID or any of its usual components (VSL's map lists both).
- Chair Manager adds kits from Dorico's instrument list, if kits are in it
  (phase 0, K5); otherwise from Dorico's kit definitions.

### 5. Sticking

VSL's map uses `pt.right` (right hand) for alternate samples. The unpitched
bridge has no sticking dimension, so `pt.right` would be lost. If phase 0
shows Dorico sends it, add a sticking add-on dimension (`pt.left`,
`pt.right`) in r3. This matters for single instruments too.

## Phase 0: what only Dorico can tell us

A probe score with the wood-blocks kit, a custom kit (for example snare and
two toms), and a single wood block, played through a probe map with the
per-instrument pitches:

| | Question | Why it matters |
| --- | --- | --- |
| K1 | Does the playback template address a kit by its kit entity on one channel, and can its components ever get channels of their own? | Whether a kit chair is enough, or chairs per component are needed too. |
| K2 | Which instrument ID does Dorico look up for a kit note: the component's, or ever the kit's? | Whether the bridge needs an entry for each kit ID (as VSL's has). |
| K3 | With `forSingleInstrument` false and every instrument listed, does a single-instrument chair still play its own entries? | Whether one map serves both, or single chairs keep r2. |
| K4 | Does the expression map see each kit note's techniques as for a single instrument, including on simultaneous notes on different components? | The decoder assumes one technique state per channel; two blocks struck together with different techniques could break that. |
| K5 | Are kits in the instrument list Fiddle reads for Chair Manager? | Where kit chairs come from. |
| K6 | Does Dorico send `pt.left`/`pt.right` from sticking, and to the expression map, the pitch, or both? | Whether r3 needs a sticking dimension. |

K4 is the risky one. The bridge's dimensions are per channel: a switch applies
to every note after it. If Dorico sends two components with different
techniques at the same instant, the expression map's switches can't describe
both, and Fiddle would need the technique in the pitch after all (per
instrument and technique, as VSL does). The probe must try it.

## Phases

0. **Probe** (above). Revise this design with the results.
1. **Bridge r3.** Per-instrument pitches, kit IDs in the unpitched section,
   the note instrument field, and sticking if K6 says so. Generator tests and
   C++ decoder tests (pitch → instrument, withheld pitches).
2. **Layers.** Resolve by the note's instrument; Note Inspector; picker
   coverage for kit chairs. C++ annotator tests with the VSL map.
3. **Chairs.** Kit chairs in Chair Manager and the playback template.
4. **Score tests and docs.** A kit score test (each block, rim, roll,
   simultaneous blocks), [library percussion maps](library-percussion-maps.md),
   [bridge maps](bridge-map-authoring.md) and the [backlog](backlog.md).

## Later, not in this design

- Drum-set kits use the same mechanism, but drum maps lean on hi-hat openness
  and choke; check the bridge's hi-hat dimension against a real drum map
  first.
- Generating roll strokes when a library entry has no roll.
