# Strings r2 — fresh live capture after restart, 2026-09-24

**The bounded 16-note live PT-state test passes.** The user reopened Dorico and
played the same score/map again, then fully quit to close the capture. All 16
musical note-on technique sets agree with both the MIDI export and the corrected
expectations (Natural at note 15 means no active add-ons).

No map, runtime decoder, probe installation or production routing was changed
between the first and second capture. The discrepancy disappeared after the
reported fresh launch. This is consistent with stale playback/edit state in the
first run, **not proof of a particular cache bug or its cause**. The first trace
is retained; it should not be silently discarded as an analysis error.

## Capture integrity and correspondence

- Original: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790294081063226-2/capture.jsonl`.
- Local preserved copy: `local-captures/smoke-r2-pass2.jsonl` (git-ignored).
- SHA256: `dedcc673703e7b6dd46e85d256658ca1b9af94bcbf99abc02a4fc4d9f5c3f3b1`.
- Closed schema-1 trace, 6,206 blocks, zero capture drops. The strict trace reader
  accepts block/event ordering and completeness with no invalid/dropped input.
- One playing pass, blocks 554–1617 inclusive, Port 1 / Channel 1 (trace port 0).
  Sixteen musical NoteOns, all MIDI72. One audition outside playback excluded.
- At idle block 543, **all 50 Init commands** are present: 18 CCs including the
  base plus 32 KS resets. No dimension is unknown at any musical note-on.
- Each played note sees CC102=2 (Natural/Medium) in recorded adapter order.
- Direct independent state reconstruction of the live trace and `smoke-r2.mid`
  confirms identical ordered `(pitch, active PT set)` sequences for all 16 notes.
  The original expected JSON is unchanged; the comparison explicitly corrects
  note 15 to an empty PT set as documented in the preceding reports.

## Key observations

| Test | Fresh live evidence | Result |
| --- | --- | --- |
| Temporary Flautando expiry | CC112 clears at sample 130888; note 5 at 132300 and note 6 have no add-ons | Pass |
| Non vibrato and independent coexistence | CC105=1, CC112=2, CC110=2 at sample 263894; note 9 at 264864 has Non vibrato + Overpressure + Sul tasto | Pass |
| Pressure replacement | Flautando + Non vibrato + Sul tasto set at 330044; note 11 at 330882 has all three | Pass |
| Fouetté coexistence | All four dimensions set at 396194; note 13 at 396900 has Fouetté + Flautando + Non vibrato + Sul tasto | Pass |
| Fouetté → Détaché | Old combination clears at 428563; new combination sets at 429269; note 14 at 430244 has Détaché plus the other three | Pass |
| Explicit Natural | CC109/112/105/110 all clear at 461638; note 15 at 463050 has no add-ons | Pass |
| Vibrato after Natural | CC105=3 at 495419; note 16 at 496301 has only Vibrato | Pass |

The other repeated notes agree as well. No old Off erased the replacement state
before a musical note at these tested boundaries. Dorico again clears/reasserts
surviving dimensions when the selected combination changes; the common Off→On
gap is 706 samples (approximately 16 ms at 44.1 kHz). Note-state snapshotting
remains necessary for future runtime integration.

## Scope and remaining cautions

This validates the tested non-overlapping CC-based combinations, temporary
expiry, replacement and explicit Natural reset in a fresh session. It also
observes all KS **initialization** commands, not active KS enum transitions.

Nineteen playing preroll blocks have negative host sample positions; no backward
clock occurs within this pass. Parameter/event-list ties occur at `(block, offset)`
`(572,193)`, `(830,398)`, `(959,500)`, `(1347,294)`, `(1476,396)`. Note-state results
use the adapter's documented parameters-before-events merge policy, not a claimed
host total order between independent VST3 input lists. The first-note length
ordering caveat in MIDI export also remains; this is not a blanket timing pass.

Next bounded tests: active KS-based enums/coexistence, then overlapping notes at
replacement boundaries, fresh mid-passage starts and stop/restart. The edit-to-live
refresh discrepancy should also be investigated before promising frictionless
map editing. No additional repeat of this unchanged 16-note baseline is required.
