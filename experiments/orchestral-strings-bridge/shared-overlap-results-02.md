# Shared-PT overlap — repeat reproduces On-before-Off exactly

Follow-up: the user authorized preparing the [single-base d1 diagnostic](single-base-diagnostic.md).
It is now ready for manual import/assignment; no host capture of d1 exists yet.
The proposal at the end of this report records the state at capture analysis.

2026-09-25. The user confirmed that bar 2 displays Flautando as an active
technique, then requested a repeat with the score/map/routing unchanged.
**The repeat reproduces the critical CC sequence at identical host sample
positions.** This is not an overlap acceptance pass; do not request more
unchanged repeats or silently remove Off actions.

## Evidence

- Source: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790392531459006-2/capture.jsonl`.
- Preserved: `local-captures/shared-overlap-r2-pass2.jsonl` (git-ignored).
- SHA256 of source and copy: `992fb07992ef0efae8b1d844fe95d970ddc3cfc9623008766f03312cf62a1f2a`.
- Strict `inspect_live.read_capture` validation accepts a closed schema-1 trace:
  63,310 blocks, zero capture drops, no missing/invalid/dropped input records.
- One playing pass, blocks **60869–61674** inclusive, 44.1 kHz, Port 1 / Channel 1.
- All 49 add-on dimensions initialized in idle block 60859; base known at every
  note. No missing/invalid values. Six idle auditions excluded.
- All twelve musical notes and their releases present; no duplicate active
  pitch or dangling note. Nineteen negative preroll blocks, no backwards clock.
- Cross-list ties: (60887,110), (61145,315), (61404,7). Controller-before-note
  at equal timestamps remains adapter policy, not a finer host-order guarantee.

## Comparison with the first capture

All CC112 playback events have identical sample positions and values:

| Host sample | CC112 | Meaning | Repeat block / offset / sequence |
| ---: | ---: | --- | --- |
| 0 | 1 | Flautando On | 60887 / 110 / 2 |
| 0 | 1 | Flautando On | 60887 / 110 / 3 |
| 131594 | 1 | Flautando On | 61144 / 121 / 1 |
| 132300 | 0 | Clear | 61145 / 315 / 0 |
| 263188 | 0 | Clear | 61401 / 131 / 0 |

All are source 0 (parameter list). CC102 changes to base 4 at sample 131594,
immediately before the Flautando On in the same block/offset. The subsequent
Off is **706 samples (16.01 ms) later**. It shares sample 132300 with bar 2's
upper G onset; the low C begins at 132476, another 176 samples (3.99 ms) later.
The two CC commands occupy distinct times/blocks in both runs, so their order
is not an equal-offset adapter tie artifact.

The bar 2 low C again ends at 230312, after D onset 164889 and E onset 198714,
before F onset 231569. No clear occurs at that long note's release. The earlier
clear cannot be attributed to this release, and is not repaired by another On.

All twelve note-start PT sets agree with the first run: five Flautando notes
in bar 1, five empty sets in bar 2, two empty sets in bar 3. Seven of twelve
requested sets match. Bar 3 is already clear, so it still does not separately
validate cancellation of an active Flautando state. Musical sample times also
match except bar 1 E: On/Off 65929/97960 versus 65968/97999 previously (39 samples
earlier). Thus the whole trace is not byte-identical, but the critical sequence is.

## Interpretation and proposed next isolation

The user's active-technique check establishes a reported mismatch between
Dorico's displayed Flautando and the received state, rather than simply missing
notation. Repeatability strengthens the scheduling hypothesis but does not
prove which internal switch generated the clear or establish a general Dorico bug.

One plausible trigger is the note-length base change (2 → 4) restarting a
combined base/add-on switch while the old combination's Off occurs after the
new On. A proposed next diagnostic is a separate map with one unconditional
Natural base and the same Flautando On **and Off** actions, using this same
score. That isolates note-length base switching; it is not implemented or
installed here. Removing Off globally could hide this symptom while breaking
real expiry/cancellation and is not a justified fix.
