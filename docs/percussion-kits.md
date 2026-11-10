# Percussion kits

Status: draft design, revised 2026-11-10 after template experiments. Not
implemented.

## Summary

A Dorico percussion kit is several unpitched instruments played by one player.
"Wood blocks" (`instrument.kit.wood.blocks`) is five wood blocks on a
five-line staff. Dorico can route a kit two ways:

- **One channel for the kit.** Every instrument plays on the same channel, and
  each note's pitch stands for an (instrument, technique) pair defined by the
  kit's percussion map. Keyswitches and controllers act on the whole channel,
  so they can't give one instrument a technique different from another's.
- **A channel per instrument.** Each instrument has a channel, expression map
  and percussion map of its own, and behaves exactly like a single
  instrument.

Fiddle uses **a channel per instrument**. A kit in Fiddle is a group of
ordinary unpitched chairs, one per instrument, each on the unpitched bridge
(r2) that single instruments already use. Every technique gets through,
simultaneous notes on different instruments never interfere, and Fiddle needs
no new bridge, decoder or note field. The cost is channels: wood blocks take
5, a full drum set 19.

The playback template needs nothing kit-specific: a split kit is addressed by
its instruments' own IDs, exactly as Fiddle already addresses chairs.

## Evidence

All in Dorico 6, 2026-11-08 to 2026-11-10.

- **One-channel kits list the kit with its instruments.** Every endpoint
  config the user had saved that includes a kit (`SY Percussion I (Synchron
  Player)`, `Elite`, `VEPro`, `Toms`, `VEP Server`, `Unified Server`) routes
  it to one channel: the kit ID (`instrument.kit.wood.blocks`) and each
  instrument's ID (the five `instrument.percussion.other.wood.block.*`) all
  have the same `endpoints` value.
- **A split kit is just its instruments.** The user split a wood-blocks kit
  across channels in Play mode and exported the endpoint config
  (`EndpointConfigs/WoodBlockSeparateTest`). Its `instruments` section has
  five entries, one per block ID, each on its own channel, all `index` 0,
  `irvIndex` 0, `kSoloPlayer`. There is **no entry for the kit ID**.
- **The split survives a template.** A playback template ("BlockTest") whose
  only endpoint is that config, applied to a new project with a wood-blocks
  kit, routed the blocks exactly as exported.
- **A kit's instruments count like separate instruments.** Adding a separate
  high wood block to that project and reapplying the template routed it to a
  new plugin instance: the template's one high-block entry (`index` 0) went to
  the kit's high block, and the standalone block, as the second high block in
  the project, matched nothing, just as a second standalone high block would. So `index`
  counts every instrument of a type in the project, inside a kit or not.
- **`index` follows player order.** With the standalone block's player moved
  above the kit's, the standalone block and the kit's other four blocks
  played on the template's channels, and the kit's high block went to the new
  instance. Whichever instrument comes first in player order is `index` 0.
- **Dorico defines its kits' instruments.** `kitDefinitions.xml` in Dorico's
  resources lists each kit's instruments by ID, for example
  `kit-definition.kit.wood.blocks` → high, medium-high, medium, medium-low and
  low blocks. There are 27 kit definitions.

**Working assumption.** Unless the kit itself is given a port and channel,
Dorico treats an instrument in a kit exactly like the same instrument outside
one: the same template matching, the same `index` counting, the same
expression and percussion maps, the same techniques. Fiddle never maps the kit
ID, so a kit's instruments behave as single instruments, which Fiddle already
plays. The kit score test (phase 4) checks it.

## Design

### 1. Kit definitions

A kit in Fiddle is a name and a list of Dorico instrument IDs. Chair Manager
adds one:

- **From a preset.** Each of Dorico's kit definitions, read from
  `kitDefinitions.xml`: wood blocks, temple blocks, tom-toms, roto toms,
  bongos, congas, drum set and so on.
- **Custom.** Any list of unpitched instruments, to match a kit the user built
  in Dorico. A custom kit's instruments are ordinary instrument IDs, so
  nothing else changes.

The kit's instruments must cover every instrument of the Dorico kit it plays:
an instrument the template doesn't map is routed, like any unmatched
instrument, to a new plugin instance.

### 2. A kit compiles to chairs

Each instrument becomes a solo chair (a kit is one player), with the kit as a
group:

- **Template.** One entry per chair, as now: the instrument's ID (and its
  aliases), `index`, `kSoloPlayer`, the chair's channel. The kit ID gets no
  entry.
- **`index`.** Fiddle already numbers chairs of the same instrument and player
  type (`DoricoConfigGenerator::playerIndices`). Kit chairs join that count, so
  a wood-blocks kit plus a standalone high block is two high-block chairs,
  `index` 0 and 1 in player order, as for Flute 1 and Flute 2. Chair order in
  Fiddle has to match the players' order in Dorico, as it already does for
  duplicate instruments.
- **Bridge.** Each chair gets the unpitched bridge by its instrument, as
  today. No kit-specific map.

Users can do all of this by hand today, by adding a chair per instrument. Kits
add the convenience and the grouping.

### 3. Grouping

- **Chair Manager** shows a kit as one row with its instrument chairs under
  it; adding, removing or reordering the kit moves them together.
- **Mixer.** The kit is a header over its instrument strips, the way a chair's
  header spans its layers. Each instrument keeps its own strip, which is
  useful for balancing toms or blocks.

### 4. Kit layers

Setting up the same library on every instrument chair one at a time would be
tedious. A kit layer applies to every chair in the kit: the same plugin
routing, expression map and library percussion map, resolved on each chair by
that chair's instrument. With VSL's wood-blocks map, each block chair plays its
own octave (see the example below). A chair can still override a kit layer or
add its own.

Whether a kit layer is one shared plugin voice or a copy per chair is a
question for implementation; the mixer and audio routing decide it.

### 5. Coverage

The unpitched bridge covers 46 of the 88 instruments used in Dorico's kits.
These kits are fully covered: drum set and basic drum set, tom-toms, floor
toms, roto toms, tenor drums, wood blocks, temple blocks, bongos, bongo bells,
congas and cowbells. Not covered:

- agogos, cajón, cuíca, maracas, timbales;
- jam blocks, kempyang/ketuk, tabla;
- the marching kits (bass drums, cymbals, snares, tenors).

Adding them means adding their instrument IDs to the unpitched bridge's
instrument list (a new revision, because Dorico keeps a project's copy of a
map whose ID and version it already has) and to `BridgeSections.h`. Until then
those chairs get the universal map.

### 6. Sticking

VSL's map uses `pt.right` (right hand) for alternate samples. The unpitched
bridge has no sticking dimension, so `pt.right` is lost. If Dorico sends it,
add a sticking add-on dimension (`pt.left`, `pt.right`) in the next unpitched
revision. That helps single instruments as much as kits.

## Open questions

| | Question | How to find out |
| --- | --- | --- |
| Q3 | Does Dorico send `pt.left`/`pt.right` from sticking? | A sticking passage through the unpitched probe. |
| Q4 | Can Fiddle read custom kits from a project, or must the user list the instruments? | Look at a project with a custom kit. |

## An example: VSL's wood blocks

`VSL SY Percussion - Woodblocks (PM).doricolib`, in Dorico's
`DefaultLibraryAdditions/Synchron Percussion I`, is a typical vendor map, made
for a kit on one channel:

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

- The kit's own entries repeat the medium block's (60–64).
- Its expression map ("Velocity + Mod Wheel Dynamic") carries only dynamics.

As a layer's library percussion map it works unchanged on a split kit:
`PercussionMap::resolve` takes the chair's instrument, so the medium-high
block's chair plays 72–76.

## Another example: Art Conductor

Art Conductor (Babylonwaves) ships **no percussion maps**: none of the 29,323
`.doricolib` files in its Dorico set contains a `DrumKitNoteMapDefinition`
(checked 2026-11-09). Its percussion maps are expression maps, one per
library patch, for one channel. In `VSL/Synchron 11 Percussion/VelXF Mod`:

- **Every combination passes the pitch through.** Each one covers pitches
  0–127 with no transposition, so the key that plays (which tom, which block)
  is whatever pitch Dorico sends. On an unpitched instrument that pitch comes
  from a percussion map Art Conductor doesn't supply.
- **Everything else is a channel-wide keyswitch**, selected by Art Conductor's
  own playing techniques, which its "PBT" companion libraries define:
  - *Concert Toms Yamaha*: beater (`pt.user.stick`, `rods`, `brushes`,
    `mallets`; KS 24–27) × group (`pt.user.high` KS 23, `pt.user.low`
    KS 21), plus `pt.user.fx`. No Natural.
  - *Taikos*: drum size (`pt.user.08`, `12`, `13`, `15`, `16`, `18`, `20`,
    `26`; KS 38–45) × beater (`pt.user.cudgel`, `tam`, `effects`;
    KS 24–26).
  - *Woodblocks*: beater only (`pt.natural` KS 24, `pt.user.stick` KS 26,
    `pt.user.mallet+pt.user.plastic` KS 25). Which block plays depends
    entirely on the pitch.

What it means for Fiddle:

- **An Art Conductor map can't choose the drum.** On a layer with no
  percussion map, Fiddle plays every note at the bridge's pitch 36
  ([library percussion maps](library-percussion-maps.md)). The
  instrument-to-key mapping has to come from the layer's percussion map, for
  example VSL's. The annotator chain runs the percussion map first and hands
  the expression map the library pitch, so the two combine on one layer.
- **Its own techniques never arrive.** `pt.user.*` aren't Dorico built-ins, so
  the bridge never sends them, and combinations built only from them (all of
  the toms map's) never match. Using them would need a per-layer rule that
  adds techniques by instrument, for example high tom → `pt.user.high`.

## What Fiddle does today

- **Bridge.** The unpitched percussion bridge (r2,
  [experiment](../experiments/unpitched-percussion-bridge/README.md)) gives
  75 single instruments one pitch, 36, with every technique in the expression
  map's dimensions.
- **Sections.** `BridgeSections.h` gives the unpitched map to those
  instruments, matching by prefix, and not to kit IDs.
- **Layers.** `PercussionMapAnnotator` resolves every note with the chair's
  instrument.
- **Chairs and template.** A chair is one Dorico instrument on one channel;
  `DoricoConfigGenerator` maps its ID to that channel with an `index` per
  instrument and player type.

## Phases

1. **Kit definitions and grouping.** Presets from `kitDefinitions.xml`, custom
   kits, kit rows in Chair Manager, kit headers in the mixer. Template and
   `index` tests in `DoricoConfigGeneratorTest`.
2. **Kit layers.**
3. **Coverage and sticking.** The missing kit instruments and, if Q3 says so,
   sticking, in the next unpitched bridge revision.
4. **Score tests and docs.** A kit score test (each block, rim, roll,
   simultaneous blocks with different techniques),
   [library percussion maps](library-percussion-maps.md) and the
   [backlog](backlog.md).

## Not chosen: one channel per kit

Fiddle could generate, per kit, a bridge percussion map giving each
(instrument, technique) pair its own pitch, with an expression map carrying
only dynamics, and decode the pitch back into instrument and technique. The
pitch is a private code between the template and Fiddle, so nothing would be
lost as long as the vocabulary fits in about 110 pitches (roughly 20
techniques per block for wood blocks, 6 per instrument for a full drum set).
It saves channels, but needs a per-kit map, a decode table and an instrument
field on every note, and roll dynamics (CC1) would still be shared by the
channel. Worth building only if channel use becomes a real problem.

## Later, not in this design

- Generating roll strokes when a library entry has no roll.
- Per-layer technique rules for maps built on their own techniques, such as
  Art Conductor's `pt.user.high` or `pt.user.12`.
