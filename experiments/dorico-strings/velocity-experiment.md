# D r4: velocity-coded keyswitch commands

> **Archived procedure.** Retain this failed shared-pitch experiment as evidence,
> not a recommended encoding or current work item. Later outcomes are linked
> from [the experiment index](README.md). The active plan is
> [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md).

Status: **export tested; 13/16 PT sets match, with missing modifier commands**.
See [round 4 results](results/round4.md). No identical re-export is needed; the
next test is [E r5](dimensions-experiment.md).
No installed map, application, or production decoder is changed.

## User test

Reuse the same completed 16-note Dorico test score, with no notation changes.

1. Import [velocity.doricolib](generated/velocity.doricolib).
2. Explicitly assign **Fiddle EXP Strings D - velocity r4** to the test endpoint.
   Importing alone does not change its assignment.
3. Export MIDI as `velocity-r4.mid` in `/Users/mhc/fiddle/experiments/dorico-strings`.

Use the disposable project and normal test instrument, **not the Fiddle plug-in**.
These are protocol notes which the production Fiddle decoder cannot consume.
No playback is needed to export. If auditioning, mute the test instrument's
audio output: a normal VSTi might sound these notes or interpret its own switches.
Do not install into DefaultLibraryAdditions. Preserve previous maps and captures.

## What this tests

Yesterday's C r3 export recovered every expected PT set, with the same unresolved
first-note CC102 ordering warning as A. C used six different command pitches,
all at velocity 127. D keeps its five CCs, base articulations, mutex groups and
expected musical notes, but moves all six commands onto **MIDI pitch 0**:

| Modifier | ON velocity | OFF velocity |
| --- | --- | --- |
| Flautando | 1 | 2 |
| Overpressure | 63 | 64 |
| Fouetté | 126 | 127 |

Values span low, middle, and high velocities. The same score tests simultaneous
commands for independent modifiers, retaining an existing modifier when another
is added, and cancellation on notes 13–15. Initialization sends velocities
2, 64, and 127 on pitch 0 to explicitly establish all three as OFF.

D also enables the map's `allowMultipleNotesAtSamePitch` option because commands
share a pitch and can overlap. Its effect on these keyswitch actions is part
of the host test, not an assumption that Dorico will preserve every command.
A/B/C remain byte-for-byte unchanged with their original option value.

The offline decoder recognizes exact pitch/velocity pairs. It latches each
modifier independently and consumes command notes before counting musical notes.
Note-off and velocity-zero note-on events have no semantic effect; a release
cannot identify which velocity-coded command it belongs to. Unexpected positive
velocities on the reserved pitch generate warnings rather than guessed commands.
Unknown initial state, port/channel isolation, and strict same-tick file ordering
are retained. Older decoders reject the new schema rather than misread it.

The result must contain precisely sixteen musical notes with the existing
expected PT sets. Missing commands, changed velocities, or same-pitch suppression
are failures to investigate, not reasons to silently change the expectations.
The existing first-note length warning remains a separate acceptance issue.

```sh
python3 experiments/dorico-strings/inspect_midi.py experiments/dorico-strings/velocity-r4.mid --variant velocity --expect experiments/dorico-strings/expected.json
```

## Scope and next gate

This tests six selected codes, not all 127 velocities or arbitrary polyphony.
Pitch 0 is reserved only in this explicit violin probe (musical range 55–103),
not globally for all instruments. Future production decoding must be profile-aware
and consume command releases as well as note-ons before forwarding to VSTi.

If this export succeeds, test exact velocity preservation and event ordering
through the live Dorico → Fiddle VST3 path, plus starts mid-passage, reconnects,
and overlapping voices. No live compatibility or bandwidth claim follows from
synthetic tests or an export alone.
