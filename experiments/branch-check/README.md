# Branch check: does a Fiddle layer play like Dorico?

**Prepared 2026-10-20; not yet run.** This checks the work on branch
`claude/gallant-hofstadter-7d1232` end to end: Fiddle should send a layer's
instrument the same MIDI that Dorico sends that instrument directly when it
plays the same library expression map. You play one short score twice and
compare:

- **Reference:** Dorico plays your library expression map straight into the
  silent **Fiddle MIDI Probe**.
- **Fiddle:** Dorico plays the Strings bridge map (r5) into Fiddle, and a
  Fiddle layer applies the same library map. Fiddle's **MIDI Capture** records
  what that layer sends.

[compare.py](compare.py) then compares every note: onset, duration, velocity,
channel, dynamics controllers, and the switch messages before it.

| Bars | What they check |
| --- | --- |
| 1 | plain notes at mf: default note duration, CC dynamics |
| 2 | staccato: duration, short-note switch and its velocity dynamics |
| 3 | tenuto: duration and its dynamic adjustment |
| 4 | slurred repeated D5: legato length without same-pitch overlap |
| 5 | f, marcato and accent: dynamic adjustments |
| 6–7 | pizz. then arco: switch changes, Off actions, switch lead |
| 8–9 | con sord. then senza sord.: add-on switch on and off |
| 10–11 | p crescendo over a held note: live dynamics |
| 12 | f then ff long notes |
| 13 | pp staccato: velocity dynamics at another level |

[branch-check.musicxml](branch-check.musicxml) is one violin, 14 bars at
quarter = 100 (about 34 s). [check_score.py](check_score.py) regenerates it
and [branch-check.expected.json](branch-check.expected.json) (37 notes).

## Before you start

1. **Run this branch's Fiddle.** From this worktree: `./fiddle.sh build`, then
   open `build/release/FiddleServer_artefacts/Release/FiddleServer.app`. Work in
   a separate test project or branch in Fiddle.
2. **MIDI Probe.** Installed as for the dynamics kit
   ([../dynamics-curve/README.md](../dynamics-curve/README.md)).
3. **Choose a library map.** Use a violin (or violin section) expression map you
   normally play with in Dorico, ideally one with pizzicato, con sordino,
   staccato and legato switches. Note its `.doricolib` file: user maps are
   under `~/Library/Application Support/Steinberg/Dorico 6/DefaultLibraryAdditions/`.
   The comparison reads it for keyswitch pitches and dynamics controllers.

## Part 1: Dorico direct (reference)

1. Import `branch-check.musicxml` as a **new** project.
2. Enter four playing techniques (Shift+P), each at beat 1: bar 6 **pizz.**,
   bar 7 **arco**, bar 8 **con sord.**, bar 9 **senza sord.**. Check
   Dorico's active-technique display shows them where expected.
3. In **Library ▸ Playback Options**, set **Humanize the start positions of
   notes by**, **Humanize the end positions of notes by** and **Humanize
   written dynamic on pitched instruments by** to 0. Leave the rest, but
   **write down**:
   - Timing: the six note durations (default, staccato, staccatissimo,
     tenuto, marcato, legato) and **Expression map switches before notes**.
   - Dynamics: **Dynamic curve power**, **Minimum/Maximum dynamic level in
     dynamic curve**, and **Increase dynamics for accents / marcato / tenuto**
     and **Decrease dynamics for ghost note**.
4. Save as `branch-check-direct`.
5. In Play mode, load **Fiddle MIDI Probe** for the violin, and in its
   **Endpoint Setup** route the violin to **channel 1** with your library map.
6. Save, quit Dorico, reopen the project, play once from bar 1 to the end,
   then **quit Dorico** so the probe closes its capture. The capture is
   `capture.jsonl` in the newest `fiddle-midi-probe-…` folder under `$TMPDIR`.

## Part 2: through Fiddle

1. In Fiddle, make a **Violin** chair with one layer: your library's instrument
   (any instrument will do for this check: only MIDI is compared) and, under
   **Expression map**, the **same library map**.
2. In **View ▸ Settings…**, group **Dorico playback options**, enter the values you
   wrote down in Part 1, step 3.
3. With Dorico closed, open **View ▸ Chairs…** and press
   **Install Playback Template** (this installs Strings r5).
4. Open `branch-check-direct`, **Save As** `branch-check-fiddle`, and apply the
   **Fiddle** playback template so the violin plays into Fiddle. Don't change
   the notation or Playback Options.
5. Play a bar and stop. In **View ▸ Technique Catalog…**, tab **Bridge maps**, the violin's
   row should report **Beacon matches** for Strings r5. If not, stop and report.
6. Open **View ▸ MIDI Capture**, select the violin layer, press **● Rec** on
   the **Emitted (FiddleServer)** side, play from bar 1 to the end in Dorico,
   press **■ Stop**, then **Export** it (for example
   `fiddle-emitted.midi-dump`).

## Part 3: compare

```sh
python3 experiments/branch-check/compare.py /path/to/capture.jsonl \
  /path/to/fiddle-emitted.midi-dump --map "/path/to/your library.doricolib" \
  --map-name "Exact map name, if the file holds several"
```

It prints one line per note, `ok` or `DIFF` with what differs, and a
summary. Tolerances: onset and duration ±3 ms; velocity and dynamics
controllers ±1 (one bridge step is about 0.13 dynamic level, so Fiddle can
land one value away where the curve is steep). Keyswitch note-offs are
ignored: Fiddle holds a keyswitch until the switch changes.

Send me the output (or both files). Differences to expect or to look at:

- **Bar 4** (slurred repeated D5): if Dorico overlaps the repeated notes, Fiddle
  ends each one 1 ms before the next instead (hosted instruments get no note
  IDs). A difference of about the overlap is expected there.
- Anything about **switch lead**, **channels** or **velocity on CC-dynamics
  switches** tests assumptions not yet confirmed in Dorico: `ticksBefore`
  precedence, 480 ticks per quarter, channel-switch numbering, and velocity
  100 when a switch has no velocity dynamics.

## Local checks

```sh
python3 experiments/branch-check/check_score.py --check
python3 -m unittest discover -s experiments/branch-check -p 'test_*.py'
```
