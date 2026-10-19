# E r5 export: separate dimensions with velocity values

> **Historical result.** Preserve the successful independent-pitch evidence;
> later [live tests](../live-results.md) followed this export. The current plan
> is [Orchestral bridge maps](../../../docs/orchestral-bridge-maps.md), not the
> prospective next-test instructions below.

`dimensions-r5.mid` SHA256:
`39f6b6b4bc3d096177c5340bfdde3421fc1048aa6f4679368012d984d675c7e6`.
The export contains sixteen musical notes in track 1, port 0, channel 1, 480 PPQN.
The source was not modified.

**16/16 PT sets match.** All six commands arrive with their exact velocities:
pitch 0 at 1/2, pitch 1 at 63/64, and pitch 2 at 126/127. Initialization explicitly
establishes all three modifiers OFF. They coexist correctly at note 12 and cancel
correctly at notes 13–15. There are no unknown commands or missing initial states.

The sole warning is unchanged: CC102=2 follows the first musical note-on at tick
zero. The inspector consequently does not mark full acceptance as passed.

Relative to D, separating command pitches recovered the missing modifier
commands while preserving velocities and other map options. This supports one
pitch per independent dimension for these binary states in this export. It
does not establish all velocity values, multi-valued group transitions, or live
ordering/restoration. The next test is [F r6](../high-vibrato-experiment.md).
