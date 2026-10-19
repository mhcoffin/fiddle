# Strings r2 — paired control and overlap test

Status: **retired as an acceptance test** after the
[fresh capture and shared-voice scope review](overlap-results-02.md).
The original expectations below assume unverified PT resolution across voices
sharing a channel. Preserve them as history, not instructions for another run.
The separate CC baseline and active-KS test passed.

## Original procedure (historical)

This test asks whether ending an older musical note can interfere with the
technique state used for newer notes on the **same MIDI channel**. The map,
decoder and plugin remain unchanged. No instructional text markers are embedded
in the MusicXML; this guide contains all setup instructions.

## Score and routing

Import [overlap-r2.musicxml](overlap-r2.musicxml) into a new disposable project.
It is one violin staff, five bars of 4/4 at 80 bpm, with **17 musical notes**.
The first four bars each have these pitches at beats 1/2/3/4:

- Beat 1: low C4 (MIDI60), down-stem voice.
- Beats 2/3/4: D5, E5, F5 (MIDI74/76/77), up-stem voice.

The low C is a quarter note in control bars **1 and 3**, but a dotted half in
overlap bars **2 and 4**. It should still sound when the D and E start, and finish
before the F starts. Both controls and overlap bars use the same two voices;
only the low note's duration differs. Bar 5 has a single C5 quarter then rests,
for final cleanup. Check those durations and pitches after import; do not add
slurs, ties, dynamics, or additional musical notes.

Route **both voices** to **Fiddle MIDI Probe, Port 1 / Channel 1**, using the
unchanged **Fiddle Bridge - Orchestral Strings r2 - INSPECTION ONLY**. Keep
**independent voice playback disabled**: a second channel would evade the shared
state test. Do not substitute ordinary Fiddle or a sounding VSTi. Silence is
intentional, and the test makes no claim about how a VSTi should change the
timbre of an already-sounding note.

## Enter actual playing techniques

Select the specified **sounding note**, not the other voice's rest. Add the same
built-in PTs used successfully in previous tests. Flautando/Overpressure are
persistent directions; Strum Up/Down are single-note attributes. Natural means
the actual playing-technique item linked to `pt.natural`, not ordinary text.

| Location | What to enter | Expected note-start intent |
| --- | --- | --- |
| **1.1**, low C | Flautando | Flautando |
| **1.2**, D | Overpressure | Overpressure, continuing on E and F |
| **2.1**, long low C | Flautando | Flautando on the old note |
| **2.2**, D | Overpressure | Overpressure on D/E/F, even while the old C overlaps |
| **3.1**, low C | **Natural + Strum Up** | Strum up only; Natural clears the preceding pressure |
| **3.2, 3.3, 3.4**, D/E/F | **Strum Down on each of the three notes** | Strum down on each |
| **4.1**, long low C | **Natural + Strum Up** | Strum up on the old note |
| **4.2, 4.3, 4.4**, D/E/F | **Strum Down on each of the three notes** | Strum down on each new note |
| **5.1**, final C5 | Natural | No active add-ons |

Before capture, inspect the active-technique display, particularly at **1.2 and
2.2** (Overpressure), **3.2 and 4.2** (Strum down), and **5.1** (Natural only).
If it shows something else, tell us rather than editing the map or forcing it
to match the expectation. The two voices deliberately stress same-channel
note-start intent; Dorico may expose a voice-scoping limitation. That would be a
finding to investigate, not automatically a bridge or decoder bug.

There are **no duration handles or playback-length edits to make**. The written
low-note duration creates the intended overlaps. Actual MIDI NoteOff timing
still needs to be checked in the trace; notation alone is not proof of overlap.

## Capture

1. Save the project after setting up notation and the capture-only endpoint.
2. **Fully quit and reopen Dorico**; load the saved project without further edits.
3. Play once from the beginning through bar 5 and its rests (about 15 seconds).
4. **Fully quit Dorico** to close the capture. Tell the assistant **done**.

An optional MIDI export should use the new name `overlap-r2.mid` in this folder.
Do not overwrite any baseline/keyswitch captures, scores, exports or bridge maps.

## What the analysis must establish

Use [overlap-r2.expected.json](overlap-r2.expected.json) and the r2 allocation.
Desired states are hypotheses, not inferred Dorico output.

1. Validate a closed, complete trace; select the correct pass/endpoint and exclude
   idle auditions. Confirm all 17 pitches and complete initialized state.
2. **First verify the controls.** Bars 1/3 must show the requested note-start PTs,
   and their low C must release before D starts. If a control fails, diagnose
   notation/voice selection/output before attributing the failure to overlap.
3. Match real musical NoteOn/NoteOff pairs by endpoint and pitch. Distinct pitches
   remove same-pitch note-lifetime ambiguity. Do not confuse KS NoteOffs with the
   musical C4/D5/E5/F5 releases.
4. In bars 2/4, prove the low C remains active at both D and E onsets, then ends
   before F starts. If actual timing fails that precondition, the intended
   overlap/post-release check is **inconclusive**, even if all PT sets match.
5. Compare each note's state **at its onset**, preserving event order. Do not
   retroactively assign Overpressure or Strum down to the earlier C after its
   onset snapshot. This is offline analysis, not implemented Fiddle support.
6. Inspect CC112 and KS120 commands around both the new On and old musical
   release. Report any delayed Off that erases the new state, transient clear,
   or later reassertion. Correct F-note state alone is insufficient if a fresh
   On merely repaired an earlier erasure; inspect the whole interval.
7. Retain source/sample/sequence metadata, cross-list tie cautions, and physical
   KS pulse releases. As before, only KS velocity 1 logically clears a dimension.
8. Verify final Natural cleanup at bar 5. Do not assert arbitrary polyphonic
   technique independence: one shared channel cannot in general carry two
   independently changing continuous technique states at the same instant.

The outcome can validate bounded note-start transport and identify constraints
for future note-state snapshots. It does not validate downstream instrument
behavior, arbitrary mixed-voice notation, multi-endpoint routing or all Dorico
PT combinations. No runtime integration is part of this test.

## Reproduce locally

```sh
python3 experiments/orchestral-strings-bridge/overlap_score.py
python3 experiments/orchestral-strings-bridge/overlap_score.py --check
python3 -m unittest discover -s experiments/orchestral-strings-bridge -p 'test_*.py' -v
```

Tests check XML timelines, voices, planned overlap/control windows, absence of
instructional words, expectation ownership and binding to the unchanged map.
Only the host capture can establish actual note overlap and PT activation.
