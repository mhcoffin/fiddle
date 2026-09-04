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

The next Phase 4 slice will place ordered hosted-effect nodes between the graph
input and fader, then expose them through a generously sized Strip Audio panel.
There are no strip-effect commands or UI in Phase 4A.

Automated coverage verifies independent graph instances, stereo gain, silence,
zero initial latency, and absence of cross-path state leakage. The production
server builds successfully, and all 26 stable tests pass (the loopback TCP test
must run outside restricted sandboxes).
