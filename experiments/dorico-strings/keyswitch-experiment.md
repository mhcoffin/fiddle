# C r3: independent modifier commands

> **Archived procedure.** C r3 was subsequently exported successfully; see
> [the experiment index](README.md) and [later live results](live-results.md).
> The original status/setup instructions below are historical, not a new test
> request. Current work follows [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md).

Status: generated and locally tested; **not yet tested in Dorico**. This is an
isolated wire experiment, not a production map or installed Fiddle decoder.

## Next user test

Reuse the completed 16-note Dorico test score. No notation changes are needed.

1. Import [keyswitch.doricolib](generated/keyswitch.doricolib).
2. Explicitly assign **Fiddle EXP Strings C - keyswitch r3** to the test endpoint.
   Merely importing it does not change the assigned map.
3. Export MIDI to `experiments/dorico-strings/keyswitch-r3.mid`.

Use the disposable test endpoint, **not the Fiddle plug-in or a working score**.
These low notes may sound or trigger a normal VSTi's own keyswitches. Playback
is not required for this export test; if auditioning, mute its audio output.
Do not install into DefaultLibraryAdditions. Keep the existing A/B captures.

From the repository root:

```sh
python3 experiments/dorico-strings/inspect_midi.py experiments/dorico-strings/keyswitch-r3.mid --variant keyswitch --expect experiments/dorico-strings/expected.json
```

## Encoding

The successful A r2 articulation/length and other CC encodings remain unchanged.
Only Flautando, Overpressure, and Fouetté move from CCs to command notes:

| Modifier | ON note | OFF note | Replaces |
| --- | --- | --- | --- |
| Flautando | 0 | 1 | CC116 |
| Overpressure | 2 | 3 | CC117 |
| Fouetté | 4 | 5 | CC118 |

These are numeric MIDI pitches, avoiding octave-label conventions. Each add-on
has one ON action and one OFF action; there are no compound modifier switches.
Both actions are positive-velocity keyswitch note-ons. The command latches its
modifier until the opposite command arrives. Physical note-off (including
note-on with velocity zero) merely releases the short command pulse: it does
**not** cancel the modifier. Repeated identical commands are idempotent.

Initialization sends the three OFF commands and initializes the five CCs
(102, 103, 106, 107, 108). The offline decoder keeps state independently for
each port/channel. It consumes both command pitches and their releases, so
they never count as musical notes. Missing initialization stays unknown rather
than being silently assumed OFF. Same-tick events retain their actual file
order; late state changes generate warnings. It refuses ambiguous cross-track
protocol ordering.

This reduces the probe from eight to five CCs. Applied to the full draft it
could save three of eighteen CCs, **if host and live-path tests succeed**. It
does not fix Dorico's add-on selection algorithm; it tests whether separate
keyswitch actions preserve the independence observed with separate CCs.

## Limits and acceptance

The declared musical range is MIDI 55–103 for this violin probe. Commands 0–5
are reserved only for this profile, not for all instruments. The inspector
warns about other out-of-range notes; a genuine musical note exactly matching
a command is intrinsically indistinguishable, so the reservation must be an
explicit profile contract. Do not generalize it to percussion or wide-range
instruments without checking their ranges.

Expected result: precisely sixteen musical notes, the same PT sets as A r2,
and cancellation of Fouetté, Overpressure, and Flautando on notes 13, 14, 15.
The known first-note CC102 ordering warning may persist; report it separately
instead of calling the entire protocol validated. Initialization keys emitted
after the first note are likewise a failed/inconclusive ordering result.

After the export works, test the actual live VST3 stream, mid-passage starts,
stop/restart/reconnect, overlapping voices, and profile/instrument changes.
Production decoding and filtering before musical-note processing are not
implemented. No live app or installed map is changed by this experiment.
