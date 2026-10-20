# Brass r2 — wire schema 2 host-test candidate

[Importable map](Fiddle-Orchestral-Brass-r2.doricolib). Allocation, values and mutex grouping are identical to
r1 ([r1 README](../brass-r1/README.md)); see the generated [allocation](allocation.md) and the full
[built-in disposition](inventory.md).

Only the wire encoding changes (wire schema 2, compiler schema 2):

- **Typed Off.** Every add-on Off writes On + 64 on its own conduit, so an Off
  ends only the technique it names. A late Off from a replaced technique is
  ignored instead of clearing its successor. Fiddle also suppresses an Off that
  arrives within 30 ms of its own technique's On (the base-change pre-roll case).
- **Revision beacon.** Init sends KS126 and KS127 with velocities derived from
  the map's entity ID and version. When playback starts, Fiddle compares them
  with the assigned revision. A match verifies the assignment without a manual
  export; a mismatch withholds notes. KS126/127 are reserved, so the pool is
  50 conduits including the base.

This map has a new entity ID and mutex-group IDs; r1 remains frozen as a
schema-1 artifact. Not yet host-verified. Supports one Dorico voice per routed
channel. Regenerate from the repository root with the winds generator in the
[candidate overview](../README.md) (`python3 experiments/orchestral-winds-bridge/generate_winds.py`).
