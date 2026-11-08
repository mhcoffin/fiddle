# Percussion kits

Status: draft design, revised 2026-11-08. Not implemented. Phase 0 (a probe
score) has to answer the open questions before anything is built.

## Summary

A Dorico percussion kit is several unpitched instruments played by one player.
"Wood blocks" (`instrument.kit.wood.blocks`) is five wood blocks on a
five-line staff. Dorico can route a kit two ways:

- **One channel for the kit.** Every component plays on the same channel. Each
  note's pitch stands for an (instrument, technique) pair, and the kit's
  percussion map defines the pairs. Keyswitches and controllers act on the
  whole channel, so they can't give one component a technique different from
  another's: the pitch has to carry it.
- **A channel per instrument.** Each component has a channel of its own and
  behaves like a single instrument. Pitches, keyswitches and controllers are
  all free to describe that instrument's techniques.

Fiddle can play neither yet: a chair is one instrument, and the unpitched
bridge sends every instrument's notes at the same pitch.

The plan supports both, chosen per kit chair in Chair Manager:

- **A channel per instrument** reuses what Fiddle has. Each component becomes
  a chair with the unpitched bridge (r2), techniques in the expression map,
  exactly like a single wood block today.
- **One channel** gets a bridge percussion map per kit that gives each
  (component, technique) pair its own pitch. Fiddle decodes the pitch back
  into the component and its techniques, and each layer's library percussion
  map picks the sample, as it already does.

## Evidence so far

- **Dorico's playback templates address a kit by listing it with its
  components.** Every endpoint config the user has saved that includes a kit
  (`EndpointConfigs/SY Percussion I (Synchron Player)`, `Elite`, `VEPro`,
  `Toms`, `VEP Server`, `Unified Server`) routes it to one channel. It lists
  the kit's ID and each component's instrument ID, all with the same
  `endpoints` value. For example, `instrument.kit.wood.blocks` and the five
  `instrument.percussion.other.wood.block.*` IDs all go to endpoint 14336. A
  template uses the same entity-to-endpoint mapping Fiddle already writes
  (`DoricoConfigGenerator`), so a channel per instrument should be a matter of
  giving the components different endpoints. None of the saved configs does
  that, so phase 0 has to confirm Dorico honours it (K1).
- **Dorico defines its kits' components.** `kitDefinitions.xml` in Dorico's
  resources lists each kit's components by instrument ID, for example
  `kit-definition.kit.wood.blocks` → high, medium-high, medium, medium-low and
  low blocks. `instruments.xml` has 27 kit IDs (drum set, toms, congas,
  temple blocks, marching kits and others). Fiddle can read both, so it knows
  a built-in kit's components when it writes the template.

## An example: VSL's wood blocks

`VSL SY Percussion - Woodblocks (PM).doricolib`, in Dorico's
`DefaultLibraryAdditions/Synchron Percussion I`, is a typical vendor map for a
kit played on one channel:

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
- Its expression map ("Velocity + Mod Wheel Dynamic") carries only dynamics,
  which is all an expression map can safely carry on a kit's channel.

Fiddle can already import this map and resolve its entries by instrument and
techniques (`PercussionMap::resolve`). On a kit's channel, what's missing is
knowing which block each note is.

## What Fiddle does today

- **Bridge.** The unpitched percussion bridge (r2,
  [experiment](../experiments/unpitched-percussion-bridge/README.md)) has a
  percussion map of one pitch, 36, for every one of its 75 instruments, marked
  `forSingleInstrument` true. Every technique arrives through the expression
  map's dimensions (strokes, roll kinds, mechanisms, modifiers); the pitch
  carries nothing. The decoder withholds a note on any other pitch.
- **Sections.** `BridgeSections.h` gives the unpitched map to single
  instruments, matching by prefix, and deliberately not to kits
  (`instrument.kit.*`), so a kit chair gets the universal map.
- **Layers.** `PercussionMapAnnotator` resolves every note with the chair's
  instrument. Notes (`midi_event.proto`) carry techniques but no instrument.
- **Chairs.** A chair is one Dorico instrument entity on one channel.
- **Template.** `DoricoConfigGenerator` maps each chair's instrument ID (and
  its aliases) to the chair's endpoint, with `index` counting players of the
  same instrument and player type.

## Design

### 1. Kit chairs and the routing choice

Chair Manager adds a kit (K5: from Dorico's instrument list, or from
`kitDefinitions.xml`) as one row, with a routing choice:

- **One channel.** One chair. The template maps the kit ID and every component
  ID to the chair's endpoint, as the saved configs do.
- **A channel per instrument.** One chair per component, grouped under the
  kit's row. The template maps each component ID to its own chair's endpoint;
  what the kit ID itself maps to is a phase 0 question (K1).

The default is a channel per instrument, because it keeps every technique and
gives each component its own mixer strip. One channel suits a kit you want
mixed as one sound, or a vendor map built for one channel.

### 2. A channel per instrument

Each component chair is a single unpitched instrument, so everything already
works: the r2 bridge, techniques in the expression map, layers resolving by
the chair's instrument, the percussion map picker. What's new:

- The chairs are grouped: Chair Manager shows them under the kit, and the
  mixer can show the kit as a header over its component strips, the way a
  chair spans its layers.
- `index`: a component of a kit and a standalone instrument of the same type
  (a wood block kit and a separate wood block) both use the component's
  instrument ID. How Dorico counts `index` across them decides how the
  template tells them apart (K2).

### 3. One channel per kit

#### The kit's bridge map

Each kit gets its own bridge percussion map, generated from its components in
`kitDefinitions.xml`: `forSingleInstrument` false, with an entry for each
(component, technique) pair at its own pitch. The kit ID also gets entries,
like VSL's, in case Dorico attributes a note to the kit (K3).

The kit's expression map carries dynamics and the Init (with the revision
beacon) and **no technique switches**: a switch would apply to every
component.

The pitches limit the vocabulary. A kit has roughly 110 usable pitches (the
beacon keyswitches sit at 126/127), so a five-block kit could give each block
about 20 techniques and a drum set with a dozen components about 9. The
vocabulary is therefore a short list per component, chosen from what library
maps actually distinguish:

- Natural.
- The roll kinds a library is likely to sample (roll, buzz roll), since a roll
  only sustains when its entry carries it (unpitched `results-01.md`,
  finding 2).
- The strokes that matter for that kind of instrument (rim for blocks, rim
  shot and cross stick for drums, bell for cymbals, openness for the hi-hat).
- Sticking, if Dorico sends it (K6).

Anything else falls back to the component's Natural entry, which is how
Dorico treats an unmatched technique (unpitched probe). Modifiers that only
change level, such as accents and dynamics, still reach Fiddle through
velocity.

Pitches are fixed per kit and append-only, kept explicitly in the generator,
so a new technique never moves another's pitch. Changing a kit's map is a new
revision, because Dorico keeps a project's copy of a map whose ID and version
it already has.

#### The decoder names each note's instrument

The compiled kit map keeps the pitch → (component, techniques) table. For each
note, the decoder sets its techniques and its **instrument** from the pitch.
That needs one new field on `fiddle::Note` (`notation_instrument`, a Dorico
entity ID). A pitch the map never sends is withheld, as now.

#### Layers resolve by the note's instrument

`PercussionMapAnnotator` resolves with the note's instrument, falling back to
the chair's when a note has none (every chair except a one-channel kit).
Everything else stays: a library map's entries for that instrument, Natural
implied, largest technique subset wins, single-instrument maps fall back to
their own entries. So on a one-channel wood-blocks chair:

- a layer with the VSL wood-blocks map plays each block from its octave;
- a layer with a single-block map plays every block on that block (its
  fallback), which is a reasonable sketch sound;
- a note for an instrument a multi-instrument map doesn't cover is withheld,
  and the Note Inspector says which instrument.

The Note Inspector shows the note's instrument. The percussion map picker
treats a map as covering a one-channel kit chair when it lists the kit's ID or
any of its components.

### 4. Sticking

VSL's map uses `pt.right` (right hand) for alternate samples. If Dorico sends
sticking (K6):

- on a channel per instrument it becomes an add-on dimension (`pt.left`,
  `pt.right`) in a new revision of the unpitched bridge, which helps single
  instruments too;
- on one channel it doubles a component's pitches, so it goes in only for
  kits whose components have room.

### 5. Custom kits

A custom kit's components are ordinary instrument IDs, so a channel per
instrument handles it like a built-in kit, once Fiddle knows the components.
One channel needs a generated map for that exact set of components. Where a
custom kit's ID and components come from, and whether a template can name it,
is K5.

## Phase 0: what only Dorico can tell us

A probe score with the wood-blocks kit, a custom kit (for example snare and
two toms), and a standalone wood block, played through probe templates for
both routings:

| | Question | Why it matters |
| --- | --- | --- |
| K1 | With the components mapped to different endpoints, does Dorico play each on its own channel? What should the kit ID map to (nothing, the first component's endpoint, a spare)? | Whether a channel per instrument can come from the template, or only from Play mode by hand. |
| K2 | How does `index` count a kit component against a standalone instrument of the same type, and against the same component in a second kit? | How the template tells a kit's wood block from a standalone one. |
| K3 | On one channel, which instrument ID does Dorico look up for each note: the component's, or ever the kit's? | Whether the kit's bridge map needs entries for the kit ID (as VSL's has). |
| K4 | On one channel, does Dorico still send an expression map's technique switches for kit notes, or only the percussion map's pitch? | Confirms the kit's expression map should carry dynamics only. |
| K5 | Are kits in the instrument list Fiddle reads for Chair Manager? How does a custom kit appear, and can a template name it? | Where kit chairs come from, and whether one-channel custom kits are possible. |
| K6 | Does Dorico send `pt.left`/`pt.right` from sticking, and to the expression map, the pitch, or both? | Whether the bridges need sticking. |

K1 decides the order of work. If the template can split a kit, a channel per
instrument is mostly chair and template work on top of the r2 bridge, and it
comes first.

## Phases

0. **Probe** (above). Revise this design with the results.
1. **A channel per instrument.** Kit rows and grouped component chairs in
   Chair Manager, the template entries (K1, K2), kit headers in the mixer.
   Generator tests for the template.
2. **One channel: bridge.** Per-kit bridge maps from `kitDefinitions.xml`, the
   kit IDs in `BridgeSections.h`, the note instrument field. Generator tests
   and C++ decoder tests (pitch → component and techniques, withheld
   pitches).
3. **One channel: layers.** Resolve by the note's instrument; Note Inspector;
   picker coverage for kit chairs. C++ annotator tests with the VSL map.
4. **Sticking** if K6 says so.
5. **Score tests and docs.** A kit score test for each routing (each block,
   rim, roll, simultaneous blocks with different techniques),
   [library percussion maps](library-percussion-maps.md),
   [bridge maps](bridge-map-authoring.md) and the [backlog](backlog.md).

## Later, not in this design

- Drum sets use the same mechanisms, but drum maps lean on hi-hat openness and
  choke, and a dozen components squeeze one-channel vocabulary hard; check
  against a real drum map first.
- Generating roll strokes when a library entry has no roll.
