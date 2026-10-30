# Score tests: does the score reach Fiddle intact?

A Fiddle playback template, with its bridge maps, should carry the score from
Dorico to the Fiddle server faithfully: which instruments play which notes,
when, for how long, how loud, and with which playing techniques. Score tests
check exactly that, at the bridge decoder's output, before any layer or
library touches the notes.

**Status (2026-10-29): all four phases built; basic-rhythm, tempo-changes, repeated-notes and articulations pass in Dorico.**
The test format and generator, eight basic tests, Fiddle's recorder and
comparison, the Score Tests window, and looking up a run by its ID.

| Test | Latest run | Result |
| --- | --- | --- |
| basic-rhythm | `basic-rhythm-20261029-180214` | Pass: 18 of 18. Onsets +0.0 to +0.6 ms, durations −0.8 to −0.2 ms, every mf note CC1 68 (level 0.5). |
| tempo-changes | `tempo-changes-20261029-180649` | Pass: 11 of 11. |
| repeated-notes | `repeated-notes-20261029-180823` | Pass: 12 of 12. Each repeated C5 ends 0.23–0.25 ms before the next starts: no overlap. |
| articulations | `articulations-20261029-180936` | Fail: 8 of 11. Dorico never selected a staccato base: the staccato quarter, staccato+accent and tenuto+staccato all kept Natural (CC102=2), so tenuto was lost in the last too. Staccatissimo, tenuto, accent and marcato arrive. Cause: Dorico's **Maximum duration for staccato playback technique** (`timingOptions.useMaximumDurationForStaccatoPlayingTechnique` on, `maximumDurationForStaccatoPlayingTechnique` `8`, shown as a quarter note) withheld Staccato from these quarters, so the maximum appears to be exclusive; Dorico's display shows only "Natural, Note length = medium". The bridge maps don't pin this option. |
| articulations | `articulations-20261029-185032` | 10 of 11 with the maximum staccato option off: staccato arrives alone and with accent. Tenuto + staccato arrives as Staccato only (CC102=12, not the Staccato+Tenuto switch 17); with the option on it arrived as Natural, not Tenuto, so Dorico doesn't request Tenuto for that pair. The expectation, not Fiddle, is wrong there. |
| articulations | `articulations-20261029-185829` | Pass: 11 of 11, with the maximum staccato option off and tenuto+staccato written as `<detached-legato/>` (Dorico imported separate tenuto and staccato elements as staccato alone). Dorico now sends the Staccato+Tenuto switch (CC102=17). |

The first two basic-rhythm runs failed every note on `pinned_playback`: the
Fiddle version in use pinned Strings r4, and reopening a saved test project
restores the Fiddle version it was saved with. Open each test as a new
project from the window, or reapply the template and save.

## Workflow

1. In Fiddle, open **View ▸ Windows ▸ Score Tests…** (the menu bar, or the
   **View** menu in the mixer's toolbar).
2. Press **Arm** on a test, then click its score (or **Open in Dorico** in the
   armed bar). Fiddle copies the MusicXML to
   `~/Library/Application Support/Fiddle/ScoreTests/scores/` and opens it in
   the newest installed Dorico.
3. In Dorico, play it from the start to the end. The armed bar counts the
   notes Fiddle receives. Played it twice? Only the latest playthrough counts.
4. Press **Done**. The window shows pass or fail, a line per failed note, and
   the run ID. **Copy run ID** puts it on the clipboard; **Show run record**
   reveals the file in Finder. **Cancel** disarms without a record.
5. On a failure, give Claude the run ID (for example
   `basic-rhythm-20261021-143200`). The run record holds the test, what
   Fiddle received, the raw MIDI and the comparison.

## Looking up a run

- `python3 tests/score/run_report.py <run ID>` explains a run: result, part
  bindings, bridge state, and each failed, missing and extra note with what
  was expected and what arrived. `--note N` adds that note's checks and the
  MIDI around it; `--list [test ID]` lists recent runs.
- The project skill `score-test-run` (`.claude/skills/score-test-run`) tells
  Claude how to read a run and find the cause; it starts when you give Claude
  a run ID.
- The Fiddle MCP server has the same as `fiddle_list_score_test_runs` and
  `fiddle_get_score_test_run` (read from disk; Fiddle need not be running).
- `fixtures/sample-run.json` is a real run record (written by
  `ScoreTestTest --write-fixture tests/score/fixtures/sample-run.json`) that
  the report and the MCP tools are tested against; `score_test_compare` fails
  if a record's shape no longer matches it.

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
3. In **Technique Catalog ▸ Bridge maps**, if a section offers **Use built-in
   revision**, press it: the tests expect the built-in maps (Strings r5,
   Woodwinds r3 or later), which pin Dorico's playback options.
4. With Dorico closed, open **View ▸ Chairs…** and press **Install Playback
   Template**.

### Dorico
Expected results assume no randomness, no added beat emphasis, and that every
staccato reaches Fiddle. In **Library ▸ Playback Options**, set:

- **Timing:** **Humanize the start positions of notes by** 0%, **Humanize the
  end positions of notes by** 0%, and turn off **Maximum duration for
  staccato playback technique** (on by default at a quarter note): otherwise
  Dorico plays longer staccato notes as Natural and Fiddle never sees the
  staccato. Expression maps can't override this option. In the articulations
  run Dorico withheld Staccato from quarters with the maximum at a quarter, so
  the limit appears to be exclusive.
- **Dynamics:** **Humanize written dynamic on pitched instruments by** 0%,
  **Increase dynamic of first beat in the bar on pitched instruments by** 0,
  **Increase dynamic of other beats in the bar on pitched instruments by** 0.
  Leave **Use pitch contour emphasis** off (Dorico's default).

Then press **Save as Default**, so every project created from a test's MusicXML
starts this way. This also changes the defaults of your other new projects; if
you'd rather not, set these six values in each test project instead.

To skip applying the playback template each time, set **Fiddle** as the
default playback template in **Preferences ▸ Play**; otherwise apply it to
each test project (**Play ▸ Playback Template ▸ Fiddle**).

Note durations, the dynamic curve and articulation dynamics don't need
setting: the bridge maps pin them.

## Recording and comparing (Fiddle)

`Source/Server/ScoreTest.h` holds the recorder and the comparison;
`ScoreTestUi.cpp` connects them to the Score Tests window
(`ui/src/lib/ScoreTests.svelte`).

- **Arm** (`armScoreTest` with a test ID) loads the test's expected result
  from the app bundle (`Contents/Resources/ScoreTests`, a copy of
  `generated/` made at build time) and notes each chair's endpoint and Dorico
  instrument. From then on Fiddle records every note the bridge decoder
  delivers, and every MIDI event it saw, bridge commands included.
- **Takes.** Transport stop ends a playthrough; the next note or transport
  start discards it, so **Done** compares the latest complete playthrough.
  Notes still sounding at Done have no duration and fail it.
- **Done** (`finishScoreTest`) compares, writes the run record and reports the
  result (`scoreTestState`, field `last_run`). `cancelScoreTest` disarms
  without a record; `requestScoreTests` sends the list of tests and state.

The comparison:

1. **Parts to endpoints.** Each part goes to the endpoint whose chair plays
   its Dorico instrument (a keyed variant counts); one part left with one
   endpoint left are paired.
2. **Clock.** Fiddle's host time and the score's start differ by one offset:
   the one that lines up the most notes by pitch, refined to the median of
   their differences. Absolute latency is therefore not tested.
3. **Matching.** Per part, in time order: the matching with the most
   same-pitch pairs within 1 s, then the smallest total onset error. Unmatched
   expected notes are **missing**; unmatched received notes are **extra**.
4. **Checks per note:** `onset`, `duration`, `pinned_playback` (the bridge
   pins Dorico's playback options), `level` or `level_between` (the starting
   dynamic: velocity, or the CC1 in force 10 ms after the note-on, as Fiddle
   waits for Dorico's late CC1, read through the pinned linear curve),
   `level_during` (CC1 moves the hairpin's way while the note sounds, not
   past its end; not checked on velocity-dynamics switches), `hairpin_order`
   (each note in a hairpin is no softer, or louder, than the one before) and
   `techniques`.

A run passes when every expected note arrives and passes and nothing extra
arrives.

### Run records

Each Done writes `~/Library/Application Support/Fiddle/ScoreTests/runs/<run ID>.json`,
the run ID being `<test ID>-<YYYYMMDD-HHMMSS>`. It holds: the result and
failures (one line per failed note, for example
`#3 vln bar 1 beat 4 C5/q: onset (+12 ms)`), the comparison per note with
every check, the received notes (with their CC1 lanes), every MIDI event
(`[ms, port, channel, kind, a, b]`), the chairs, each bridge's beacon and
withheld-note counts, and the expected result itself.

`score_test_compare` (`Source/Server/tests/ScoreTestTest.cpp`) plays every
test as Dorico would through the built-in bridge maps into the real decoder:
each must pass, and deliberate faults (a late note, a dropped note, a missing
staccato, a wrong dynamic, a hairpin out of order, an extra note) must be
reported on the right note.

## Local checks

```sh
python3 tests/score/generate.py --check
python3 -m unittest discover -s tests/score -p 'test_*.py'
```
