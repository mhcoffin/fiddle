# Fretted instruments bridge — r1 (wire schema 2)

Created 2026-11-11 as the sixth section profile, the guitar bridge the
[section-profile plan](../../docs/orchestral-bridge-maps.md) left for later.
**Current: [r1](fretted-r1/README.md)**,
[importable map](fretted-r1/Fiddle-Fretted-r1.doricolib), generated
[allocation](fretted-r1/allocation.md) and full
[298-ID disposition](fretted-r1/inventory.md).

**Not yet host-verified.** The map is generated, compiled by Fiddle's
bridge compiler in the C++ tests, and bundled as the Fretted template, but
nobody has played it in Dorico yet.

## Instruments

Every Dorico `instrument.fretted.*` entity (94, counting tunings and aliases)
and the guitarrón, which Dorico files under strings
(`instrument.strings.guitarrón`). That is:

- classical, steel-string, 12-string, jazz, semi-acoustic, electric (6, 7 and
  12 strings) and resonator guitars, in every tuning Dorico offers;
- electric, fretless, acoustic and semi-acoustic bass guitars (4–6 strings) and
  the guitarrón;
- Renaissance and Baroque lutes and the theorbo;
- banjo, tenor banjo, mandolin, ukulele and dulcimer;
- Hawaiian lap-steel and pedal-steel guitars;
- balalaikas, domras, bandurria, bandola, bordonúa, cavaquinho, charango,
  cuatros, requinto, tiple, tres, vihuelas and the Portuguese guitar.

`BridgeSections.h` covers them with two stems: `instrument.fretted` and
`instrument.strings.guitarrón`. A Python test keeps them in step with
`profiles.py`. Other plucked instruments stay on the universal map: harp,
oud, koto, kora, shamisen, zither and the tanpuras.

## Vocabulary

Techniques a fretted player shares with bowed strings keep the strings
r3–r5 conduit and values exactly: legato/non legato, accent/marcato,
vibrato, mute/open, tremolo (tremolo picking), trills, arpeggio (rolled
chords), dynamics, soft, ghost note, tremblement (lute ornament), muffled
strings, glissando, portamento (fretless bass), strum up/down, damp,
l.v. (let ring) and simile.

Two strings dimensions keep every strings value and append the guitar ones:

| Dimension | Strings values kept | Appended |
| --- | --- | --- |
| excitation (CC106) | Plucked 1, Pizzicato 2, Bartók/snap 3, buzz 4, nail 5, alla chitarra 6, secco 7 | Guitar pizzicato 8 |
| harmonic (CC113) | Artificial 1, Natural 1 2, Natural 2 3, Half 4 | Pinch 5, Harp 6 |

Fretted-only dimensions take the conduits the bowing dimensions leave free
(CC107–112) and then free keyswitch pitches:

| Dimension | Conduit | Values | Notes |
| --- | --- | --- | --- |
| bend | CC107 | Bend, Slight bend, Dip/bend | Alternatives for one note's bend |
| pre_bend | CC108 | Pre-bend | Independent: a pre-bent note can then bend or release |
| vibrato_bar | CC109 | Vibrato bar dip, Vibrato bar scoop | |
| vibrato_bar_line | CC110 | Vibrato bar line | Direction, independent of dips and scoops |
| palm_muting | CC111 | Palm muting | Independent of Mute; ended by its own line |
| tapping | CC112 | Left-hand tap, Right hand tap, Right-hand tap (tapping) | The two right-hand IDs stay distinct |
| slide | KS7 | Slide | Distinct from Glissando and Portamento |
| pick_scrape | KS8 | Pick scrape | |
| skank | KS15 | Skank | |
| drive | KS16 | Overdrive, Distortion | |
| modulation | KS17 | Chorus, Flange, Phaser | One at a time |
| delay | KS18 | Delay | |
| wah_pedal | KS19 | Wah-wah | Direction |
| wah_position | KS20 | Wah-wah open, Wah-wah stopped | Per-note o / + |

39 of 50 conduits (18 CCs, 21 keyswitch pitches), 11 spare; 25 base rows
(the universal set, as strings and pitched percussion use) and 87 add-ons;
91 encoded technique IDs, all present in Dorico 6's catalog.

Left out, with reasons in the inventory: bowing (col legno, bow strokes,
contact point, direction, pressure), left-hand pizzicato, the harp
techniques (pincé, sons xylophoniques), the jazz wind gestures (scoop, plop,
fall, doit, smear, flip, jazz turn, rip, squeeze, shake), and percussion,
keyboard and wind/vocal techniques.

## Open questions for review

- **Bends.** Dorico can play notated guitar bends as pitch bend. If it
  also activates the Bend techniques, a library map that bends on a keyswitch
  would bend twice. Check what arrives when a score with bends is played.
- **Hammer-ons and pull-offs** have no playing technique in Dorico; they
  arrive as slurs, so as Legato. Dead (x) notes have none either.
- **Effects and wah** are electric-guitar colours. Grouping them as drive /
  modulation / delay is a judgement call: in Dorico, chorus then flange
  replaces chorus, but drive plus chorus combine.
- **Dynamics.** The base rows are the universal set, as for strings: plain,
  tenuto and staccato-tenuto notes carry their dynamic on CC1, staccato and
  staccatissimo notes on velocity. Each layer's library map re-expresses the
  level, so a plucked library that wants velocity still gets it. The
  unpitched bridge puts every hit on velocity instead (`spec.BASES`); that
  is the alternative if CC1 proves awkward for plucked notes.

## Reproduce

From the repository root:

```sh
python3 experiments/fretted-bridge/generate_fretted.py          # --check to verify
python3 -m unittest discover -s experiments/fretted-bridge -p 'test_*.py'
```

The generator is the shared engine (`../orchestral-strings-bridge/generate.py`),
which gained an optional `PREFIX` so this section's names leave out
"Orchestral": `Fiddle-Fretted-r1.doricolib`, `xmap.user.Fiddle.Fretted.r1`
and `ptmg.user.Fiddle.Fretted.r1.*`.
