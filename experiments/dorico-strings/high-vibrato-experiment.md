# F r6: multi-valued vibrato on high keyswitch pitch 127

> **Archived procedure.** [Live capture and reliability results](live-results.md)
> now supersede the prospective test status below. Keep the bounded results and
> this encoding for reproducibility, not as the production allocation. Follow
> [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md) for current work.

Status: **16/16 PT sets match the baseline Dorico export**, with the unchanged
first-note length-ordering warning. The Non vibrato → Vibrato transition succeeds
on pitch 127 alongside low-pitch modifiers. No identical re-export is needed.
Next: [capture-only live input verification](live-probe.md). No experimental
decoder or map has been installed in normal Fiddle.

## First export: unchanged score

1. Import [high-vibrato.doricolib](generated/high-vibrato.doricolib) into the existing
   disposable 16-note test project.
2. Explicitly assign **Fiddle EXP Strings F - high vibrato r6** to the test endpoint.
3. Export `high-vibrato-r6.mid` into `/Users/mhc/fiddle/experiments/dorico-strings`.

No notation edits are required. Keep using the normal test instrument, **not the
Fiddle plug-in or a working score**. Playback is unnecessary; if auditioning,
mute its audio output because low and high protocol notes can be audible or
trigger a VSTi's own switches. Do not install in DefaultLibraryAdditions.

```sh
python3 experiments/dorico-strings/inspect_midi.py experiments/dorico-strings/high-vibrato-r6.mid --variant high-vibrato --expect experiments/dorico-strings/expected.json
```

## Encoding and hypothesis

E r5's three working binary dimensions are unchanged on pitches 0, 1, 2. The
vibrato group moves from CC106 to **MIDI pitch 127**, the upper MIDI boundary:

| Velocity | Complete vibrato value |
| --- | --- |
| 1 | Unspecified |
| 2 | `pt.nonVibrato` |
| 64 | `pt.conVibrato` |
| 126 | `pt.vibrato` |
| 127 | `pt.wideVibrato` |

Every command replaces the complete dimension rather than setting an independent
bit. The existing four-member vibrato mutex group is unchanged. Each member's
switch-off sends unspecified (velocity 1); initialization also sends unspecified.
Four CCs remain: 102, 103, 107, 108. Base switches, binary modifiers, and
`allowMultipleNotesAtSamePitch=true` are unchanged from E.

This tests simultaneous independence of low-pitch dimensions and one multi-valued
high-pitch dimension. The existing score exercises unspecified on notes 1–6,
Non vibrato on notes 7–15, and Vibrato on note 16. It also tests retaining Non
vibrato while three independent low-pitch modifiers enter and leave.

The decoder records each scalar dimension separately per port/channel and
replaces its previous member on each exact command. It requires explicit initial
state, reports unknown velocities, preserves same-tick file order, and never
infers restoration of an earlier member. Physical note-off and velocity-zero
note-on events do not change dimension state. An explicit switch-off to
unspecified followed by a missing new-member command therefore fails the test.

## Acceptance limits and subsequent tests

Success means all sixteen expected PT sets match, including the final switch
from Non vibrato to Vibrato. The known first-note CC102 length-ordering warning
remains separate and must not be silently fixed by reordering.

This first export does **not** test Con vibrato or Wide vibrato selection,
restoration after a temporary attribute, all high pitches, or live behavior.
After it passes, use a separate score copy to test additional group members and
a temporary attribute over an ongoing direction, checking the actual Dorico
playback-technique lane. Preserve the baseline score/export for comparison.
Then validate exact values and ordering through the live bridge, mid-passage
starts, reconnects, and overlapping voices. Detailed live-test setup is deferred
until a safe capture path exists; do not route this map to the legacy decoder.

Only pitches 0–2 and 127 are reserved by this explicit violin probe (musical
range 55–103). The result cannot establish that the entire proposed low/high
reservation is appropriate for every instrument or that all 52 conduits work.
