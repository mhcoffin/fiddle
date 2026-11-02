# Pitched percussion r2 — written note durations

[Importable map](Fiddle-Orchestral-PitchedPercussion-r2.doricolib). Allocation, values, mutex grouping and wire schema 2 are
identical to r1 ([r1](../README.md)); see the generated [allocation](allocation.md)
and the full [built-in disposition](inventory.md).

Only the playback options change: the map overrides Dorico's six note-duration
options (note, staccato, staccatissimo, legato, tenuto, marcato) to 100%, so
Dorico sends written lengths whatever the project's Playback Options say, and
turns off "Shorten note if followed by abutting note of same pitch" (Fiddle
keeps same-pitch notes from overlapping itself). Switch commands keep Dorico's
default lead of 10 ticks whatever the project says; Fiddle applies the
project's lead to each layer's own switches. Dynamics are sent on a linear
curve over levels −8..8 with no articulation deltas, so Fiddle can read each
note's dynamic level back and apply each layer's own curve.
Fiddle then applies the duration percentages itself, per layer: the layer
map's own override, else Fiddle's project copy of Dorico's options
(the **Settings…** window's **Dorico playback options**). Wire schema, typed Offs and the
beacon are unchanged; the new version gives a new beacon.

This map has a new entity ID and mutex-group IDs; r1 remains frozen. Not
yet host-verified: check in Dorico that the overrides apply exactly (no
minimum-length clamping or tick rounding) and change nothing else.
Regenerate from the repository root with `python3 experiments/orchestral-percussion-bridge/generate_percussion.py` (add `--check` to verify).
