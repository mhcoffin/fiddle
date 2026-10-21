# Score tests: does the score reach Fiddle intact?

A Fiddle playback template, with its bridge maps, should carry the score from
Dorico to the Fiddle server faithfully: which instruments play which notes,
when, for how long, how loud, and with which playing techniques. Score tests
check exactly that, at the bridge decoder's output, before any layer or
library touches the notes.

**Status (2026-10-21): phase 1.** The test format, the generator and eight
basic tests exist. Phases 2–4 (recording and comparing in Fiddle, the Score
tests pane, and looking up a run by its ID) are not built yet.

## Workflow (once phases 2–4 exist)

1. In Fiddle's **Score tests** pane, press **Arm** on a test.
2. In Dorico, open the test's MusicXML (the pane links to it) and play it from
   the start to the end.
3. Press **Done**. Fiddle compares every note it received with the test's
   expected result and shows pass or fail per note and per property.
4. On a failure, give Claude the run ID shown in the pane (for example
   `basic-rhythm-20261021-1432`). The run record holds the test, what Fiddle
   received, the raw MIDI and the comparison.

## Files

| Path | Contents |
| --- | --- |
| `specs/<id>.json` | One test: instruments and their music in the notation below. The single source of truth. |
| `generated/<id>.musicxml` | The score to open in Dorico. |
| `generated/<id>.expected.json` | Every sounding note as the bridge decoder should receive it. |
| `generated/index.json` | The list of tests, for Fiddle's pane. |
| `scoretest.py`, `generate.py` | The notation parser, MusicXML writer and expected-result builder. |

After editing or adding a spec, run `python3 tests/score/generate.py`.
`ctest -R score_test_specs` (part of `scripts/test.sh`) fails if the generated
files are stale.

## Spec format

```json
{
  "id": "two-instruments",
  "title": "Two instruments",
  "description": "What the test checks.",
  "tempo": 100,
  "time": "4/4",
  "parts": [
    {"id": "vln", "instrument": "violin", "music": "!mf C5/h E5/h | G5/w"},
    {"id": "fl",  "instrument": "flute",  "music": "!p E5/q F5/q G5/q A5/q | B5/w"}
  ]
}
```

`instrument` is one of `violin`, `viola`, `flute` (see `INSTRUMENTS` in
`scoretest.py`: each maps to Dorico's instrument ID and MusicXML sound).
Horn waits until Dorico's import of transposing instruments is checked.

`music` is a space-separated list of tokens:

| Token | Meaning |
| --- | --- |
| `C5/q` | a note: pitch (`C#5`, `Bb4`, octave 4 starts at middle C) / length `w h q e s`, dotted with `.` (`q.`) |
| `C5/q+stacc+acc` | articulations: `stacc`, `staccmo` (staccatissimo), `ten`, `acc`, `marc` (marcato) |
| `C5/h~` | tied to the next note (same pitch); a tie chain sounds once |
| `r/q` | a rest |
| `!mf` | a dynamic, from `pppppp` to `ffffff`; a part must start with one |
| `<` `>` … `]` | crescendo / diminuendo start, and its end; a dynamic after `]` is the hairpin's target |
| `(` … `)` | a slur around notes |
| `T=120` | a tempo change (quarter notes per minute), in the first part only |
| `|` | a bar line (checked against the time signature) |

Notes may not cross bar lines: tie them.

## What a test expects

For each sounding note, in time order across parts: its part and Dorico
instrument, pitch, onset and duration in milliseconds (from the tempo map,
relative to the start of the score), dynamic, and playing techniques.

- **Duration** is the written length: the bridge maps pin Dorico's note
  durations to 100%.
- **Dynamic** is Dorico's level (p −1, pp −2 …, mp −0.5, mf +0.5, f 1 …; from
  [the dynamics measurement](../../experiments/dynamics-curve/results-01.md)).
  The bridge pins a linear curve and no articulation adjustments, so the level
  is exact for a written dynamic. Inside a hairpin a note's level lies between
  the hairpin's ends (`between`), and levels move one way; a note held under a
  hairpin keeps changing (`during`).
- **Techniques**: `includes` must arrive, `excludes` must not. Articulations
  map to `pt.staccato`, `pt.staccatissimo`, `pt.tenuto`, `pt.accent` and
  `pt.marcato`; slurred notes include `pt.legato`, except a slur's last note,
  which is not checked; unslurred notes exclude it.
- **Tolerances** (per test, default): onset and duration ±5 ms, level ±0.2.

Expectations about Dorico's own behaviour are hypotheses until a run confirms
them, notably legato on slurred notes and the exact path of a hairpin.

## One-time setup

### Fiddle
1. Use a dedicated Fiddle project (or branch) for testing.
2. Create one chair per instrument the tests use (`instruments` in
   `generated/index.json`; today **Violin** and **Flute**). Each needs a layer
   (any instrument) so it is routed; what layers play is not tested.
3. With Dorico closed, open **View ▸ Chairs…** and press **Install Playback
   Template**.

### Dorico
Expected results assume no randomness and no added beat emphasis. In
**Library ▸ Playback Options**, set:

- **Timing:** **Humanize the start positions of notes by** 0%, **Humanize the
  end positions of notes by** 0%.
- **Dynamics:** **Humanize written dynamic on pitched instruments by** 0%,
  **Increase dynamic of first beat in the bar on pitched instruments by** 0,
  **Increase dynamic of other beats in the bar on pitched instruments by** 0.
  Leave **Use pitch contour emphasis** off (Dorico's default).

Then press **Save as Default**, so every project created from a test's MusicXML
starts this way. This also changes the defaults of your other new projects; if
you'd rather not, set these five values in each test project instead.

To skip applying the playback template each time, set **Fiddle** as the
default playback template in **Preferences ▸ Play**; otherwise apply it to
each test project (**Play ▸ Playback Template ▸ Fiddle**).

Note durations, the dynamic curve and articulation dynamics don't need
setting: the bridge maps pin them.

## Local checks

```sh
python3 tests/score/generate.py --check
python3 -m unittest discover -s tests/score -p 'test_*.py'
```
