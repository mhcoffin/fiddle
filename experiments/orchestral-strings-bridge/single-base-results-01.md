# Single-base d1 — shared-PT overlap passes

Scope update, 2026-10-15: this is historical shared-channel multi-voice evidence.
The [one-voice-per-channel decision](../../docs/orchestral-bridge-maps.md#supported-routing-scope--one-voice-per-channel)
excludes it from bridge acceptance and supersedes further shared-voice follow-ups.

2026-10-15. The user reports completing the prepared single-base diagnostic.
The newest closed MIDI Probe capture contains one playback pass of the expected
12-note fixture. Every musical note observes CC102=2, consistent with d1; MIDI
alone does not establish the assigned map's entity ID or hash.

## Evidence

- Source: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1792094884989590-2/capture.jsonl`.
- Preserved: `local-captures/shared-overlap-single-base-d1-pass1.jsonl` (git-ignored).
- SHA256: `63aabe8967eab28433e2d09fb23085dbd371a329bff1e116cb66697997cad56d`.
- Strict `inspect_live.read_capture` validation passes: 7,563 blocks, complete
  footer, zero capture drops, no invalid/dropped/missing input records.
- One playing pass starts at block 6146; sample rate 44.1 kHz, Port 1 / Channel 1.
- All 49 add-on dimensions initialized before playback; every note has complete,
  valid dimension state and base 2. All 12 note releases are present; no duplicate
  active pitches, unmatched releases, dangling notes, or backwards playback clock.
- Cross-list ties occur at block/offset 6164/94, 6422/299, and 6680/503.
  Parameters-before-events at equal offsets is adapter policy, not finer host-order proof.

## Result

All 12 pitch/onset-group PT expectations match: Flautando on the first ten
notes, no modifiers on the final two. Simultaneous groups are matched by pitch,
not serialized voice order.

| Host sample | CC | Value | Meaning |
| ---: | ---: | ---: | --- |
| 0 | 102 | 2 (twice) | Natural base |
| 0 | 112 | 1 (twice) | Flautando On |
| 263894 | 102 | 2 | Same base before bar 3 |
| 264600 | 112 | 0 | Flautando clears for Natural |

This is the entire CC102/112 playback stream. There is no transient clear or
reassertion at bar 2. The bar-2 low C begins at sample 132476 and ends at 230312,
covering D at 164889 and E at 198714; F starts after its release at 231569.
Flautando remains set throughout this overlap and afterward. Bar 3's upper C
starts at 264600, its low C at 264732; both observe the cleared state. Clearing
at the upper C timestamp uses the documented adapter tie policy.

## Interpretation and next isolation

The original r2 captures cleared Flautando at sample 132300, following a base
change and reassertion at 131594. That sequence disappears in d1 while legitimate
Natural cancellation still works. This supports a link to the original
base-selection configuration; it does not prove Dorico's internal scheduling
mechanism. d1 removes 24 base rows as well as the retained row's length condition.

A useful next controlled comparison would restore a minimal pair of conditional
Natural bases, keeping all add-on actions unchanged. This is a proposed isolation,
not a prepared or validated map. Document its selection rules and expected
transition before generating another diagnostic.
Do not remove Off actions globally or discard production length transmission
on this evidence. The four production profiles and matching decoder remain
unfinished; this is host-input evidence, not TCP/instrument validation.
