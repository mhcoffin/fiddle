# Shared-PT overlap — first captured run

Follow-up: the user confirmed Flautando remains active in Dorico's bar 2
display. The [repeat capture](shared-overlap-results-02.md) reproduces the
critical On/Off sample positions exactly. The original next-check request below
is historical; no additional unchanged playback is needed.

2026-09-25. **Complete capture, correct note geometry, but the requested shared
Flautando state disappears at the start of bar 2.** This is not an overlap
acceptance pass. Do not change the map or ask for another unchanged playback
before checking Dorico's active PT display in the saved score.

## Capture and scope

The first reported playback had no probe loaded in the saved project and no
new trace. After the user corrected routing, the saved engine data identifies
Fiddle MIDI Probe (`472C6B180B184E7693E656AA50E8CF21`). This result concerns only
the subsequent captured run.

- Source: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790392252654314-2/capture.jsonl`.
- Preserved: `local-captures/shared-overlap-r2-pass1.jsonl` (git-ignored).
- SHA256: `931f66302ebf0d21b111187dcf530486bde5c9f816af9645318d92248e3565cb`.
- Closed schema-1 capture accepted by `inspect_live.read_capture`: 12,186
  blocks, zero capture drops, no missing/invalid/dropped input records.
- One playing pass, blocks **10784–11589** inclusive; Port 1 / Channel 1.
- All 49 add-on dimensions initialized in idle block 10772; base known at
  every note. No missing or invalid dimension values. One idle audition excluded.
- Twelve expected musical notes, all with matching releases; no duplicate
  active pitch or dangling note. Eighteen negative preroll blocks, no backwards
  host clock. Sample rate 44.1 kHz.
- Cross-list ties at (block, offset): (10801,351), (11060,44), (11318,248).
  Parameter-before-event ordering is adapter policy, not a finer host guarantee.

## Note-start state and lifetimes

Times are host sample positions. Simultaneous written onsets can be slightly
humanized and can arrive in either pitch order; match by bar/onset neighborhood
and pitch, not serialized expectation index. No voice ID is transmitted.

| Bar/beat | MIDI pitch | On → Off | Reconstructed add-ons |
| --- | ---: | --- | --- |
| 1.1 | 60 | 0 → 31549 | Flautando |
| 1.1 | 79 | 0 → 31824 | Flautando |
| 1.2 | 74 | 33251 → 65282 | Flautando |
| 1.3 | 76 | 65968 → 97999 | Flautando |
| 1.4 | 77 | 99048 → 130941 | Flautando |
| 2.1 | 79 | 132300 → 164193 | none |
| 2.1 | 60 | 132476 → 230312 | none |
| 2.2 | 74 | 164889 → 197333 | none |
| 2.3 | 76 | 198714 → 230331 | none |
| 2.4 | 77 | 231569 → 263531 | none |
| 3.1 | 72 | 264600 → 296424 | none |
| 3.1 | 60 | 264732 → 296212 | none |

Bar 1's low C ends before D. Bar 2's low C overlaps D and E, then ends before F.
The intended physical overlap conditions are satisfied. Seven of twelve
desired sets match, but bar 3 was already clear: it cannot independently prove
that the final `nat` marking cleared an active Flautando state. CC102 is 2 in
bars 1/3 and 4 throughout bar 2; length-category correctness is not asserted by
this test and channel-wide base behavior is preserved as an observation.

## Complete CC112 sequence during playback

All entries are source 0 (parameter list).

| Sample | Value | Meaning | Block / offset / sequence |
| ---: | ---: | --- | --- |
| 0 | 1 | Flautando On | 10801 / 351 / 2 |
| 0 | 1 | Flautando On | 10801 / 351 / 3 |
| 131594 | 1 | Flautando On | 11058 / 362 / 1 |
| 132300 | 0 | Clear | 11060 / 44 / 0 |
| 263188 | 0 | Clear | 11315 / 372 / 0 |

The second-bar On precedes its clear by 706 samples (about 16 ms). The clear
coincides with the upper G onset and precedes the low C onset by 176 samples.
It precedes the long C release by 98,012 samples, so that release cannot be its
cause. There is no later Flautando reassertion in bar 2, and no separate clear
at the long C release. The first-bar note releases did not erase Flautando.

This is a concrete set-then-clear observation, not merely an absent On. It
does not yet distinguish PT duration/scope in the score from Dorico's switch
scheduling. Do not infer an Off-event fix or that Off events are unnecessary.

## Natural naming hazard discovered before this run

The installed `DefaultLibraryAdditions/art-conductor-vsl-playing-techniques.doricolib`
defines popover text `natural` as `pta.user.natural` → `pt.user.natural`. The
custom playback technique has no alias, fallback, or mutex membership and is
an attribute, not the built-in direction. Dorico's bundled `scoreLibrary.xml`
maps `nat` and `ord` to **`pt.natural`**, which is what the bridge's cancellation
groups contain. Use those entries, not the full word `natural`.

The user observed that `nat`/`ord` cancels Flautando where `natural` does not.
This gives a possible explanation for older cleanup discrepancies; those saved
scores have not been checked for their actual technique identities. Do not
retroactively claim the old captures establish a host cancellation defect.

## Next diagnostic — no new playback yet

In the saved shared-overlap score, inspect active playback techniques at
**bar 2 beat 1 for both voices and at beats 2/3 for the upper voice**. Is
Flautando still shown, and does its playback span actually extend through bar 2?
If yes, the clear conflicts with the displayed intent and merits a narrowly
targeted On/Off scheduling investigation. If not, resolve notation scope or
duration first. No map/plugin/runtime changes have been made.
