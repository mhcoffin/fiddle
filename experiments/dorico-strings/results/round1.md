# Strings wire experiment: first real Dorico exports

> **Historical result.** Preserve these observations and hashes. Later rounds
> supersede its prospective test instructions; the active plan is
> [Orchestral bridge maps](../../../docs/orchestral-bridge-maps.md).

Source files: `../separate.mid` and `../packed.mid`, received 2026-09-21.
Both have 480 PPQN, 16 C5 notes in track 1, port 0, channel 1. All owned
controllers are initialized, all observed values are valid, and the two files
have the expected different controller footprints (8 versus 6).
The original MIDI files were not modified. SHA-256 fingerprints, original map
digests, decoded notes, and controller events are in [round1.json](round1.json).
That report was captured before regenerating the maps as r2.

## Observations

The comparison uses the agreed correction to note 6: Staccatissimo + Marcato,
without Tenuto. No other expectations were relaxed to match the observations.

| Notes | Intended PTs, in addition to any persistent Non vibrato | A: separate | B: packed |
| --- | --- | --- | --- |
| 1–3 | Unmarked; Staccato; Tenuto | PT sets match | PT sets match |
| 4 | Staccato + Tenuto | Staccato only | Staccato only |
| 5 | Staccato + Tenuto + Accent | Accent only | Accent only |
| 6 | Staccatissimo + Marcato | Marcato only | Marcato only |
| 7–10 | Non vibrato, temporary Accent, then Flautando | Match | Match |
| 11 | Flautando + Overpressure | Both present | Overpressure only |
| 12 | Flautando + Overpressure + Fouetté | All three present | Overpressure only |
| 13 | Flautando + Overpressure | Both restored after Fouetté ends | Overpressure only |
| 14–16 | Flautando, then none, then Vibrato replaces Non vibrato | Match | Match |

A has three PT-set mismatches; B has six. A's modifier sequence demonstrates
successful coexistence and note-boundary restoration for these tested notes.
It does not establish behavior for overlapping voices or arbitrary starts.

## Generator defect: compound spelling

The experimental generator wrote switch combinations as
`pt.accent, pt.staccato, pt.tenuto`. The existing Fiddle map, installed Dorico
resources, and vendor maps instead use `+` inside switch combinations, while
commas separate mutex-group membership. For example, the existing Fiddle bridge
has `pt.staccato+pt.tenuto`; VSL Duality contains `pt.accent+pt.legato`.

Fiddle's parser explicitly accepts both separators, so the C++ round-trip checks
could not detect this host-serialization defect. The identical losses of compound
base states in A and B are consistent with Dorico ignoring/misinterpreting these
entries. **This is the leading explanation, not yet proven by a corrected export.**
The failures cannot yet tell us whether packed add-ons are intrinsically viable.

Revision 2 uses `+` for base/add-on conjunctions and retains commas for MEGs.
It adds a regression check for the actual emitted spelling, new map identities
and visible revision names, and corrected note-6 expectations/scaffold. No wire
values or production Fiddle behavior changed.

## Independent first-note ordering observation

Both files initialize CC102=0, then emit the first NoteOn at tick 0, then
CC102=2 at the same tick. In strict file order the first note consequently sees
the initialized Very-short category instead of the subsequently selected Medium
category. The other 15 notes see Medium. PTs on the first note are empty either
way, so the original PT-only comparison did not report this.

The inspector now warns about a changed owned CC following a note-on at the
same tick, retaining the actual file order. This is a real export ordering
observation, but does not establish the order of Dorico's live VST3 events or
whether Fiddle's eventual note-association logic can safely resolve it. It must
not be silently repaired in the diagnostic to manufacture a pass.

## Next evidence needed

Re-import the corrected maps, explicitly select the new **r2** entries, and
export the same score as `separate-r2.mid` and `packed-r2.mid`. No score rebuild
is needed. This isolates the separator correction while preserving the first
captures for comparison. Then decide whether add-on packing succeeds; live MIDI,
mid-passage starts, and overlapping voices remain separate acceptance gates.
