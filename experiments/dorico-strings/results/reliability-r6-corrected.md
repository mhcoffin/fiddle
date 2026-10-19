# Corrected r6 reliability export

> **Historical result, retained unchanged below.** The automatic-restoration
> failure remains relevant; later [live results](../live-results.md) document
> explicit-return tests. Follow [Orchestral bridge maps](../../../docs/orchestral-bridge-maps.md)
> for the current work plan.

Input: `../reliability-r6-corrected.mid` (708 bytes), SHA-256
`04581d6710a9eb3ed3d33aa47675d04cb024c9c3a683473047ad1d2ae256cda2`.
480 PPQN, two tracks, channel events in track 1 (zero-based).
Inspected with `inspect_midi.py` and the unchanged, digest-verified
`generated/high-vibrato.decoder.json`.

## Main finding

The exported MIDI reproduces the live inspector's bar-5 failure. This is not
evidence of Fiddle losing a transmitted restoration command: no such command
appears in the exported sequence before the affected note.

Note 127 is the vibrato scalar conduit. Velocity 1 means unspecified, 2 means
non-vibrato, 64 means con-vibrato, 126 means vibrato, and 127 means wide vibrato.
These are positive-velocity Note On commands, not physical Note Off messages.

| Boundary | Tick | Exported command, followed in file order |
| --- | ---: | --- |
| Bar 5 beat 1 | 7670 | Note 127, velocity 126 (vibrato) |
| Bar 5 beat 2 | 8149, 8150 | Note 127/1 (reset), then 127/2 (non-vibrato) |
| Bar 5 beat 4 | 9109 | Note 127/1 (reset) |
| Bar 5 beat 4 | 9110, 9120 | CC102=2, then musical C5; **no 127/126 restoration** |
| Bar 6 beat 1 | 9590 | Note 127/126, corresponding to explicit vibrato marking |
| Bar 7 beat 2 | 11989, 11990 | 127/1, then flautando ON (0/1) and vibrato (127/126) |
| Bar 7 beat 4 | 12949, 12950 | Flautando OFF (0/2) and 127/1, then **127/126** |
| Bar 7 beat 4 | 12960 | Musical C5 with vibrato alone |

Thus this score/map combination does not implement a temporary mutex override
that automatically restores its previous member. The independent flautando
combination does return to vibrato. This is an observation of this configuration,
not a claim about every Dorico expression map or notation setup.

Do not add a previous-value stack to Fiddle's scalar decoder: the same reset
command is also used during ordinary replacements and does not encode a request
to restore a prior value. Recommended next step is an explicit vibrato marking
on bar 5 beat 4, followed by repeat/start-in-middle/stop-and-seek tests. Keep this
original export and failed restoration expectation as evidence; an explicit
marking tests a different, deterministic return rather than fixing this result.

## Coverage and limitations

- All 38 musical notes and expected pitches are present across 28 onsets.
  For comparison only, humanized note times were assigned to their nearest
  quarter-note onset (maximum deviation 7 ticks); chord ordering was ignored.
- Decoded PT/pitch multisets match 37 of 38 expected notes. The sole PT mismatch
  is bar 5 beat 4: empty PT set instead of `pt.vibrato`.
- All four notes of each non-vibrato, con-vibrato, and wide-vibrato bar carry
  the expected distinct scalar. Both temporary flautando notes and its ending
  match the expectation.
- **This export is not a complete initialization pass.** Initial CC102/103/107/
  108 messages are on channel 2, while musical notes, scalar/binary keyswitches,
  and subsequent CC102 messages are on channel 1. The first musical note also
  precedes CC102=2 at tick zero in file order. The offline decoder therefore
  correctly reports missing CC state and unknown length for the first note;
  the remaining 37 notes decode Medium. No channel merging or reordering was
  used to hide those warnings. The preceding live inspector used channel 2
  and showed initialized dimensions, so this file alone does not establish
  an equivalent live initialization fault.
- Exported events corroborate the live boundary observations; they are not a
  substitute for live repeated playback and seeking checks.

No score, map, decoder, runtime, or mixer changes were made during this analysis.
