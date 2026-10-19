# Strings r2 — MIDI export comparison, 2026-09-24

**Update:** the [fresh live capture after reopening Dorico](smoke-results-03.md)
now matches this export for all 16 PT sets. The exact cause of the first-run
discrepancy remains unproven; both traces are preserved.

**All 16 exported note-on PT sets match the corrected expectations.**
Both discrepancies in the first live capture are absent from the MIDI export:
Non vibrato is transmitted, and explicit Natural clears the active add-ons.
The map therefore demonstrably supports the intended combinations/cancellation
in this export. Do not change the map or reject add-on composition on the basis
of the first live capture alone.

## Evidence

- File: `smoke-r2.mid`, supplied by the user after inspecting the score and
  exporting the assigned map. No score edits were reported in that exchange.
- SHA256: `8e14700934c61f89265fa32ea62ca3c94e20a8a5dc6150521f27054f039328c8`.
- Standard MIDI file, 480 ticks/quarter, two tracks. Track 0 has no channel
  events; track 1 has 177 channel events, all on port 0/channel 1.
- Sixteen musical NoteOns, all MIDI72. State reconstruction uses the r2
  allocation in original track-event order, without sorting controller commands
  ahead of notes at equal ticks. All 50 initialization commands are present.
- Expectations use the original smoke fixture except note 15, corrected to no
  active add-ons because the user's Dorico display reports Natural alone there.
  Note-length categories are not part of this PT-set pass.

| Check | Export evidence | First live capture |
| --- | --- | --- |
| Temporary Flautando expires | CC112=0 at tick 1900; note 5 at 1920 is unspecified | Worked |
| Non vibrato at 3.1 | CC105=1 at tick 3830; note 9 at 3844 has Non vibrato + Overpressure + Sul tasto | Non vibrato absent |
| Pressure replacement at 3.3 | At tick 4790, Flautando + Non vibrato + Sul tasto turn on; note 11 at 4802 has all three | Non vibrato absent; other two correct |
| Fouetté at 4.1 | At tick 5750, pressure/stroke/vibrato/contact commands establish all four; note 13 at 5760 matches | Non vibrato absent; other three correct |
| Détaché at 4.2 | At tick 6230, CC109=2 with the other three add-ons; note 14 at 6244 matches | Non vibrato absent; other three correct |
| Natural at 4.3 | At tick 6700, CC109/112/105/110 all clear; note 15 at 6720 has no active add-ons | No clears; stroke/pressure/contact remained active |
| Vibrato at 4.4 | CC105=3 at tick 7190; note 16 at 7203 has only Vibrato | Vibrato plus the three unwanted retained add-ons |

The other notes also match their corrected expected sets. Off→On ordering at
the observed replacements is clear-then-set, separated by 10 ticks. No later
old Off erased a new On before a musical note in this export.

## Timing caveat retained

At tick 0, the first musical note is track-event index 53. CC102=2 follows it
at index 54 at the same tick; the earlier Init set CC102=0. Thus strict file
order gives the first note the initialized base code (Natural/VeryShort), not
the subsequently written Natural/Medium code. Its **PT set** is still correct.
This is the already observed export ordering caveat, not proof of fully correct
per-note length timing. The first live capture saw base code 2 before that note
under the adapter's equal-offset parameter-before-events policy.

## Interpretation and next discriminator

The round-trip map is structurally unchanged, the user's active-PT display is
correct, and the MIDI export contains the required commands. The first live
trace does not. That narrows the investigation to differences between the live
run and the export: playback-state/cache freshness, the actual score state at
the time of each operation, or live VST3 generation/delivery/capture behaviour.
It does **not** yet identify a specific Dorico bug or prove the probe path faulty.

Next: save the existing test project without changing the notation or map,
fully quit and reopen Dorico, and make one fresh live capture from the beginning
through all 16 notes with the same r2 map and **Fiddle MIDI Probe** on Port 1 /
Channel 1. Quit afterward to close the new trace. Preserve the original trace,
MIDI export and round-trip map. If the fresh capture agrees with export, the
first run was not representative of the freshly loaded score; investigate
edit/playback refresh before asserting an exact cause. If it still differs,
isolate the live VST3 path with this export as the reference.

No new map, runtime decoder, plugin installation or production routing change
is warranted by this comparison. Active KS groups, overlapping notes and
mid-passage starts remain deferred until the live discrepancy is resolved.
