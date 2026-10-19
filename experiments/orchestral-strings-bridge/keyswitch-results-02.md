# Strings r2 — corrected active keyswitch capture, 2026-09-24

**All 12 note-on PT sets match.** The user corrected the score, saved/reopened
and repeated the test. Every previously missing strum is now present. This
supports the user's explanation of score-entry error for the first KS run;
no bridge-map, decoder or plugin change was needed.

## Integrity and scope

- Original trace: `/var/folders/3k/4c2wqkxn5p50ldsh3glsd78r0000gn/T/fiddle-midi-probe-1790297335441753-2/capture.jsonl`.
- Preserved local copy: `local-captures/keyswitch-r2-pass2.jsonl` (git-ignored).
- SHA256: `9a90767c7a88799ba5945c7e5222b4c9adf576745f9241ee8ab3c6705ff1daff`.
- Closed schema-1 trace; 3,946 blocks, zero capture drops, no invalid/dropped
  adapter input or incomplete/out-of-order records according to the strict reader.
- One playing pass, blocks 2545–3351 inclusive; all twelve musical NoteOns
  are MIDI72 on Port 1 / Channel 1. One idle audition excluded.
- All 49 add-on dimensions initialize in block 2534, with the base established
  separately. No unknown state at any played note; each sees base CC102=2.
- Exact PT sets match `keyswitch-r2.expected.json` under `allocation.json` in
  recorded adapter order, without treating KS NoteOff as a logical clear.

## Results

| Notes | Observed technique sets | Result |
| --- | --- | --- |
| 1 | None | Pass |
| 2 | Sons xylophoniques | Pass |
| 3 / 4 | Sons xylophoniques + Strum up / Strum down | Pass |
| 5 | Sons xylophoniques only | Pass: strum expiry |
| 6 | Sons xylophoniques + Vibrato | Pass |
| 7 / 8 | Sons xylophoniques + Vibrato + Strum up / Strum down | Pass: two KS pitches plus CC |
| 9 | None | Pass: explicit Natural clears all |
| 10 / 11 | Strum up / Strum down alone | Pass |
| 12 | None | Pass: final strum expiry |

The tested high-pitch enum is KS120, velocity 2 for Up and 3 for Down; the
independent low-pitch flag is KS8, velocity 2 for Sons xylophoniques. Velocity 1
clears each KS dimension. The companion CC is CC105=3 for Vibrato.

At note 7, all three commands arrive at sample 197744, before its musical onset
at 198450. Before note 8, Vibrato clears at 230113 and both KS dimensions clear
at 230731; all three are reasserted with Strum down at 230819, before onset at
231525. No old logical Off erases the tested replacement before the new note.

Before Natural/note 9, CC105 clears at 263188 and both KS dimensions clear at
263806; onset at 264864 has no add-ons. Strum-only Up then Down arrive at 296969
and 330044, before notes 10/11 at 297719/330882. The final KS120 velocity-1 clear
is at 363031, before the unmarked note 12 at 363825.

As before, the capture includes negative preroll (18 playing blocks) and
parameter/event-list same-offset ties (16 offsets during playback), but no
backward playing host clock. State results use the documented adapter tie policy;
they do not prove a VST3 cross-list total order. No overlapping musical-note
test, all-profile certification, or Fiddle runtime integration is implied.

The baseline and bounded active-KS test are now complete. Next is a separate
overlapping-notes test. Preserve both first/second captures; do not request
another unchanged repeat of this score.

## User preference for subsequent scores

The user reports that instructional text markers in MusicXML do not import
cleanly and prefers the detailed written guide. **New MusicXML fixtures must
omit instructional text markers.** Put note/bar/beat instructions in a separate
Markdown guide. The scaffold helper now defaults to no instructional words;
explicit marker arguments remain only to reproduce the completed historical
fixtures byte-for-byte. No changes to those MusicXML files are required.
