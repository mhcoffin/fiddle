# Strip audio-path foundation

Phase 4A introduces one independent post-instrument JUCE
`AudioProcessorGraph` per mixer strip. The existing instrument host renders
into its private scratch buffer, that buffer passes through the strip graph,
and the resulting stereo signal is summed into the Master input.

The initial graph topology is deliberately small:

1. stereo graph input;
2. strip fader;
3. stereo graph output.

Mute, solo suppression, library deactivation, and a fader at minus infinity
set the graph gain to zero. The instrument and graph still process the block,
so MIDI state remains synchronized and the eventual effect rack can preserve
tails while inaudible. Metering occurs after the graph, matching the existing
post-fader behavior.

The completed Phase 4B slice places ordered hosted-effect nodes on both sides
of the fader and exposes them through the generously sized Channel Audio panel.
See [`strip-effect-rack-results.md`](strip-effect-rack-results.md).

Automated coverage verifies independent graph instances, stereo gain, silence,
zero initial latency, and absence of cross-path state leakage. The production
server and stable regression suite pass (the loopback TCP tests must run
outside restricted sandboxes).
