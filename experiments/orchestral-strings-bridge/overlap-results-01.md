# Strings r2 — first overlap run, without pre-playback reload

**Superseded interpretation:** the [fresh run and scope review](overlap-results-02.md)
retire this fixture's acceptance expectations. The repeat requested below has
been completed; do not repeat it again or treat its mismatch count as proof of
a bridge-map defect. Original observations follow unchanged.

2026-09-24. The user reports entering PTs, playing and quitting Dorico, but
omitting the requested save/reopen before playback. This does not invalidate
the recorded events. **The physical overlap preconditions are satisfied, but
the control bars fail, so overlap-specific acceptance is inconclusive.** A
fresh-session repeat of the saved score is needed before changing the map.

## Evidence and selected pass

- Original: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790297931969609-2/capture.jsonl`.
- Local preserved copy: `local-captures/overlap-r2-pass1.jsonl` (git-ignored).
- SHA256: `33d894869ed3d86829e83e2e8e822d0a7bc885912b602bd4bf792551c5bb6445`.
- Closed schema-1 capture; 74,672 blocks, zero capture drops. Strict validation
  finds no missing/out-of-order records or invalid/dropped adapter input.
- Two playing passes are present. The first has nine musical notes; use only
  the second, full 17-note pass at blocks **71072–72396** inclusive.
- State reconstruction begins at the preceding STOP (block 15096), not by
  borrowing state from the first pass. All 49 add-on dimensions are freshly
  established in idle block 71060; base state is known at every played note.
- All 17 pitches match the scaffold on Port 1 / Channel 1. Thirteen idle audition
  NoteOns in the preparatory interval are excluded. Every musical note has a
  matching release; no duplicate active pitch or unmatched release occurs.

## Actual note lifetimes establish the intended geometry

Times below are host sample positions at 44.1 kHz.

| Bar | Low C onset → release | D onset | E onset | F onset | Geometry |
| --- | --- | --- | --- | --- | --- |
| 1 (CC control) | 0 → 31549 | 33075 | 66326 | 99004 | Low C released before D |
| 2 (CC overlap) | 132335 → 230240 | 165551 | 198450 | 231063 | C active at D/E, released before F |
| 3 (KS control) | 264864 → 296481 | 297719 | 330882 | 363648 | Low C released before D |
| 4 (KS overlap) | 396900 → 495150 | 430239 | 462741 | 496319 | C active at D/E, released before F |

## PT state observations

Only **5/17** full note-on PT sets match `overlap-r2.expected.json` (notes 1 and
5–8). Base codes are known: 4 on the two dotted-half C notes and 2 on all other
notes. Natural base codes are normalized away; the extra states below are
actual add-ons, not a length-code interpretation issue.

- **Bar 1:** the opening C has Flautando. Overpressure On (CC112=2) arrives at
  sample **32369**, but CC112=0 follows at **33075**, the exact onset of D. In the
  recorded parameter-before-events order, D/E/F therefore have no Overpressure.
  This is an observed set-then-clear, not simply a missing On. It happens in
  the non-overlapping control bar; its producer/cause is not identified yet.
- **Bar 2:** C has Flautando; D/E/F correctly have Overpressure. The old C's
  release at 230240 does not clear the current Overpressure before F at 231063.
  This isolated positive result does not rescue the failed paired control.
- **Bar 3:** Natural does not clear the preceding Overpressure, and the first
  Strum Up On is absent. C has only Overpressure. D/E/F have Strum down **plus
  unwanted Overpressure**.
- **Bar 4:** C has Strum up + unwanted Overpressure. D/E/F have Strum down +
  unwanted Overpressure. The old low C release at 495150 produces no observed
  logical clear of Strum down before F at 496319.
- **Bar 5:** Strum clears, but final Natural leaves Overpressure active. CC112
  clears at 527808 then is explicitly reasserted as 2 at 528494 before the final
  C5 at 529464. Thus it is not merely a stale value invented by the analyzer.

Natural/first-Strum input activation, voice-scoped selection, live edit-state
freshness and delayed Off scheduling remain possible factors. Do not claim the
skipped reload caused all of these observations, or that the map/host is proven
broken. In particular, the first control's delayed clear is concrete evidence
to inspect if a fresh run reproduces it.

## Timing cautions and next step

Nineteen playing preroll blocks have negative host positions. No backward host
clock was observed within the selected pass. Parameter/event-list equal-offset
ties occur at `(71090,37)`, `(71154,345)`, `(71348,277)`, `(71477,344)`,
`(71670,47)`, `(71863,456)`, `(71865,138)`, `(71928,251)`. Most importantly, the
control's CC112=0 and D NoteOn share an offset; the observed state uses the
documented adapter policy, not a host-proven cross-list total order.

Next: ensure the notation edits are saved, fully reopen Dorico and the same
test project, confirm the two voices still share Port 1 / Channel 1 and r2,
then play once from the start and fully quit. No score redesign, new map or
runtime changes requested. If the controls still fail, inspect active PTs and
export MIDI from this saved score before attributing any failure to overlap.
