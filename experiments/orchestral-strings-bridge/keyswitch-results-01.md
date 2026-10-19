# Strings r2 — first active keyswitch capture, 2026-09-24

**Follow-up:** after the user's score correction, the
[second capture](keyswitch-results-02.md) matches all 12 PT sets. No map or plugin
changes were needed. This first result is retained as historical evidence.

**Partial success: 8/12 complete PT sets match.** The first Strum up/down pair
works, as do Xylo's low-pitch commands, CC/KS coexistence and Natural cancellation.
The four later strum On commands are absent altogether. Do not infer general KS
composition failure: strums are also absent where they should be the only PT.

## Capture integrity

- Original: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790296536747117-2/capture.jsonl`.
- Preserved copy: `local-captures/keyswitch-r2-pass1.jsonl` (git-ignored).
- SHA256: `5478b4a80b2928b3acedde9da8fae0ae57f40bf5ca006a8ad9156f6f30b53461`.
- Closed schema-1 trace; 3,236 blocks; zero capture drops. Strict trace reader
  accepts complete ordered blocks/events with zero invalid/dropped adapter input.
- One playback pass: blocks 1644–2449 inclusive. Twelve musical NoteOns, all
  MIDI72 on Port 1 / Channel 1. One idle audition is excluded.
- All add-on dimensions are initialized before playback in block 1629; base
  code is also established. At all twelve musical onsets, state is complete and
  base CC102=2 (Natural/Medium). No unknown dimensions were assumed clear.
- Reconstruction uses `allocation.json` and `keyswitch-r2.expected.json`, bound
  to the unchanged r2 map, in recorded adapter order. Physical command NoteOffs
  are retained as evidence but do not clear logical PT state.

## Note comparison

| Note | Location | Actual active PTs | Result |
| --- | --- | --- | --- |
| 1 | 1.1 | None | Match |
| 2 | 1.2 | Sons xylophoniques | Match |
| 3 | 1.3 | Sons xylophoniques + Strum up | Match |
| 4 | 1.4 | Sons xylophoniques + Strum down | Match |
| 5 | 2.1 | Sons xylophoniques | Match: Strum expired |
| 6 | 2.2 | Sons xylophoniques + Vibrato | Match |
| 7 | 2.3 | Sons xylophoniques + Vibrato | Missing Strum up |
| 8 | 2.4 | Sons xylophoniques + Vibrato | Missing Strum down |
| 9 | 3.1 | None | Match: Natural cleared both dimensions |
| 10 | 3.2 | None | Missing Strum up |
| 11 | 3.3 | None | Missing Strum down |
| 12 | 3.4 | None | Match, but not evidence of expiry after the missing final strum |

There are no unexpected extra PTs. The expected high-pitch KS120 commands for
notes 7/8/10/11 never appear in this trace; they are not merely late or erased by
a later Off. The capture does not expose score PT identities or map identity.
The initial working pair agrees with r2, but that does not establish the later
score markings or diagnose the reason for the missing commands.

## Working KS transitions and timing

- KS8 velocity 2 at sample 32369 establishes Xylo before note 2 at 33075.
- Before note 3, Xylo clears at 65356, then KS8 velocity 2 and KS120 velocity 2
  arrive at 65444; note 3 is at 66326. Two independent KS pitches coexist.
- Before note 4, both dimensions clear via velocity 1 at 98431; KS8 velocity 2
  and KS120 velocity 3 arrive at 98519; note 4 is at 99225. This confirms the
  tested Strum-up→Strum-down value change on one pitch, with Xylo preserved.
- At 131506 both dimensions clear, then Xylo alone is reasserted at 131594.
  Note 5 at 132300 has Xylo only. The first Strum attribute's disappearance works.
- At 164669, CC105=3 and KS8 velocity 2 establish Vibrato + Xylo before note 6.
- Natural: CC105 clears at 263188, KS8 velocity 1 clears Xylo at 263806; note 9
  at 264864 has neither. Different command types have different lead times here.
- For the observed KS replacements, Off→On is 88 samples (about 2 ms at 44.1 kHz),
  not the roughly 16 ms seen for CC clears in the previous score. Do not impose
  a universal lead-time assumption. No old Off erases the working replacement
  before notes 3/4.

Raw normalized KS velocities round to the requested MIDI values 1, 2 and 3.
Command-note physical releases on pitches 8/120 are present; they are not logical
Off commands. There are 18 negative-position preroll blocks, no backward host
clock during this pass, and parameter/event-list equal-offset ties at
`(1661,408)`, `(1725,10)`, `(1726,204)`, `(1789,317)`, `(1854,112)`, `(1855,306)`,
`(1918,419)`, `(1920,101)`, `(1983,214)`. Results use the adapter's documented
merge policy, not a claimed VST3 cross-list total order.

## Next check — score active PTs before changing anything

Ask the user to inspect Dorico's active playback techniques at:

- **2.3:** should include Strum up + Sons xylophoniques + Vibrato.
- **3.2:** should be Strum up alone.

If either lacks Strum up, inspect that specific notation object's association
and position. If both displays are correct, export MIDI from this unchanged
score to `keyswitch-r2.mid` to distinguish export output from the live run, as
with the previous score. Preserve the current capture and do not assume that
the earlier restart-related discrepancy has the same cause here.

No map/runtime changes were made. Three-way KS/KS/CC coexistence and the final
strum-only pair remain unvalidated. Overlap testing stays deferred.
