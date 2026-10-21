---
name: score-test-run
description: Explain a Fiddle score test run and find the cause of its failures. Use when the user gives a score test run ID (like basic-rhythm-20261021-143200, from Fiddle's Score Tests window) or asks why a score test failed.
---

# Explaining a score test run

A score test plays a generated score in Dorico and checks that Fiddle's bridge
decoder receives every note as written (tests/score/README.md explains the
format, the checks and run records). The user's run ID names a run record:
`~/Library/Application Support/Fiddle/ScoreTests/runs/<run ID>.json`.

## 1. Read the run

From the repository root:

```bash
python3 tests/score/run_report.py <run ID>
```

It prints the result, how parts were bound to endpoints, each bridge's state
at Done, and every failed, missing and extra note with what was expected and
what arrived. Then look closer at a note:

```bash
python3 tests/score/run_report.py <run ID> --note 3 --window 50
```

which adds all of that note's checks and the MIDI Fiddle saw around it
(bridge commands included), in ms from the score's start. `--list [test ID]`
lists recent runs. If the Fiddle MCP server is connected,
`fiddle_get_score_test_run` (with `note` and `midi_window_ms`) and
`fiddle_list_score_test_runs` return the same as JSON.

Don't read the whole record into context: it holds every MIDI event. If you
need something the report doesn't show, query it with a short Python snippet.

## 2. Find the cause

Look for the pattern first: one note, every note, every note of one kind, or
everything after a point.

| Symptom | Look at |
| --- | --- |
| Every note missing, or a part bound to nothing | No chair plays the part's instrument, the template isn't applied in Dorico, or the bridge withheld notes: see **Bridges at Done** (identity check `mismatched` = Dorico plays another bridge revision; withheld "base articulation not received" = Dorico never sent the bridge's Init; start playback again). |
| Notes missing after a point | Playback stopped early, or a later take was cut short (only the latest playthrough counts). |
| Extra notes | Auditioned notes while armed (clicking in Dorico), or another instrument on the bound endpoint. |
| Onsets drifting through the test | Tempo: Dorico's tempo differs from the spec (tempo-changes test), or swing/humanize is on. |
| Onsets scattered by a few ms | Humanize start positions isn't 0 in Dorico's Playback Options. |
| Durations wrong by a fixed percentage | Dorico's note-duration options aren't pinned: `pinned_playback` failing means the endpoint's bridge isn't a pinned revision (Strings r5, Woodwinds r3, Brass r3, Pitched percussion r2 or later). |
| Durations short only before a same-pitch note | Dorico's truncation of abutting same-pitch notes. |
| `level` off on every note | The dynamic curve isn't the bridge's pinned linear curve, or beat-stress/humanize dynamics are on. |
| `level` off only on accents, marcato, tenuto | Articulation dynamic deltas aren't pinned to 0. |
| `level` off only at a new dynamic | CC1 arrived later than 10 ms after the note-on: check the MIDI around the note. |
| `level_during` / `hairpin_order` | Hairpin playback; the expectation itself is a hypothesis (below). |
| `techniques` missing or unexpected | The bridge map's switches for that technique (experiments/*-bridge generators), or what Dorico sends for that notation: the MIDI shows the bridge commands before the note. |

Code: recorder and comparison in `Source/Server/ScoreTest.h`; the decoder in
`Source/Server/BridgeMapDocument.h` (`BridgeDecoderState`) and
`Source/Server/NoteStreamTracker.h`; expected results from
`tests/score/scoretest.py`.

## 3. Decide what is wrong

A failure means one of three things, and the report alone doesn't say which:

1. **Fiddle or a bridge map is wrong**: fix it, with a test.
2. **The expectation is wrong**: some expectations are hypotheses about
   Dorico, not yet confirmed (notably legato on slurred notes, and the exact
   path of a hairpin). If Dorico's behaviour is reasonable and consistent,
   change `tests/score/scoretest.py` or the spec, run
   `python3 tests/score/generate.py`, and say so in the README.
3. **The setup is wrong** (Dorico Playback Options, chairs, template): tell the
   user exactly what to change, citing the README's one-time setup.

Tell the user which it is and why, with the evidence (the note, the expected
and received values, the MIDI). Ask before changing an expectation.
