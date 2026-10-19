# Strings r3 — curated inspection candidate

[Importable map](Fiddle-Orchestral-Strings-r3.doricolib), display name
**Fiddle Bridge - Orchestral Strings r3 - INSPECTION ONLY**.

The current allocation is 32 add-on dimensions plus one combined base dimension:
33 conduits, 18 CCs and 15 keyswitch pitches (KS0–14), leaving 19 pool entries free.
It retains 25 original base rows and has 87 add-on rows for 91 distinct built-in
PT IDs plus the Bowed→Natural alias. There are 22 explicit mutex groups.

The profile covers violin, viola, cello and double bass, solo or section, with
one voice per routed channel. Retained general dynamics/articulations and extended
string techniques are deliberate; this is not restricted to common sample-library
patches. See [curation](curation.md), [allocation](allocation.md) and the full
[298-ID disposition](inventory.md).

All retained grouping and Natural-cancellation decisions are inherited from r2;
On sets only its dimension and Off clears only that dimension. Base rows retain
original conditions, timing, volume and velocity data, writing CC102 only. This
revision changes vocabulary/allocation, not the proposed Off semantics.

This is not a matched production map/decoder. No r3 host acceptance has occurred,
and prior r2 results do not establish r3 host behavior. For optional inspection,
import into a disposable Dorico project via Library > Expression Maps and review
alongside r2. Do not assign to production Fiddle or a VSTi. No installation,
endpoint assignment, or playback-template changes have been made.

Allocation and enum values have changed; the entity ID and mutex group IDs are
revision-specific. Root-level r2 data remain available for old captures. Reserved
musical range 21–114 is unchanged in this candidate; freed pool entries are spare,
not an expanded-range promise. The eventual decoder must consume command-note
releases as well as On commands before downstream instrument routing.

Reproduce from the repository root with
`python3 experiments/orchestral-strings-bridge/generate.py`; add `--check` for
read-only verification. Local tests cover exact retained/removed IDs, all 298
inventory dispositions, action/reset consistency, allocation budget, and frozen
r2 reproduction.
