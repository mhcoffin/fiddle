# E r5: one pitch per independent dimension

> **Archived procedure.** The successful export remains evidence for independent
> conduits; the old next-test instructions below are not the current work queue.
> See [later live results](live-results.md) and the active
> [Orchestral bridge maps plan](../../docs/orchestral-bridge-maps.md).

Status: **16/16 PT sets match the export**, with the existing first-note ordering
warning. See [round 5](results/round5.md). No identical re-export is needed;
the next test is [F r6](high-vibrato-experiment.md).
No production map, decoder, app, or user project assignment has been changed.

## User test

Reuse the completed 16-note disposable Dorico test score, with no notation changes.

1. Import [dimensions.doricolib](generated/dimensions.doricolib).
2. Explicitly assign **Fiddle EXP Strings E - dimensions r5** to the same endpoint.
3. Export as `dimensions-r5.mid` into `/Users/mhc/fiddle/experiments/dorico-strings`.

Use the normal test instrument, **not the Fiddle plug-in or a working score**.
No playback is needed for the export. If auditioning, mute the instrument's audio
output because it may sound or interpret the protocol notes as its own switches.
Do not install in DefaultLibraryAdditions; retain the earlier maps and captures.

## Controlled change from D

| Dimension | MIDI pitch | ON velocity | OFF velocity |
| --- | --- | --- | --- |
| Flautando | 0 | 1 | 2 |
| Overpressure | 1 | 63 | 64 |
| Fouetté | 2 | 126 | 127 |

Only the Overpressure and Fouetté command pitches change from D, apart from the
new map/revision identity. Velocities, five CCs, 90 base switches, 16 add-ons,
musical mutex memberships, and `allowMultipleNotesAtSamePitch=true` are retained.
There are no compound modifier switches. These independent modifiers are binary
dimensions, not a musical mutex group with each other.

Initialization sends the OFF velocity on each dimension's pitch. Both ON and
OFF commands are positive-velocity note-ons; physical note releases and zero-
velocity note-ons are ignored semantically. The decoder requires the exact
pitch/velocity pair, isolates ports/channels, reports unknown commands and
missing initialization, and preserves actual same-tick event order.

The test asks whether **one distinct pitch per independent dimension** preserves
coexistence and cancellation while encoding each dimension's state as velocity.
Expected PT sets remain unchanged: all three modifiers on note 12, then Fouetté
absent on note 13, Overpressure absent on note 14, and Flautando absent on note 15.

```sh
python3 experiments/dorico-strings/inspect_midi.py experiments/dorico-strings/dimensions-r5.mid --variant dimensions --expect experiments/dorico-strings/expected.json
```

The existing first-note length-ordering warning may persist and is a separate
acceptance issue. It must not be silently repaired or mistaken for PT-set loss.

## What remains after this test

Passing would validate these three binary dimensions in this export only.
Before choosing a production allocation, we still need:

- A genuinely multi-valued mutex dimension encoded on one pitch, including
  member-to-member changes and restoration after a temporary attribute.
- Verification of the proposed high-pitch range and instrument-specific safe
  reservations. This probe reserves only pitches 0–2 for the violin range 55–103.
- Exact command values and ordering through the live VST3 bridge, including the
  first note, playback starting mid-passage, reconnects, and overlapping voices.

These are next gates, not claims that the 52-conduit design is already validated.
