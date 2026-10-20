# Woodwinds r3 — written note durations

[Importable map](Fiddle-Orchestral-Woodwinds-r3.doricolib). Allocation, values, mutex grouping and wire schema 2 are
identical to r2 ([r2](../woodwinds-r2/README.md)); see the generated [allocation](allocation.md)
and the full [built-in disposition](inventory.md).

Only the playback options change: the map overrides Dorico's six note-duration
options (note, staccato, staccatissimo, legato, tenuto, marcato) to 100%, so
Dorico sends written lengths whatever the project's Playback Options say, and
turns off "Shorten note if followed by abutting note of same pitch" (Fiddle
keeps same-pitch notes from overlapping itself).
Fiddle then applies the duration percentages itself, per layer: the layer
map's own override, else Fiddle's project copy of Dorico's options
(the **Settings…** window's **Dorico note durations**). Wire schema, typed Offs and the
beacon are unchanged; the new version gives a new beacon.

This map has a new entity ID and mutex-group IDs; r2 remains frozen. Not
yet host-verified: check in Dorico that the overrides apply exactly (no
minimum-length clamping or tick rounding) and change nothing else.
Regenerate from the repository root with `python3 experiments/orchestral-winds-bridge/generate_winds.py` (add `--check` to verify).
