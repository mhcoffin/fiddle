# Strings r2 — active keyswitch test (12 notes)

Status: **completed**. After correcting the score, the
[second capture](keyswitch-results-02.md) matches all 12 PT sets. No map changes
were needed. The initial missing-strum capture is retained; no further unchanged
repeat is requested. Future scores will omit the instructional text markers
at the user's request; this completed fixture remains unchanged.
The earlier 16-note CC baseline passed
after reopening Dorico. This separate score tests active commands on a low KS
pitch and a high KS pitch, replacement of velocity-coded enum values, absence
after a one-note attribute, and coexistence with a CC dimension. **Overlapping
musical notes remain a separate subsequent test.**

Use the **unchanged r2 bridge map** and silent **Fiddle MIDI Probe**. Do not route
to ordinary Fiddle or a sounding VSTi. Save this as a new disposable project;
retain the previous test score and captures.

## Enter the score

Import [keyswitch-r2.musicxml](keyswitch-r2.musicxml). It contains twelve C5
quarter notes in three bars of 4/4 at 80 bpm (about nine seconds of playback).
The imported text is only an instruction sheet: **add actual playing techniques**.
No custom playback techniques, map edits or duration-handle adjustments are needed.

These notation-to-PT associations were checked in the installed Dorico 6
`scoreLibrary.xml`, not guessed from their names:

| Playing-technique item | Built-in playback technique | r2 action |
| --- | --- | --- |
| **Xylo** (pitched/tuned percussion category; text `xylo`) | Sons xylophoniques, `pt.sonsXylophoniques` | MIDI note **8**, velocity **2**; Off velocity **1** |
| **Strum Up** (Guitar category) | `pt.strumUp` | MIDI note **120**, velocity **2**; Off velocity **1** |
| **Strum Down** (Guitar category) | `pt.strumDown` | MIDI note **120**, velocity **3**; Off velocity **1** |
| **Vibrato** (not Con vibrato) | `pt.vibrato` | CC105=3; Off=0 |

The unusual category placement is just Dorico's palette organization; the test
uses these existing PTs on the violin staff to exercise the bridge. Check the
selected item's Playback technique in Library → Playing Techniques if a name
is ambiguous. Use the palette/library items rather than ordinary staff text.
If an item is unavailable, tell us which one—do not substitute an arpeggio,
glissando, string upbow/downbow, or new vendor PT. The bridge encodes all of those
separately. This capture-only single-note test does not validate how a strum
should ultimately be rendered musically.

| Location | Note | Actual PT to add | Expected active PTs |
| --- | ---: | --- | --- |
| 1.1 | 1 | None | None |
| 1.2 | 2 | **Xylo**, persistent | Sons xylophoniques |
| 1.3 | 3 | **Strum Up**, this note only | Xylo + Strum up |
| 1.4 | 4 | **Strum Down**, this note only | Xylo + Strum down |
| 2.1 | 5 | None; no Natural here | Xylo only; Strum must expire |
| 2.2 | 6 | **Vibrato**, persistent | Xylo + Vibrato |
| 2.3 | 7 | **Strum Up**, this note only | Xylo + Vibrato + Strum up |
| 2.4 | 8 | **Strum Down**, this note only | Xylo + Vibrato + Strum down |
| 3.1 | 9 | **Natural / ord.**, linked to `pt.natural` | None; including no Vibrato |
| 3.2 | 10 | **Strum Up**, this note only | Strum up only |
| 3.3 | 11 | **Strum Down**, this note only | Strum down only |
| 3.4 | 12 | None | None; Strum must expire |

Strum up/down are built-in **Attributes**. Enter them on individual notes, with
no extended duration. Xylo and Vibrato are **Directions** and should continue
until Natural at 3.1. Verify these combinations in Dorico's active-technique
display before capture, especially 2.3 and 3.1. Avoid slurs, ties, dynamics,
arpeggios and additional voices in this first KS pass.

## Capture once from a fresh session

1. Assign **Fiddle MIDI Probe**, Port 1 / Channel 1, and **Fiddle Bridge -
   Orchestral Strings r2 - INSPECTION ONLY** in the new project's endpoint setup.
   Import r2 into the project if necessary. Import alone does not assign it.
2. Save after all notation/map/endpoint edits. **Fully quit and reopen Dorico**
   before recording; the earlier test exposed an unexplained edit/live-state
   discrepancy that disappeared in a fresh session.
3. Reopen the saved test project, confirm its capture-only endpoint, and play
   once from the beginning through all twelve notes. Silence is intentional.
4. **Fully quit Dorico afterward** so the probe writes its completion footer.
   Tell the assistant **done**. We will locate and validate the new trace.

An optional MIDI export can be saved as `keyswitch-r2.mid` in this directory,
without overwriting `smoke-r2.mid`. Do not overwrite earlier captures or maps.

## Analysis contract

Use [keyswitch-r2.expected.json](keyswitch-r2.expected.json), bound by SHA256 to
the unchanged r2 map, and [allocation.json](allocation.json). These are hypotheses
until the actual active-technique display and trace agree.

- Validate the closed trace and all input/drop counts, distinguish auditions
  using transport state, and select the correct pass and endpoint.
- Count **12 musical notes**, not command-note pulses. Decode exact KS velocities
  on pitches 8 and 120. Velocity 1 clears that dimension; velocity zero / physical
  NoteOff releases a command pulse and must not clear a logically active PT.
- At notes 3/4, 7/8 and 10/11, test value 2→3 replacement on the **same pitch**.
  Values do not represent simultaneous flags. At notes 5/12 verify explicit
  velocity-1 expiry without a new same-group PT.
- Verify that low-pitch KS8 and high-pitch KS120 coexist, and that adding CC105
  preserves both. Review Off→On ordering and any stale clear after a replacement.
- Confirm Natural clears Xylo and Vibrato at note 9. Do not assume the previous
  test's original (incorrect) expectation of retaining vibrato through Natural.
- Establish state only from received commands, including the full Init. Preserve
  raw block/sample/source/sequence information, command-note releases and
  equal-offset parameter/event-list ties; do not reorder away a discrepancy.
- This is not an overlap, mid-start, arbitrary-concurrency, musical-strum,
  runtime-decoder or VSTi-output acceptance test.

## Local checks

```sh
python3 experiments/orchestral-strings-bridge/keyswitch_score.py
python3 experiments/orchestral-strings-bridge/keyswitch_score.py --check
python3 experiments/orchestral-strings-bridge/smoke_score.py --check
python3 experiments/orchestral-strings-bridge/generate.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```

The original score and map generation checks must remain byte-for-byte unchanged.
Local tests verify fixture structure and allocation agreement, not Dorico output.
