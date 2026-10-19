# r6 reliability test — seven bars, about 14 seconds

> **Archived test procedure with recorded outcomes.** Do not treat setup/replay
> instructions below as an outstanding task. Retain the failed automatic-return
> hypothesis and the successful explicit-return checks as separate evidence.
> Current work follows [Orchestral bridge maps](../../docs/orchestral-bridge-maps.md).

Use a **disposable project** and the existing **Fiddle EXP Strings F - high
vibrato r6** expression map. No new map, decoder, or plug-in installation.
Fiddle must still be running with the experimental opt-in on **Port 1,
Channel 2**; its plug-in must expose at least two channels. Do not restart
Fiddle normally or use the experimental map in a production score.

## Setup (once)

**Recorded outcome:** the original automatic-restoration probe at bar 5 beat 4
failed in both live inspection and MIDI export. The user then added explicit
vibrato at that position. With that amendment, the focused live checks for full
and repeated playback, both mid-passage starts, and interrupted backwards seek
passed. The original scaffold/expectations below retain the no-restatement
experiment; they are not evidence that automatic restoration works. See
[live results](live-results.md) and the [original corrected export analysis](results/reliability-r6-corrected.md).

Import [reliability-r6.musicxml](generated/reliability-r6.musicxml) into Dorico.
It contains seven 4/4 bars at quarter = 120, 28 quarter-note onsets and 38
individual notes. Pitch is C5, with C5/E5/G5 chords in bar 1 beat 4 and bar 6.
Route its violin to the ordinary Fiddle plug-in on Port 1, Channel 2 and select
the existing r6 expression map. MusicXML's channel hint does not verify routing.

**The printed labels are ordinary text, not active playing techniques.** Add
the actual techniques below. Use Dorico's built-in playback identities; a
similar-looking vendor playing-technique name can map to something different.
If a suitable visible playing technique is missing, ask before substituting:
a project-local notation pointing at the existing built-in PT is acceptable,
but inventing a new playback technique would change the test.

| Position | Add actual playing technique | Playback identity / duration |
| --- | --- | --- |
| Bar 1 | Nothing | No PTs |
| Bar 2 beat 1 | Non vibrato | `pt.nonVibrato`, persistent |
| Bar 3 beat 1 | Con vibrato | `pt.conVibrato`, persistent |
| Bar 4 beats 1–4 | Wide vibrato | `pt.wideVibrato`, explicit whole-bar duration |
| Bar 5 beat 1 | Vibrato | `pt.vibrato`, persistent |
| Bar 5 beats 2–3 | Non vibrato | `pt.nonVibrato`, **duration limited to those two notes** |
| Bar 6 beat 1 | Vibrato | `pt.vibrato`, explicit fresh baseline for chord test |
| Bar 7 beat 1 | Vibrato | `pt.vibrato`, explicit background for the flautando test |
| Bar 7 beats 2–3 | Flautando | `pt.flautando`, **duration limited to those two notes** |

For each persistent direction, select only its starting note before adding it.
For the two duration-limited directions, select the two notes first, then add
the playing technique; verify its duration covers those notes and ends before
beat 4. Do **not** add a cancelling/restoring marking on beat 4: that would hide
the behavior we need to measure. Do not group/join it with the earlier direction.
Do not add slurs, dynamics, articulations, or extra techniques.

Correction from live setup: Dorico's Wide vibrato definition is an Attribute,
not a persistent Direction. In the corrected local score, its marking has an
explicit four-beat duration (2.0 seconds), ending at the bar-5 boundary. Verify
all four bar-4 notes during playback; do not assume coverage from its label.
No playback-definition type or expression-map changes are part of this test.

Dorico documents that range-selected Direction techniques acquire a duration
and affect playback within that duration. A visible continuation line by itself
is not proof of playback duration. See the official
[input instructions](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/write_mode/write_mode_notations_input/write_mode_playing_techniques_inputting_popover_t.html)
and [duration explanation](https://www.steinberg.help/r/dorico-pro/6.2/en/dorico/topics/notation_reference/notation_reference_playing_techniques/notation_reference_playing_techniques_duration_c.html).
Whether the previous mutex member is restored is deliberately an experiment.

Save a new `.dorico` project beside the scaffold (suggested name:
`reliability-r6.dorico`); do not overwrite the original 16-note project.

## Playback passes — do one, then let the assistant inspect

1. **Whole passage:** play bars 1–7, then stop. Expected: all 38 notes,
   including all chord tones. Every note has Medium written length. Bar 5
   beat 4 should recover `pt.vibrato`; bar 7 beat 4 should retain vibrato but
   lose flautando. These are desired outcomes, not already-proven behavior.
2. **Repeat after STOP:** play from the beginning again. Compare with pass 1;
   there must be no leftover wide-vibrato, non-vibrato, or flautando state.
3. **Start inside an existing direction:** while stopped, position playback
   at bar 2 beat 3; play to the end. The first note must already have
   `pt.nonVibrato` without replaying bar 2 beat 1 (30 individual notes remain).
4. **Start inside the temporary override:** while stopped, position at bar 5
   beat 3; play to the end. First note: non-vibrato. Next: restored vibrato
   (18 individual notes remain). Do not start at the earlier non-vibrato mark.
5. **Interrupt and seek backwards:** play from bar 4, stop during it, return
   to bar 2 beat 3, then play. Expected same states as pass 3; wide vibrato
   must not leak backwards. Auditioned selection clicks are not part of the take.

The assistant should inspect each pass before the next overwrites useful
records. Do not clear the inspector or switch instruments for this test.
Do not judge PT transport by timbre alone: these vendor maps do not implement
every requested technique. Check incoming canonical IDs, lengths, chord tones,
and cancellation separately from vendor switch selection and audible output.

## Evidence and limits

Expectations are in [reliability-r6.expected.json](generated/reliability-r6.expected.json),
one row per musical note, grouped by bar/beat. Chord-note ordering is immaterial;
compare pitch/PT multisets within an onset, not the exact order of chord records.
The old fixed 16-note C++ replay/expectation is **not** valid for this new score.
If needed, a MIDI export or separate probe capture can diagnose discrepancies;
an export alone does not prove live seek/stop behavior. Do not replace the live
instrument with the capture-only probe without coordinating that separate test.

All notes share one endpoint and the three Violin I layers. This does not test
different simultaneous per-note PTs on one MIDI channel, independent endpoints,
other families/ranges, fresh-process mid-passage starts, or full built-in coverage.
Reproduce a failed restoration rather than rewriting the decoder to guess it.

Regenerate only these new artifacts with `python3 experiments/dorico-strings/reliability.py`;
use `--check` for read-only validation. Existing map bytes remain unchanged.
