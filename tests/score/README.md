# Score tests: does the score reach Fiddle intact?

A Fiddle playback template, with its bridge maps, should carry the score from
Dorico to the Fiddle server faithfully: which instruments play which notes,
when, for how long, how loud, and with which playing techniques. Score tests
check exactly that, at the bridge decoder's output, before any layer or
library touches the notes.

**Status (2026-11-01): all four phases built; twelve of thirteen tests pass
in Dorico; percussion-techniques' first run passed 22 of 38 (below).** The test format
and generator, thirteen tests, Fiddle's recorder and comparison, the Score
Tests window, and looking up a run by its ID.

| Test | Latest run | Result |
| --- | --- | --- |
| basic-rhythm | `basic-rhythm-20261029-180214` | Pass: 18 of 18. Onsets +0.0 to +0.6 ms, durations −0.8 to −0.2 ms, every mf note CC1 68 (level 0.5). |
| tempo-changes | `tempo-changes-20261029-180649` | Pass: 11 of 11. |
| repeated-notes | `repeated-notes-20261029-180823` | Pass: 12 of 12. Each repeated C5 ends 0.23–0.25 ms before the next starts: no overlap. |
| articulations | `articulations-20261029-180936` | Fail: 8 of 11. Dorico never selected a staccato base: the staccato quarter, staccato+accent and tenuto+staccato all kept Natural (CC102=2), so tenuto was lost in the last too. Staccatissimo, tenuto, accent and marcato arrive. Cause: Dorico's **Maximum duration for staccato playback technique** (`timingOptions.useMaximumDurationForStaccatoPlayingTechnique` on, `maximumDurationForStaccatoPlayingTechnique` `8`, shown as a quarter note) withheld Staccato from these quarters, so the maximum appears to be exclusive; Dorico's display shows only "Natural, Note length = medium". The bridge maps don't pin this option. |
| articulations | `articulations-20261029-185032` | 10 of 11 with the maximum staccato option off: staccato arrives alone and with accent. Tenuto + staccato arrives as Staccato only (CC102=12, not the Staccato+Tenuto switch 17); with the option on it arrived as Natural, not Tenuto, so Dorico doesn't request Tenuto for that pair. The expectation, not Fiddle, is wrong there. |
| articulations | `articulations-20261029-185829` | Pass: 11 of 11, with the maximum staccato option off and tenuto+staccato written as `<detached-legato/>` (Dorico imported separate tenuto and staccato elements as staccato alone). Dorico now sends the Staccato+Tenuto switch (CC102=17). |
| dynamics-ladder | `dynamics-ladder-20261029-190005` | Pass: 24 of 24. CC1 ppp 40, pp 48, p 56, mp 60, mf 68, f 72, ff 80, fff 88: exactly Dorico's levels on the pinned linear −8..8 curve (largest error 0.05 level, from 7-bit rounding). |
| slurs | `slurs-20261029-190133` | Pass: 16 of 16. Slurred notes carry `pt.legato`, unslurred don't, confirming that expectation. The last note of each slur (three of them, currently unchecked) also carries `pt.legato`. Slurred notes play their written length (legato pinned to 100%): no overlap. |
| hairpins | `hairpins-20261029-190413` | Pass: 13 of 13. Dorico moves the level in a straight line over time: a crescendo p→f over a held 4.8 s note steps CC1 56→72 one value every 300 ms; under a crescendo over four quarters the notes start at levels −1, −0.5, 0, +0.5 and each rises 0.5 while it sounds; the diminuendo mirrors it. |
| two-instruments | `two-instruments-20261029-190605` | Pass: 16 of 16. Violin through Strings r5 (port 2 channel 15) and flute through Woodwinds r3 (port 1 channel 9), each bound by its chair's instrument. |
| horn | `horn-20261030-122642` | Pass: 18 of 18. Horn in F through Brass r3 (port 2 channel 5). Dorico imported the written-pitch MusicXML with its `<transpose>` and sent sounding pitches (first note F3, written C4). Onsets +0 ms, durations −0.2 ms; articulations, legato under the slur, and tenuto+staccato (Staccato+Tenuto switch) arrive as on strings; levels exact (mf CC1 68, f 72, p 56) and both hairpins move CC1 one value at a time in a straight line. |
| pitched-percussion | `pitched-percussion-20261030-124022` | Pass: 33 of 33. Timpani (port 2 channel 13) and marimba (port 3 channel 6, Dorico's default `marimba.4.3oct`) both through Pitched percussion r2, each bound by its chair's instrument. Onsets +0 ms, durations −0.2 ms. The crescendo over eight timpani eighths starts each note on the hairpin's line (CC1 56, 58, … 70) and rises two values while it sounds. Staccato notes take the map's velocity-dynamics switch (velocity 60 at mp, 72 at f); everything else arrives on CC1. Slurred marimba notes carry legato, the slur's last included. |
| snare-drum | `snare-drum-20261031-144228` | Fail: 26 of 28, the two tremolos missing. Dorico sent them on pitch 73, the retired pitch-code map's tremolo entry: Dorico had run since before that map was replaced and never loaded the new library (it reads `DefaultLibraryAdditions` only at startup). The hits all passed. |
| snare-drum | `snare-drum-20261031-154157` | Pass: 28 of 28, through Unpitched percussion r2 (port 3 channel 7), bound by the chair's instrument. Every note on the map's one pitch (36). Hits carry their dynamic as velocity (mf 68, ladder ppp 40 … fff 88) with accent, marcato, staccato and tenuto arriving; the whole-note tremolo is one 2.4 s note on the tremolo base (CC102=30, VeryShort bucket) with CC1 56→72 under the hairpin, and the half-note tremolo likewise. (This run still imported through Dorico's five-line kit wrapper, which contains a snare, see below.) |
| snare-drum | `snare-drum-20261031-172210` | Pass: 28 of 28 on the one-line staff: imported as the single instrument (no kit, two plug-in slots), same results as the run above. |
| cymbals-and-triangle | `cymbals-and-triangle-20261031-160247` | Fail: 0 of 16, 5 extra. Dorico's import put the bass drum part into its marching bass drums kit (part name "Bass Drum" matches a drum of that kit), the template could not route the kit, and Dorico loaded nine Fiddle plug-in instances; the cymbal's five notes arrived on the snare chair's channel, nothing else arrived. |
| cymbals-and-triangle | `cymbals-and-triangle-20261031-165626` | Fail: 0 of 16, 5 extra, with the bass drum part renamed so every part fell to Dorico's Drum Set: the same five cymbal notes on the snare chair's channel. Dorico routes the one-instrument Drum Set wrapper through its snare component; the fix is the one-line staff (above). |
| percussion-techniques | 22 of 38 (`percussion-techniques-20261101-144822`): every snare and cymbal mark but side stick | Strokes and modifiers the other percussion tests don't reach, each from Dorico's own items: snare noteheads set by hand (side stick, rim shot, cross stick), On Rim, Stick Shot, buzz roll, Snares Off/On; hi-hat Open, Closed, Half-open; cymbal Choke, l.v., Damp; a tambourine trill; conga Open and Muffled. The notehead techniques answer the open question in `experiments/unpitched-percussion-bridge/results-01.md`: whether a technique Dorico takes from the notehead reaches the expression map as a text mark does. Needs three new chairs (Hi-hat, Tambourine, Conga (Quinto)). See "Hand-applied marks". First run: rim shot and cross stick (noteheads set by hand) arrive, so a notehead's technique does reach the expression map; so do On Rim, Stick Shot, buzz roll, Snares Off/On, Choke, l.v. (written length kept) and Damp. Side stick failed: on a one-line staff the import makes the x head a notehead-set override (`noteheadset.Xnoteheads`, no technique), unlike the probe's five-line staff, so side stick is now a notehead set by hand like the other two. Hi-hat, tambourine and conga sent nothing: the hi-hat chair was the marching hi-hat, and no chair had a bridge map (the template wasn't reinstalled after adding them). |
| cymbals-and-triangle | `cymbals-and-triangle-20261031-171854` | Pass: 16 of 16 on one-line staves: Dorico imported three single instruments (no kit, two plug-in slots) and routed each to its chair (suspended cymbal port 3 channel 8, triangle 9, bass drum 10). The cymbal's whole-note roll under p < f is one 2.4 s note on the tremolo base with CC1 56→72 one value every 150 ms; the bass drum's ff roll one note at CC1 80; hits by velocity (triangle mf 68, cymbal f 72), accents arriving on CC104. Switches 12.5 ms before each note, Off 25 ms before. |

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
3. If the armed bar lists marks to add, add them in Dorico first (see
   "Hand-applied marks"). Then play the score from the start to the end. The
   armed bar counts the notes Fiddle receives. Played it twice? Only the
   latest playthrough counts.
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

`instrument` is one of `violin`, `viola`, `flute`, `horn`, `timpani`, `marimba`,
`snare`, `bass-drum`, `suspended-cymbal`, `triangle`, `hi-hat`, `tambourine`,
`conga` (see `INSTRUMENTS` in `scoretest.py`: each maps to Dorico's
instrument ID and MusicXML sound). An optional `name` renames the part (its
part and score-instrument names in the MusicXML). Dorico's import picks the
instrument by sound ID and then by name, so the default names are Dorico's
own: "Hi-hat" (not the marching hi-hat, which shares `metal.hi-hat`) and
"Conga (Quinto)", the high conga (all five congas share `drum.conga` and the
name "Conga"; if Dorico imports another one, change it in Setup to Conga
(Quinto) before playing). Unpitched parts are
written on a **one-line** percussion staff, which Dorico imports as the single
instrument. On a five-line staff Dorico wraps the part in a percussion kit
(its Drum Set, or a stock kit with a component named like the part: "Bass
Drum" selects the eight-drum marching bass drums kit), the playback template
routes the kit through its snare component, and every other instrument's
notes land on the snare chair or nowhere; the snare-drum test passed in that
state only because the kit contained a snare.
Pitches in `music` are always sounding pitches. For a transposing instrument
(horn, Horn in F) the generator writes the MusicXML at written pitch with a
`<transpose>` element and the written key, and the expected results stay at
sounding pitch: Dorico sends sounding pitch.

`music` is a space-separated list of tokens:

| Token | Meaning |
| --- | --- |
| `C5/q` | a note: pitch (`C#5`, `Bb4`, octave 4 starts at middle C) / length `w h q e s`, dotted with `.` (`q.`) |
| `C5/q+stacc+acc` | articulations: `stacc`, `staccmo` (staccatissimo), `ten`, `acc`, `marc` (marcato) |
| `x/q+trem` | on an unpitched instrument, a hit (`x`, no pitch) with ornaments `trem` (three-stroke tremolo, `pt.tremolo`), `trill` (`<trill-mark/>`, `pt.trill`) or `buzz` (buzz roll, `pt.buzzRoll`; Dorico's import drops it, so it is also a hand-applied mark), and noteheads `xhead`, `ghost` (written in the MusicXML, no technique expected) |
| `x/q+rim` | a hand-applied percussion mark (below): `sidestick`, `rimshot`, `crossstick`, `rim`, `stickshot`, `open`, `closed`, `halfopen`, `choke`, `damp`, `muffled`, `lv`, `snaresoff`, `snareson` |
| `C5/h~` | tied to the next note (same pitch); a tie chain sounds once |
| `r/q` | a rest |
| `!mf` | a dynamic, from `pppppp` to `ffffff`; a part must start with one |
| `<` `>` … `]` | crescendo / diminuendo start, and its end; a dynamic after `]` is the hairpin's target |
| `(` … `)` | a slur around notes |
| `T=120` | a tempo change (quarter notes per minute), in the first part only |
| `|` | a bar line (checked against the time signature) |

Notes may not cross bar lines: tie them.

### Hand-applied marks

MusicXML can't carry most unpitched techniques: Dorico's import keeps hits,
dynamics, accents, marcato, staccato, tenuto, tremolos, trills and the x
notehead (as a head only, with no technique), and drops the rest. So a spec names them as marks (`PERCUSSION` in
`scoretest.py`) and the operator adds each in Dorico before playing. The
generator puts an "add: …" cue above each such note in the MusicXML, and
writes the list of steps into the expected result and `index.json`. The
Score Tests window shows that list while the test is armed, with the bar,
beat, part and the exact Dorico item, for example:

    Bar 4 beat 1, Snare Drum: Playing Techniques panel ▸ Unpitched Percussion ▸ Snares Off
    Bar 1 beat 3, Snare Drum: Option-Shift-Up/Down on the note until its head is slashed (Rim shot)

Use exactly those items. Typing a technique's name into the Shift-P popover
can make a new user playing technique instead, which plays back as a user
technique no bridge map knows: in the probe, a ghost note added that way
sent nothing (results-01.md, finding 4). Side stick, rim shot and cross stick
are the snare's notehead techniques (its notehead set: x is side stick,
slashed is rim shot, circled is cross stick), set by hand because the
import gives an x head no technique on a one-line staff. They test whether a notehead-assigned
technique reaches the expression map. The cues stay in the score as staff
text; they don't affect playback. Save the edited project if you want to
re-run without marking it again, but open it after reapplying the Fiddle
template (see the note on saved projects above).

Dead stroke, Rods and the ghost note have no built-in Dorico item, so no test
marks them. The probe carried all three on the same conduits r2 uses (CC108,
CC105, KS4).

An unpitched part's expected pitch is the unpitched bridge's percussion-map
pitch (`experiments/unpitched-percussion-bridge/unpitched-percussion-r2/allocation.json`),
36 for every note: Dorico plays the pitch of the entry it picks, and the map
puts Natural and every roll kind on one pitch. The techniques arrive through
the expression map as for any instrument; hits carry their dynamic as
velocity, rolls on CC1 (see `experiments/unpitched-percussion-bridge/results-01.md`).

## What a test expects

For each sounding note, in time order across parts: its part and Dorico
instrument, pitch, onset and duration in milliseconds (from the tempo map,
relative to the start of the score), dynamic, and playing techniques.

- **Duration** is the written length: the bridge maps pin Dorico's note
  durations to 100%.
- **Dynamic** is Dorico's level (p −1, pp −2 …, mp −0.5, mf +0.5, f 1 …; from
  [the dynamics measurement](../../experiments/dynamics-curve/results-01.md)).
  The bridge pins a linear curve and no articulation adjustments, so the level
  is exact for a written dynamic. A hairpin moves the level in a straight line
  over time from its start to its end, so a note inside one starts at that
  line's level (`level`, also bounded by `between`), levels move one way, and a
  note held under a hairpin keeps changing and ends where the line is when it
  ends (`during.end_level`).
- **Techniques**: `includes` must arrive, `excludes` must not. Articulations
  map to `pt.staccato`, `pt.staccatissimo`, `pt.tenuto`, `pt.accent` and
  `pt.marcato`; slurred notes include `pt.legato`, the slur's last note too;
  unslurred notes exclude it.
- **Percussion techniques**: a note's marks are included; every other
  percussion technique the harness knows is excluded, so a switch left on
  past its note fails the next one. Snares Off and Snares On are Dorico
  Directions and hold until the other one; every other mark is an Attribute
  and applies to its note only (Dorico's `playingTechniqueDefinitions.xml`).
  An instrument's natural equivalent (Closed on the hi-hat, Open on the
  triangle, from `instruments.xml`) is never excluded: a plain note may carry it.
  A notehead from the MusicXML (`xhead`, `ghost`) expects no technique: the
  probe's five-line snare imported its x head as side stick, but on the
  one-line staff the tests use the import keeps only the head
  (`percussion-techniques-20261101-144822`).
- **Tolerances** (per test, default): onset and duration ±5 ms, level ±0.2.

Expectations about Dorico's own behaviour are hypotheses until a run confirms
them. Legato on slurred notes, the last included, is confirmed
(`slurs-20261029-190133`), and so is a hairpin's path: a straight line in
level from one dynamic to the next over time (`hairpins-20261029-190413`).
The tests check both exactly.

percussion-techniques has first guesses that its runs may correct: that
a plain hi-hat note doesn't need Closed, that a trill on the tambourine arrives as `pt.trill` (not a
trill interval), and that Dorico imports Conga (Quinto) by its name.

## One-time setup

### Fiddle
1. Use a dedicated Fiddle project (or branch) for testing.
2. Create one chair per instrument the tests use (`instruments` in
   `generated/index.json`; today **Violin**, **Flute**, **Horn in F**,
   **Timpani**, **Marimba**, **Snare Drum**, **Bass Drum**, **Suspended
   Cymbal**, **Triangle**, **Hi-hat**, **Tambourine** and **Conga
   (Quinto)**). Each needs a layer
   (any instrument) so it is routed; what layers play is not tested.
3. In **Technique Catalog ▸ Bridge maps**, if a section offers **Use built-in
   revision**, press it: the tests expect the built-in maps (Strings r5,
   Woodwinds r3, Brass r3, Pitched percussion r2 or later), which pin Dorico's playback options.
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
  **Increase dynamic of other beats in the bar on pitched instruments by** 0,
  and the same three options **on unpitched instruments** (the unpitched
  tests play hits by velocity, which these would alter).
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
   past the hairpin's end, and ends at `end_level`; not checked on
   velocity-dynamics switches), `hairpin_order`
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
