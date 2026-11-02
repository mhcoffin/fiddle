# Switch order: has Dorico fixed "On for the next note, then Off for the previous one"?

**2026-10-23: case A in the Dorico beta is STILL PRESENT** ([results-01](results-01.md)).
Case B not yet run.

## The issue

With the Strings r2 bridge map, Flautando (an add-on: On writes CC112=1, Off
writes CC112=0) was written over bars 1–2 of a violin part. When the note
length changed at bar 2, Dorico changed the base switch (CC102 2 → 4 for a
longer note) and restated Flautando's On about 16 ms before the note. Then,
at the note itself, it sent the **previous** combination's Off. The Off
arrived after the On, so the technique ended although the score still had it:

| Host time (ms from the first note) | Message | Meaning |
| ---: | --- | --- |
| 2983.99 | CC102 = 4 | new base (longer note) |
| 2983.99 | CC112 = 1 | Flautando On, restated for bar 2 |
| 3000.00 | CC112 = 0 | Flautando Off for bar 1's combination, 16.01 ms later |
| 3000.00 | note on | bar 2 starts without Flautando |

Instead of Flautando holding through bars 1–2, every bar-2 note started without
it. This reproduced at identical sample positions in two captures
([shared-overlap-results-01](../orchestral-strings-bridge/shared-overlap-results-01.md),
[-02](../orchestral-strings-bridge/shared-overlap-results-02.md)) and with a map
trimmed to the two bases involved
([two-base-results-01](../orchestral-strings-bridge/two-base-results-01.md)).
With a single base (no base change) it disappeared
([single-base-results-01](../orchestral-strings-bridge/single-base-results-01.md)).
[docs/orchestral-bridge-maps.md](../../docs/orchestral-bridge-maps.md) has the
history. Fiddle's bridge decoder defends against it (typed Offs; an Off
within 30 ms of its own On is ignored), so this check reads Dorico's raw output
from the silent **Fiddle MIDI Probe**, not Fiddle.

## The check

Two cases, each played once into the Probe, then checked with
[check_switch_order.py](check_switch_order.py):

- **A. The reported case:** the original reproduction project, unchanged.
  The current Dorico gave the sequence above, so this case alone says whether
  the beta changed it.
- **B. One voice:** a new one-voice score whose note lengths change four times
  under Flautando (quarters, then a dotted half, and back), the case that
  matters for Fiddle. It was never captured with the current Dorico.

The script reports every base change while Flautando should be active, every
Off in that span, and Flautando's value at each note-on:

- **FIXED**: base changes happened under Flautando and no Off cut it. An Off
  followed by an On before the next note (old Off, then new On) is the correct
  order and is listed as harmless.
- **STILL PRESENT**: an Off was left in force, or a note started without
  Flautando. It prints the Off and how long after the On it came.
- **INCONCLUSIVE**: no base change happened, so the trigger never occurred.

The Off that ends Flautando just before the final **nat.** notes is expected
and not counted.

## Before you start

- The **Fiddle MIDI Probe** plug-in is installed (as for the
  [dynamics kit](../dynamics-curve/README.md)).
- The Strings r2 inspection map is in the reproduction project (case A). For
  case B, import it into Dorico's library if it isn't there already:
  [Fiddle-Orchestral-Strings-r2.doricolib](../orchestral-strings-bridge/Fiddle-Orchestral-Strings-r2.doricolib)
  ("Fiddle Bridge - Orchestral Strings r2 - INSPECTION ONLY").

## A. The reported case

1. Copy the project so the original stays as it was (the beta may convert it):

   ```bash
   cp "experiments/orchestral-strings-bridge/Strings bridge r2- shared PT overlap.dorico" ~/Desktop/switch-order-A.dorico
   ```

2. Open `~/Desktop/switch-order-A.dorico` in the Dorico beta. In Play mode,
   check that the violin plays into **Fiddle MIDI Probe**, Port 1 / Channel 1,
   with **Fiddle Bridge - Orchestral Strings r2 - INSPECTION ONLY**.
3. Check the active playback techniques: Flautando for both voices through
   bars 1–2, Natural at bar 3. Change nothing else.
4. Save, quit Dorico, reopen the project, play once from the start through
   bar 3, then **quit Dorico** so the Probe closes its capture.
5. Run the check (next section).

## B. One voice

1. Import [one-voice-switch-order.musicxml](one-voice-switch-order.musicxml)
   as a new project: one violin, quarter = 80, four bars.
2. Enter Flautando at **bar 1 beat 1**, lasting through bar 3, and **`nat`**
   (or `ord`) at **bar 4 beat 1**. Don't type `natural`: a library may map it
   to a custom technique that doesn't cancel Flautando. Check the active
   playback techniques: Flautando on bars 1–3, Natural at bar 4.
3. In Play mode, route the violin to **Fiddle MIDI Probe**, Port 1 /
   Channel 1, with the r2 inspection map.
4. Save as `switch-order-B`, quit Dorico, reopen it, play once from the start
   through bar 4, then **quit Dorico**.
5. Run the check.

## Run the check

The Probe writes `capture.jsonl` into a new `fiddle-midi-probe-…` folder in
`$TMPDIR` for each instance; the check reports the ones without notes as
inconclusive. After each case:

```bash
for f in $(ls -t $TMPDIR/fiddle-midi-probe-*/capture.jsonl | head -3); do echo "== $f"; python3 experiments/dorico-switch-order/check_switch_order.py "$f"; done
```

Send me the output, or the capture paths.

## Local checks

```bash
python3 -m unittest discover -s experiments/dorico-switch-order -p 'test_*.py'
```

The tests run the check on reduced copies of the real 2026-09-25 (bug) and
2026-10-15 (single base) captures in [fixtures](fixtures), plus edits of them
with the late Off removed or moved before the On.
