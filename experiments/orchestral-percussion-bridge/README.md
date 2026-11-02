# Orchestral pitched percussion bridge — r2 current (wire schema 2)

**Current (2026-10-20): [r2](pitched-percussion-r2/README.md)** is r1 with
Dorico's note-duration playback options pinned to 100% (written lengths;
Fiddle applies the percentages). r1, described below, stays frozen.

Created 2026-10-19 as the fourth section profile in the
[section-profile plan](../../docs/orchestral-bridge-maps.md).
[Importable map](pitched-percussion-r1/Fiddle-Orchestral-PitchedPercussion-r1.doricolib),
generated [allocation](pitched-percussion-r1/allocation.md) and full
[298-ID disposition](pitched-percussion-r1/inventory.md).

Covers timpani, glockenspiel, crotales, xylophone, xylorimba, marimba,
vibraphone and tubular bells, with one Dorico voice per routed channel.
Dorico files timpani under drums, but Fiddle's installer classifies them here.
Keyboards (celesta, piano) and unpitched percussion are out of scope; those
chairs keep the universal map.

Techniques shared with strings (legato, accent, tremolo, trill, arpeggio,
dynamics, soft, ghost note, glissando, damp, l.v., simile) keep the strings
conduit and values. Percussion-only dimensions:

| Dimension | Values | ord./nat. cancels |
| --- | --- | --- |
| beater | Felt, Rods, Puffy, Dreads, Sticks | No |
| strike_location | Centre, Edge (drums), Rim | Proposed yes |
| roll | Roll (pt.roll; snare roll variants excluded) | No |
| dead_stroke | Dead stroke | No |
| muffled | Muffled/Damped | No |
| motor | Motor on, Motor off | No |
| pedal | Pedal (percussion PT; CC64 passes through separately) | No |

**Review needed.** The built-in inventory marks beaters, playing spots and
damping as provisional groups. Dorico has no built-in hard/soft mallet PTs, so
only the listed beater IDs can be transmitted. Not yet host-verified.

Regenerate from the repository root with
`python3 experiments/orchestral-percussion-bridge/generate_percussion.py` (add `--check` to verify).
