# Two-base d2 — conditional pair reproduces the unwanted clear

Scope update, 2026-10-15: this is historical shared-channel multi-voice evidence.
The [one-voice-per-channel decision](../../docs/orchestral-bridge-maps.md#supported-routing-scope--one-voice-per-channel)
excludes it from bridge acceptance and supersedes the proposed timing follow-up.

2026-10-15. After the user completed the d2 test and fully quit Dorico, strict
`inspect_live.read_capture` validation accepted the closed capture. The observed
2→4 transition and repeated unwanted clear match the original r2 symptom.
MIDI state is consistent with d2 but does not independently identify the assigned
map entity/hash; assignment follows the user's completion report.

## Evidence

- Source: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1792101257874785-2/capture.jsonl`.
- Preserved: `local-captures/shared-overlap-two-base-d2-pass1.jsonl` (git-ignored).
- SHA256: `7134c263fa5b6ab6aba2442d69fedd866047f44765c65cbf111ed84377f6dbef`.
- 9,130 blocks, complete footer, zero capture drops and no invalid, dropped,
  missing or duplicate input records. 44.1 kHz, Port 1 / Channel 1.
- One playback pass, blocks 4192–4995 inclusive. All 49 add-on dimensions
  initialized before playback. All 12 notes and releases present, complete valid
  dimension state, no duplicate active pitches, unmatched releases, dangling
  notes or backwards playback clock. One idle audition excluded.
- Cross-list ties at block/offset 4209/235, 4467/440, 4726/148 use adapter
  parameters-before-events policy. The critical On and Off are in separate blocks
  at distinct sample times, so their order does not depend on that tie policy.

## Complete CC102/112 playback stream

| Host sample | CC | Value | Meaning |
| ---: | ---: | ---: | --- |
| 0 | 102 | 2 (twice) | Medium Natural base |
| 0 | 112 | 1 (twice) | Flautando On |
| 131594 | 102 | 4 | >Long Natural base |
| 131594 | 112 | 1 | Flautando reasserted |
| 132300 | 112 | 0 | Unwanted clear at bar 2 |
| 263188 | 112 | 0 | Further clear before bar 3 |
| 263894 | 102 | 2 | Return to Medium Natural base |

The critical reassertion→clear interval is 706 samples (16.01 ms), identical to
both original r2 shared-overlap captures. No later On repairs it.

Bar-2 upper G starts at 132300, low C at 132476. Low C ends at 230312, overlapping
D at 164889 and E at 198714; F begins after its release at 231569. Thus the intended
physical overlap is present. All five bar-1 notes observe Flautando/base 2; all
five bar-2 notes observe no modifiers/base 4. Bar 3's C pitches (72 at 264616,
60 at 264732) observe no modifiers/base 2. Seven of twelve requested PT sets
match; all five failures are missing Flautando in bar 2. Because Flautando is
already clear, this run cannot separately demonstrate legitimate cancellation
of an active state at bar 3. The d1 run did demonstrate that cancellation.

## Conclusion

The original two conditional Natural rows suffice to reproduce the symptom in
this fixture; the other 23 base rows are unnecessary. With the same add-on data,
one unconditional Natural base passed and the conditional pair reproduced the
On-before-Off anomaly. This narrows the trigger to the retained base-selection
configuration and transition, without proving which internal Dorico operation
emits the clear or establishing a universal rule for add-ons.

Keep Off actions and original length transmission intact in production. A next
bounded diagnostic could vary base timing while retaining this pair and all
add-on semantics, to determine whether the relative scheduling changes. Review
the original timing fields and define the expected comparison before generating
it. This is a proposal, not a prepared map or a decoder repair.
