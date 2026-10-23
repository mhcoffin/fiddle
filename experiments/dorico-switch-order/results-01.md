# Case A in the Dorico beta: still present

2026-10-23. Case A (the original reproduction project) played once into the
MIDI Probe in the Dorico beta, as reported by the user. **The On-before-Off
sequence is still there**, essentially unchanged.

## Evidence

- Source: `$TMPDIR/fiddle-midi-probe-1792784238589321-2/capture.jsonl`;
  preserved as `local-captures/case-A-beta-2026-10-23.jsonl` (git-ignored);
  reduced copy in [fixtures](fixtures/r2-shared-overlap-beta-2026-10-23.json).
- SHA256 `55271b2dcd1a8c1845e24da418817c4be55c3cfdcfc701794d02403e4d1e44a2`.
- Closed capture: 5,331 blocks, nothing dropped or invalid, 44.1 kHz. One
  playing pass (blocks 3085–3890), channel 1 only, all 12 notes and 12
  releases. The capture doesn't record Dorico's version; that this was the
  beta rests on the user's report.

`check_switch_order.py`: **STILL PRESENT**, 1 Off cut Flautando, 5 notes
started without it.

## The sequence, beside the 2026-09-25 capture

| ms from first note | Beta (2026-10-23) | Before (2026-09-25) |
| ---: | --- | --- |
| 2984.35 / 2983.99 | CC102 = 4, CC112 = 1 (block 3359) | same, sample 131594 |
| 3000.00 | CC112 = 0 (block 3361, sequence 0), then note on G5 (sequence 2) | same, sample 132300 |
| On → Off | 15.65 ms (690 samples) | 16.01 ms (706 samples) |
| 5968.73 / 5968.0 | CC112 = 0 for nat. (expected) | same |

The Off is two blocks after the On, so its order is not the adapter's
tie policy. All five bar-2 notes start with CC112 = 0 while the score still
has Flautando; bar 1 is unaffected. The base change and On now come 16
samples later than before (131610 vs 131594), the only visible difference.

## Not yet run

Case B (one voice) has not been run. It isn't needed to show the beta still
has the bug, but would show whether a one-voice part triggers it too.
