# D r4 export: shared-pitch velocity commands

> **Historical result.** Preserve the failed shared-pitch encoding as evidence;
> later rounds completed the next test described below. Current work follows
> [Orchestral bridge maps](../../../docs/orchestral-bridge-maps.md).

Analyzed `velocity-r4.mid` on 2026-09-22 without modifying it.
SHA256: `b9bc23b112bbacedcfde48b0213e91da88188c571771db6cc263699101061d3f`.
Map SHA256: `9fc5cd60e54a7c865a4670e8498ad74817b245e3bddfeecfbd0dd3b0abe6275a`.
The file contains sixteen musical notes in track 1, port 0, channel 1, at 480 PPQN.

**13/16 expected PT sets match.** Notes 11 and 13 lack Flautando; note 12 lacks
Flautando and Fouetté. Non vibrato survives throughout that passage. No unknown
command values or missing initialization are reported. The only ordering warning
is the existing CC102=2 after the first musical note-on at tick zero.

At tick zero, pitch 0 note-ons with velocities 2, 64, and 127 explicitly initialize
the three modifiers OFF before the musical note. During the passage, velocity 1
enables Flautando at tick 4310 and velocity 2 disables it at 4789. At 4790 only
velocity 63 enables Overpressure: Flautando's required reactivation is missing.
At 5270 only velocity 63 appears again; Fouetté's velocity 126 is absent from
the entire capture. The pattern repeats at 5750. Flautando returns at tick 6230.

Observed positive command velocities are 1, 2, 63, 64, 127, all exact configured
values. This shows loss of required commands, not numerical corruption of those
that were transmitted. It does not prove where Dorico drops them or establish a
universal one-value-per-pitch rule: initialization successfully emits three.

The earlier C r3 distinct-pitch capture matched all sixteen PT sets. The next
[E r5 experiment](../dimensions-experiment.md) therefore separates the three
dimensions onto distinct pitches while retaining D's six velocities and other
options. It changes no notation or expectations. A regression test preserves
D's actual failure and map hash so decoder changes cannot quietly erase it.
