# Dynamics measurement kit

**Captured 2026-10-20: see [results-01.md](results-01.md).** Fiddle needs Dorico's exact
dynamic-to-MIDI mapping before it can apply each layer's own dynamic curve and
articulation adjustments (see "What reaches instruments" in
[bridge-map-authoring.md](../../docs/bridge-map-authoring.md)). This kit
measures it with the capture-only **Fiddle MIDI Probe**. It answers:

1. How a dynamic level becomes CC1 and note velocity: the curve's form, its
   output range, and whether velocity follows the same curve.
2. The level Dorico gives each written dynamic, pppppp to ffffff.
3. Whether **Minimum/Maximum dynamic level in dynamic curve** rescale the curve
   or clip it.
4. The size and placement of the accent, marcato and tenuto adjustments
   (attack velocity, CC1 at the note start, or both).
5. How much 7-bit resolution survives each curve, which decides the curve the
   bridge maps should pin.
6. The exact text Dorico writes for each playback override in a map, so the
   bridge maps can pin them.

| File | Contents |
| --- | --- |
| [Fiddle-Dynamics-Probe.doricolib](Fiddle-Dynamics-Probe.doricolib) | Two measurement expression maps: **…CC1** (dynamics on CC1, velocity attack) and **…velocity** (dynamics on velocity). Natural only: no switches, conditions, Init or overrides. |
| [dynamics-probe.musicxml](dynamics-probe.musicxml) | Two violins, identical, 20 bars of 4/4 at quarter = 120 (40 s). |
| [dynamics-probe.expected.json](dynamics-probe.expected.json) | The 52 sounding notes per part and the four passes' settings. |
| [analyze.py](analyze.py) | Reads the closed capture and reports the results. |
| [kit.py](kit.py) | Regenerates the three files above (`--check` verifies them). |

The score: bars 1–14 step through pppppp … ffffff, one dynamic per bar (a half
note and two quarters). Bars 15–16 are at mf: plain, accent, plain, marcato,
plain, tenuto, plain, plain, each articulated note repeating the pitch of the
plain note before it. Bars 17–18 hold one F5 under a crescendo from p, reaching
f at bar 19. Bar 20 is a rest.

## Before the session

1. Build the probe: `cmake --build build/release --target FiddleMidiProbe`.
   The bundle is `build/release/FiddleMidiProbe_artefacts/VST3/Fiddle MIDI Probe.vst3`.
   With Dorico closed, copy it into your VST3 plug-in folder if it isn't there
   already (the build never installs it). Leave the normal Fiddle plug-in alone.
2. Open Dorico. In **Library ▸ Expression Maps**, **Import**
   `Fiddle-Dynamics-Probe.doricolib`. Both maps appear.

## Set up the test project

1. Import `dynamics-probe.musicxml` as a **new** project. Check that it shows
   two violins, every dynamic in bars 1–14, an accent, a marcato and a tenuto
   in bars 15–16, and the crescendo in bars 17–18.
2. In Play mode, load **Fiddle MIDI Probe** as the instrument for both
   players. In **Endpoint Setup** for the probe, route **Violin 1** to
   **channel 1** with map **Fiddle Dynamics Probe - CC1**, and **Violin 2** to
   **channel 2** with map **Fiddle Dynamics Probe - velocity**. Leave independent
   voice playback off.
3. In **Library ▸ Playback Options**, set the baseline for every pass. These
   remove Dorico's random and phrase-dependent changes so only the curve is
   measured:
   - Timing: **Humanize the start positions of notes by** 0%, **Humanize the
     end positions of notes by** 0%.
   - Dynamics: **Humanize written dynamic on pitched instruments by** 0%,
     **Increase dynamic of first beat in the bar on pitched instruments by** 0,
     **Increase dynamic of other beats in the bar on pitched instruments by** 0,
     **Use pitch contour emphasis** off, **Use polyphonic instrument voice
     balancing** off, **Reset dynamic level for short consecutive gradual
     dynamics of same direction** off.
4. Save the project, quit Dorico completely, and reopen the project.

## Capture: four passes in one session

Play the whole score four times, from bar 1 to the end, changing only these
Playback Options before each pass. Let each pass reach the final barline
before stopping; a pass stopped early is ignored by the analysis.

| Pass | Dynamic curve power | Minimum dynamic level | Maximum dynamic level | Accents | Marcato | Tenuto |
| --- | --- | --- | --- | --- | --- | --- |
| A | 2.50 | -6 | 6 | 0.50 | 0.80 | 0.00 |
| B | 1.00 | -6 | 6 | 0.50 | 0.80 | 0.00 |
| C | 1.00 | -3 | 3 | 0.50 | 0.80 | 0.00 |
| D | 2.50 | -6 | 6 | 1.00 | 2.00 | 1.50 |

The last three columns are **Increase dynamics for accents / marcato / tenuto
by**. Silence is expected. After pass D, **quit Dorico** so the probe closes its
capture file.

The capture is `capture.jsonl` in the newest `fiddle-midi-probe-…` folder
under your temporary folder (`echo $TMPDIR` in Terminal). Dorico's plug-in scan
can leave other, empty captures; use the one that contains the passes.

## Export the override formats (no capture)

1. In **Library ▸ Expression Maps**, duplicate **Fiddle Dynamics Probe - CC1**.
2. In the copy's **Playback Options Overrides**, turn on and set:
   **Dynamic curve power** 1.75, **Minimum dynamic level in dynamic curve** -5,
   **Maximum dynamic level in dynamic curve** 5, **Increase dynamics for
   accents by** 0.60, **marcato by** 0.90, **tenuto by** 0.30, **Decrease
   dynamics for ghost note by** 2.25, **Humanize written dynamic on pitched
   instruments by** 7%, **Increase dynamic of first beat in the bar on pitched
   instruments by** 0.35, **Increase dynamic of other beats in the bar on
   pitched instruments by** 0.15, **Use pitch contour emphasis** on.
3. **Export** just that map as `dynamics-overrides-export.doricolib`.

The unusual values make each option recognisable in the exported file.

## Analysis

From the repository root:

```sh
python3 experiments/dynamics-curve/analyze.py /path/to/capture.jsonl --json dynamics-results.json
python3 experiments/dynamics-curve/analyze.py --overrides /path/to/dynamics-overrides-export.doricolib
```

The first prints, for each pass, the CC1 and velocity Dorico sent for every
dynamic (raw normalized values, before MIDI rounding), the crescendo's CC1
values, the level each articulation added, and the smallest 7-bit gap between
adjacent dynamics. It then derives each dynamic's level from pass B (assuming
power 1 is linear) and reports which curve form, `x^power` or `x^(1/power)`
with `x` the level's position between the minimum and maximum, explains the
other passes, with its largest error. A large error means neither form fits;
the raw table is then the evidence to work from. If the capture holds extra
complete passes, name the four with `--select 2,3,4,5`.

The second prints each override's option name and value exactly as Dorico
wrote it.

## Local checks

```sh
python3 experiments/dynamics-curve/kit.py --check
python3 -m unittest discover -s experiments/dynamics-curve -p 'test_*.py'
```
