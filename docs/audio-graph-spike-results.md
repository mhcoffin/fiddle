# Phase 1 Audio-Graph Spike Results

**Status:** Complete; graph approach validated  
**Date:** 2026-09-02  
**Scope:** Deterministic, non-UI JUCE 9 `AudioProcessorGraph` prototype

## Result

The proposed graph architecture is viable. The retained
`AudioGraphSpikeTest` covers the difficult behavior needed before production
refactoring:

- separate scheduled-MIDI sources feeding separate instrument nodes;
- a stereo instrument path with an insert, send branch, aux return, Master
  effect, and audio output;
- sample-aligned summing of dry and latency-bearing send paths;
- latency changes followed by a graph rebuild;
- continuous rendering while insert nodes are repeatedly added and removed;
- no ordinary or aligned C++ heap allocations during steady-state rendering.

The focused test passed 100 consecutive executions. Each execution performs 64
insert/remove topology cycles while another thread continuously renders audio.
The complete project build and all 18 CTest tests also pass.

## Answers to the Phase 1 questions

### Can scheduled MIDI be isolated per instrument?

Yes. A MIDI-only source node can feed one instrument node without leaking events
to another instrument. The test schedules different notes at different sample
offsets and verifies both the receiving processor and the resulting stereo
samples.

Production code should therefore use one `ScheduledMidiSourceProcessor` per
instrument strip, backed by that strip's realtime scheduler.

### Does JUCE compensate the dry/send topology correctly?

Yes. With an eight-sample effect on the aux return, the graph delays the dry
path and sums both paths at sample eight. After the effect changes its reported
latency to twelve samples and the graph is rebuilt, both paths sum at sample
twelve. `AudioProcessorGraph::getLatencySamples()` reports the corresponding
overall latency.

Latency-change notifications must still be marshalled to the message thread;
the effect processor must not rebuild the graph from the audio callback.

### Can topology change during continuous rendering?

Yes, provided graph mutation and rebuilding occur on the message thread. The
test alternates between a direct path and an inserted-gain path while a separate
thread continuously calls `processBlock`. Every rendered block belongs wholly
to either the old or new topology; none contains missing, mixed, or corrupt
samples.

JUCE 9 builds a render sequence on the control side and offers it to the audio
thread through a try-lock exchange. If the exchange is temporarily busy, the
callback keeps its current sequence rather than waiting. Retired sequences are
reclaimed later by JUCE's message-thread timer.

This validates the exchange mechanism, not arbitrary third-party plug-in
behavior. Production code must continue to prohibit plug-in construction,
destruction, state capture, and editor work on the audio thread.

### How should processor and editor ownership work?

The graph should be the sole owner of processor instances. Control-side code
should retain `AudioProcessorGraph::Node::Ptr` values or stable Fiddle IDs, not
long-lived raw processor pointers. A transient map should translate each stable
`PluginSlotId` to its current graph node ID.

Editor windows remain message-thread objects outside the graph. A hosted-slot
abstraction should bind the stable slot ID, graph node handle, cached state, and
editor lifetime. It must close the editor before requesting node removal and
release its node handle only after publishing the replacement topology. This is
the ownership model to implement in Phase 2.

### What scheduling adjustment is required?

Fiddle currently schedules a running-transport event at:

```text
now + max(0, playbackDelayMs - 40 ms)
```

To keep the audible result at the same target after adding graph latency, use:

```text
graphLatencyMs = 1000 * graphLatencySamples / sampleRate
triggerDelayMs = max(0, playbackDelayMs - 40 ms - graphLatencyMs)
minimumSafePlaybackDelayMs = 40 ms + graphLatencyMs
```

If `playbackDelayMs` is below that minimum, scheduling is clamped to now and the
UI must report that the configured delay cannot absorb the current graph
latency. The stopped-transport rule remains unchanged: cleanup events are sent
immediately.

## Allocation finding

After graph preparation and eight warm-up blocks, the spike renders 256 blocks
under a thread-local global allocation probe. It observes zero ordinary and
zero aligned C++ heap allocations. This covers the graph and every fake Fiddle
processor in the tested steady-state path.

The result does not promise that every third-party plug-in is allocation-free.
It establishes that Fiddle's graph wrapper and custom processors can satisfy
the realtime requirement without introducing allocations of their own.

## Deliberate limitations

Phase 1 does not load real VST3 plug-ins, open editors, persist project state,
exercise effect tails, or change the production mixer. It also does not attempt
audio-device timing or quantify callback jitter under operating-system load.
Those concerns belong to the later acceptance tests and manual smoke-test
matrix.

No persistence or migration code was added. As agreed in the design, the first
audio-routing schema may begin cleanly because existing projects are disposable
tests.

## Decision

Retain the spike as regression coverage and proceed to Phase 2: extract the
hosted plug-in abstraction and catalog metadata without yet changing the visible
mixer workflow.
