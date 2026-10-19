# Strings wire experiment: r2 export results

> **Historical result, retained as evidence.** The proposed 720-base experiment
> below is superseded as the active next step. The failures concern this packed
> encoding, not add-ons generally. Follow
> [Orchestral bridge maps](../../../docs/orchestral-bridge-maps.md) for current work.

Analyzed `separate-r2.mid` and `packed-r2.mid` on 2026-09-21. Both files have
16 notes, 480 PPQN, and the note/controller stream in track 1, port 0, channel 1.
The unchanged [expected PT sets](../expected.json) include the agreed note-6
correction. Source-file hashes, map/decoder fingerprints, decoded notes, and
full channel-event streams are recorded in [round2.json](round2.json).
Original exports were not modified.

## Result

| Check | A r2: separate | B r2: packed |
| --- | --- | --- |
| All required controller state observed | Yes | Yes |
| Invalid/unknown controller values | None | None |
| Exact PT sets at all 16 note-ons | **16/16 match** | **13/16 match** |
| Notes 4–6: compound base articulations | All correct | All correct |
| Notes 11–13: combined modifiers | All correct | Missing modifiers |
| First-note length setting precedes note-on | No | No |
| Full/live acceptance | Still pending | Fails this export test |

The comma-to-plus generator correction repaired the observed compound-base
losses: CC102 now sends 47 for Staccato + Tenuto, 52 with added Accent, and 72
for Staccatissimo + Marcato (all with Medium length). These were 32, 7, and 12
in round 1. This validates those base combinations in these exports, not every
one of the 90 generated base states.

## Remaining packed-add-on loss

Non vibrato is retained throughout this section in both captures.

| Note | Expected string modifiers | Expected B CC116 | Observed B CC116 | Decoded B modifiers |
| --- | --- | --- | --- | --- |
| 10 | Flautando | 1 | 1 | Flautando |
| 11 | Flautando + Overpressure | 3 | 2 | Overpressure |
| 12 | Flautando + Overpressure + Fouetté | 7 | 2 | Overpressure |
| 13 | Flautando + Overpressure | 3 | 2 | Overpressure |
| 14 | Flautando | 1 | 1 | Flautando |
| 15 | None | 0 | 0 | None |

The captured bytes establish the lost information. They do **not** establish
whether the exact internal cause is switch import, selection precedence,
interactions with the independent Non vibrato add-on, or conflicting individual
CC writes. In particular, this is not proof that every possible compound-add-on
map fails. The current generated B scheme is not safe to adopt as an optimization.

A retains all three modifiers and returns to the correct pair after Fouetté,
then to Flautando alone, then none. That confirms note-boundary coexistence and
restoration for this sequence, not general overlapping-voice correctness.

## Timing issue remains separate

At tick zero, both exports contain CC102=0 initialization, then the first NoteOn,
then CC102=2. Strict file-order decoding reports Very-short for the first note
and Medium for the other fifteen. The inspector warns rather than reordering
events or marking full acceptance as passed. Its nonzero exit code for A r2
therefore means **ordering warning**, not a PT-set mismatch.

No live VST3 event trace was supplied. Do not infer either that live playback
has this exact ordering or that the current Fiddle note-enrichment window solves
it. Explicit initialization, same-timestamp association, starts/seeks, and
overlapping notes remain production acceptance gates.

## Recommended next experiment

Keep A as the known-good PT-set baseline. To test a more deterministic route to
packing, move the small modifier product into **base switches**: each selected
base writes the complete CC102 articulation/length value and complete CC116
modifier mask. Leave independent properties such as vibrato on their own CCs.

For this probe that expands 90 bases to 90 × 8 = 720 bases without requiring
more controllers. It would test the already-working complete-base-selection
path rather than assuming competing add-ons compose correctly. It still needs
a host test and must not become an unbounded cross product of every dimension.
This third variant is a recommendation, **not generated or validated yet**.

The conservative 18-CC draft allocation remains the working budget. The proposed
16-CC budget remains conditional; the two controllers have not yet been saved
by a validated map. No production map, decoder, or project assignment changed.
