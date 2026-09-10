# Audio real-time and synchronization audit

Follow-up: [audio-safety-results.md](audio-safety-results.md) records the implemented
frame-length, MIDI/inspection and lifecycle/undo repairs and their boundaries.
The observations below describe the earlier code, not a fresh audit of those repairs.

Date: 2026-09-09. Scope: current working tree, including the uncommitted restore-service work. This is a diagnostic review, not a claim that the reported intermittent crackling has been reproduced or its single cause identified. No production code or project settings were changed during the audit.

## Conclusion

We cannot yet certify either “audio never waits for UI work” or “threading and synchronization are correct throughout.” The top-level rendering design is sound in several important respects, but there are definite correctness defects, avoidable UI load, and gaps at the plug-in, MIDI/control, and interprocess boundaries. A focused reliability pass should precede more audio features.

Strongest findings:

- **Reproduced:** short audio callbacks advance instrument processing by the prepared maximum block size, dropping part of the generated stream.
- **Observed:** the recurring full-mixer UI refresh dominated approximately half the sampled main-thread stacks, even without incoming MIDI during the sample.
- **Confirmed in source:** JUCE VST3 rendering can wait on a spinlock also used by main-thread reconfiguration; state operations also call into live vendor processors.
- **Confirmed in source:** MIDI annotation/inspection state lacks cross-thread ownership, and undo-held strips/buses bypass normal reader-aware retirement.
- **Confirmed in source:** the audio return does not negotiate sample rates or compensate clock drift, and its underrun recovery inserts abrupt silence.

These are not all explanations for passive looping. Editing/lifecycle races require an overlapping edit; short-buffer corruption requires a short callback; clock mismatch requires differing effective rates. Measurements must identify which condition occurs at the audible glitch.

## What was inspected and measured

The signal path is:

```
Dorico audio thread -> fixed-size MIDI queue -> relay thread -> TCP server
  -> tracking/annotation -> per-strip MIDI queues

Fiddle CoreAudio callback -> instruments (serial) -> strip FX
  -> direct Master or group bus/FX -> Master FX -> shared-memory audio ring
  -> Dorico audio thread -> Dorico output device

Message thread: UI, plug-in editors/state, graph construction, persistence
```

Read-only observations of the running system:

- FiddleServer was the **Debug**, ARM64 build. Its CoreAudio callback ran on `com.apple.audio.IOThread.client`, not the message thread.
- The default output device was Scarlett 6i6 USB at 44.1 kHz. The ring advertised 44.1 kHz. This does not independently establish Dorico's selected device, but observed production and consumption were both approximately 44.1k frames/second.
- An eight-second `sample` capture contained 4,118 samples per long-lived thread. The recurring `pushMixerState(false)` timer task appeared in 2,003 main-thread samples. This is a statistical stack observation, not a deadline measurement or an exact CPU-utilization percentage.
- The audio thread was mostly waiting for its next CoreAudio callback. During rendering it spent substantial time inside Synchron Player and the JUCE wrapper. Some stacks contained Synchron Player semaphore waits, whose purpose/duration cannot be established from stripped vendor symbols. There was no evidence of a sustained Fiddle audio-thread wait for its UI mutex.
- The MIDI receiver was waiting for input throughout that capture: this was **not a verified reproduction of the active musical loop**.
- Ring observations overlapping profiling included a drain to zero and long gaps. Profiling can perturb the process, so these are not attributed to the user's original problem.
- A subsequent **unprofiled 30-second** read-only observation found occupancy between **1,536 and 2,048 frames** (about 35–46 ms), no sampled empty/full/invalid ring states, and maximum observed producer/consumer index-change gaps around 13 ms. Polling can miss brief events; this does not rule out rare playback underruns.

Temporary diagnostic sources and the stack capture are in `/tmp/fiddle-audio-audit.1PIt6R`. The ring probe maps only metadata read-only and never consumes audio or advances indices. The block-length reproducer links existing production objects into a separate executable; it neither opens an audio device nor uses the user's database.

### Follow-up with Dorico playback enabled

The user enabled playback after the initial audit. A fresh 30-second ring observation, **before attaching the thread profiler**, differed materially from the idle baseline:

| Five-second interval | Producer frames/s | Consumer frames/s | Minimum fill | Polls finding empty | Maximum observed producer gap | Maximum observed consumer gap |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 44,128 | 44,128 | 0 | 11 | 13.91 ms | 12.93 ms |
| 2 | 43,307 | 43,204 | 0 | 42 | 24.96 ms | 69.83 ms |
| 3 | 43,824 | 43,619 | 0 | 18 | 23.79 ms | 69.19 ms |
| 4 | 43,819 | 44,024 | 512 | 0 | 23.48 ms | 12.89 ms |
| 5 | 43,919 | 43,714 | 0 | 12 | 23.81 ms | 57.74 ms |
| 6 | 43,185 | 43,287 | 0 | 24 | 23.98 ms | 58.24 ms |

No full or invalid ring states were sampled. Empty-poll counts are **not dropout counts**: several polls can describe one empty period, and draining the ring exactly does not itself prove an underrun on the next callback. Nevertheless, repeated empty periods, reduced frame throughput, and long intervals without consumer progress are consistent with starvation and rebuffering. `AudioConsumer::pullAudio()` deliberately stops advancing its index and returns silence while rebuilding approximately 40 ms of reserve. Consumer index gaps therefore do not by themselves prove that Dorico stopped receiving audio callbacks.

The probe requests 1 ms sleeps but runs as an ordinary scheduled thread; its observed index-change gaps are not exact callback durations. In-process counters remain necessary to distinguish late callbacks, processing overruns and ring recovery precisely.

A six-second FiddleServer stack sample started at **17:19:51 PDT**, with a requested 2 ms interval (`playback.sample.txt`):

- Incoming MIDI note processing was present, unlike the initial idle capture.
- 1,275 of 1,781 audio-thread samples were inside the device callback; 1,163 were in Synchron Player instrument processing. This shows substantial render load, not an exact CPU percentage or a measured deadline overrun. The profiler can perturb scheduling.
- 859 of 1,781 main-thread samples were in the queued full-mixer refresh, approximately 48%.
- There was no sampled sustained audio-thread wait on a Fiddle UI lock. Five instrument-processing samples ended in vendor semaphore waits; their purpose and duration remain unknown.

A second low-overhead ring observation **after profiling ended** also found repeated empty periods. Thus the empty-buffer behavior was observed both before and after profiling, not only during its perturbation.

The user subsequently reported an approximately **five-second extended stuttering episode**. Its exact time is not yet established. The six-second thread sample began at 17:19:51 PDT (approximately 17:19:51–17:19:58); if the episode overlapped that window, profiling is a plausible contributor and the episode must not be treated as an unperturbed reproduction. The repository's `log.txt` was last modified September 2 and cannot corroborate this September 9 episode. No exact callback/underrun trace was available to correlate it retrospectively.

**Updated assessment:** active playback provides evidence consistent with audio starvation that was absent from the earlier idle baseline. Heavy instrument rendering plus avoidable UI work is a stronger working hypothesis than a demonstrated UI-lock stall. This does not yet attribute a particular audible crackle, exclude other defects, or establish that Synchron Player is itself faulty. Prioritize deadline/underrun telemetry and the meter-only refresh path, alongside the independently reproduced short-block fix. No production code, audio settings, or playback controls were changed for these observations.

## Findings

### 1. P1 — Incorrect processing length for short callbacks (reproduced)

`MixerStrip.cpp:358–438` schedules MIDI for the destination length but calls the instrument with its full preallocated `runtime->scratchBuffer`. The scratch buffer's logical length remains the preparation block size. Only the requested prefix is mixed back.

`GroupBus.cpp:50–75` has the same underlying assumption: `beginBlock()` clears a prefix without narrowing the buffer presented to routed strips, and `processTo()` processes the full bus buffer.

A counting instrument prepared for 64 frames and rendered in two 16-frame callbacks produced this result on both direct and grouped paths:

| Callback | Expected output | Actual output | Cumulative instrument processing |
| --- | --- | --- | --- |
| First | 0–15 | 0–15 | 64 frames |
| Second | 16–31 | 64–79 | 128 frames |

This is an actual discontinuity, not merely inefficient processing. It can also advance effect state and consume MIDI earlier than intended. JUCE's device callback size enforcer guarantees a maximum, not an invariant callback size.

**Recommendation:** keep storage preallocated, but pass non-owning views with exactly the current frame count throughout instrument, strip, bus and Master processing. Define safe behavior for zero and oversized blocks. Add varying-block sample-continuity tests on direct/grouped routes, including effects and timestamped MIDI.

### 2. P1 — Significant unnecessary work at meter-refresh frequency (observed)

`MainComponent.cpp:2426` queues full mixer/Master/bus refreshes every 60 ms. `pushMixerState()` at line 2120 serializes the complete mixer to JSON, parses it back, queries SQLite for stale library layers, then serializes/broadcasts it again. `MixerStrip::toJson()` also enumerates **every program name** of each loaded instrument on each refresh.

The stack sample directly confirms this path is a major main-thread workload. It is not itself proof of an audio mutex stall, but it unnecessarily competes for CPU/cache/allocation resources and slows UI/control work. Debug overhead aggravates this; Release is a useful comparison, not a correctness fix.

**Recommendation:** separate a compact meter snapshot (IDs and atomic numeric levels) from structural/configuration state. Send structural data on changes, cache program lists and library-revision status, and coalesce UI notifications. Do not query SQLite or enumerate plug-in metadata on a meter tick. Make detailed MIDI logging opt-in and batched.

### 3. P1 — The complete rendering path is not wait-free (source-confirmed)

The top-level `MixerModel::processBlock()` uses atomic publication rather than `stripsMutex_`. However, the actual JUCE 9 implementation contains these downstream synchronization points:

- `juce_VST3PluginFormatImpl.h:2395`: VST3 `processBlock()` takes `processMutex` with a **blocking spinlock**, not a try-lock. The same lock is used for MIDI mapping updates, preparation, bus-layout checks, reset and release. `juce::SpinLock::enter()` eventually yields repeatedly until it succeeds.
- `juce_AudioProcessorGraph.cpp:922`: each process node takes a callback critical section. In Fiddle these nodes wrap our own small processors rather than the actual vendor editor; ordinary editor activity does not automatically contend for this particular lock.
- `juce_AudioDeviceManager.cpp:1085`: device callback dispatch takes `audioCallbackLock`, also used for callback registration/removal. This is principally a lifecycle boundary, not ordinary meter rendering.
- Vendor processing can itself wait; Synchron Player semaphore waits were visible in the sample. We cannot certify a closed-source plug-in's internal scheduling.

Importantly, JUCE graph publication itself uses a **try-lock and pointer swap**, deferring deletion to the message thread (`juce_AudioProcessorGraph.cpp:1658–1708`). It should not be rewritten on the mistaken assumption that graph publication routinely blocks for the UI.

**Recommendation:** audit and constrain operations on live instances while rendering. Construct/configure replacements outside the active path, publish them at a safe boundary, and retire old instances off the audio thread. Respect main-thread requirements for VST3 lifecycle/state calls; simply moving them to arbitrary worker threads is not safe. Instrument processing durations and exercise plug-in restart/reconfiguration deliberately. A try-lock that drops the audio block is not an adequate audible-quality fix.

### 4. P1 — State capture/application is not isolated from live rendering

`HostedPluginSlot.cpp:283–310` calls vendor get/set state and program APIs on the currently published processor. Runtime publication protects object lifetime, **not concurrent mutation inside the processor**. The code provides no policy preventing these operations from overlapping rendering.

Mute/solo/library activation can invoke `saveAllStripsToDB()` through `MixerJsHandlers` persistence callbacks. That routine refreshes all instrument states. Effect `snapshot()` methods call `captureState()` rather than merely reading cached bytes, so persistence and host-state rebuilds can also serialize live effects. Explicit editor gestures can initiate state capture during playback.

The JUCE get/set-state functions require the message thread and call the vendor component/controller; they do not prove that a particular vendor's state operations are free of audio-side contention. The sample does not establish that state capture caused a crackle.

**Recommendation:** distinguish cheap scalar persistence from expensive processor-state capture, coalesce captures, and establish an explicit live-edit/save policy. For exact Dorico saves, retain the invariant of returning only a fully captured committed version; any deferral or temporary safe suspension must be explicit rather than quietly saving stale bytes. Prepare restoration state before publishing a replacement where possible.

### 5. P1 — MIDI/control objects have cross-thread data races

Incoming MIDI is processed on `MidiTcpServer`'s thread. `MixerModel::routeAnnotatedNoteOn()` at line 496 dereferences the strip's annotator and writes annotation history. Meanwhile, message-thread editing replaces `annotator`, `expressionMap`, and `incomingTracker`, changes the Lua collection, and reads/clears annotation history (`MixerStrip.cpp:147–216,315`). These are ordinary pointers/containers, not synchronized publications.

The graph reader guard keeps the **strip** alive; it does not keep a replaced **annotator** alive or make concurrent deque/vector/shared_ptr mutation safe. An open MIDI inspector or an expression-map/Lua edit during playback can race the producer.

The MIDI path also shares locks with UI-side note/subnote/capture work and posts many individual UI messages. This need not stop sample rendering directly, but can make musical events arrive late.

**Recommendation:** give mutable MIDI/annotation state one non-UI owner, deliver control changes through a command queue, and publish immutable inspector snapshots. Keep UI formatting and log delivery downstream of MIDI scheduling. Add thread-sanitizer and concurrent inspector/map-change tests; do not solve this by putting a large UI mutex into the audio callback.

### 6. P1 — Undo lifetime/reinsertion bypasses the retirement protocol

Normal strip/bus removal retires objects until published-graph readers have left. `removeStripKeepAlive()` and `removeGroupBusKeepAlive()` instead hand unique ownership to undo actions (`MixerModel.cpp:81,219`). Clearing/trimming undo or discarding redo destroys those objects without consulting graph readers. An older in-flight graph can still hold their raw pointers.

Reinsertion calls `prepareToPlay()` again on that same object (`MixerModel.cpp:94,199`). If an old graph is still processing it, this can reconfigure processors or alter buffers concurrently. Reader guards protect lifetime only when every destruction route honors them.

**Recommendation:** unify undo ownership and render retirement. Undo history disposal must retire render-visible objects rather than delete them immediately; reinsertion must not reprepare an in-use generation. Test with a deliberately paused audio callback, remove/undo/redo, history clearing and repeated version replacement.

This is an edit/lifecycle defect, not evidence for unexplained crackles during an untouched loop.

### 7. P1 — Audio IPC assumes matching clocks and has abrupt recovery

`MainComponent::initAudioDevice()` chooses the system default output as Fiddle's render clock. Dorico has its own audio-device configuration. The native consumer reads frames one-for-one; there is no rate negotiation, resampling or drift feedback.

`AudioSharedMemory.h:99` initializes the advertised rate to 44.1 kHz. `setSampleRate()` exists, but no production call updates it when `audioDeviceAboutToStart()` obtains the actual device rate. Thus 48 kHz operation also has incorrect advertised buffering metadata.

On overflow, `pushAudio()` silently drops an entire block. On underrun, `AudioConsumer.h:114` immediately outputs zero and waits to refill approximately 40 ms before resuming. Those transitions can click/drop out. Producer restart resets both cursors in place without a generation handshake; unsigned `write-read` arithmetic is not validated against reset/inconsistent cursor states. `remap()` also uses a weaker publication/reclamation memory-order protocol than the shared `RealtimeObjectPublisher` design and warrants its own concurrency test.

**Recommendation:** report both rates and device/block settings; publish the actual producer rate; add exact underrun/overflow/rebuffer counters and generation-aware reset handling. Define a supported same-clock configuration first, then choose a host-clock-driven renderer or bounded rate/drift conversion for independent clocks. Recovery should be deliberate and click-controlled, without hiding repeated dropouts. Do not merely increase buffering and declare the cause fixed.

No sustained rate mismatch or drift was observed in the short unprofiled baseline.

### 8. P2 — Gain/bypass changes can click without a scheduling failure

Strip and Master gain processors apply one new scalar to an entire block, with no ramp (`StripAudioEngine.cpp:24`, `MasterAudioEngine.cpp:24`). Mute/solo can abruptly gate summation. Effect bypass immediately skips processing; it neither crossfades wet/dry nor preserves a latency-aligned bypass path.

**Recommendation:** short sample ramps for gain/audibility changes and a defined latency-preserving bypass transition. Test continuity on a nonzero waveform and through a delayed effect. This explains clicks while changing controls, not an otherwise untouched loop.

### 9. P2 — Real-time work and allocation guarantees need stronger tests

The main handoffs use fixed-size queue elements and preallocated buffers. However, `RealtimeMidiScheduler::renderBlock()` drains its incoming queue until empty without a per-block work budget; concurrent producers can keep refilling it. Scheduling many due events also incurs ordered `MidiBuffer` insertion work. The JUCE VST3 MIDI event list grows a dynamic array on high-water marks. The audio producer has a `std::cerr` path when shared memory is not ready.

These prevent a blanket claim of bounded, allocation-free, logging-free execution. A `free` frame in the stack sample alone is not evidence of a significant deallocation: short non-owning JUCE buffers may destroy an empty heap holder.

**Recommendation:** impose explicit MIDI work limits with a safe note-off/panic overflow policy, prewarm/reserve supported capacities where possible, count overloads, remove callback logging, and instrument allocations on the actual render thread. Vendor behavior remains a separate acceptance boundary.

## Threading assessment

Appropriate foundations already present:

- CoreAudio supplies the server's audio callback thread.
- Dorico's process callback hands off fixed-size MIDI events; TCP, protobuf serialization and synchronous save responses are outside that callback.
- Strip gains, mute/solo, input assignments, bypass flags and meters use atomics.
- Top-level graph and instrument-runtime publication pin readers and normally reclaim on the message thread.
- Hosted change listeners flag atomic notifications instead of calling the UI directly.
- Instruments run serially in the server callback. That is a legitimate initial design, not a threading bug by itself. A long instrument can nevertheless consume the block's deadline regardless of total machine CPU availability.

Do not introduce per-block thread creation, futures, a general-purpose thread pool, or arbitrary high-priority threads as a first remedy. Measure per-strip/worst-block costs first. If parallel rendering is justified, it needs bounded audio workers, appropriate macOS audio-workgroup scheduling, preallocated jobs and an explicit deadline/overload policy.

## Proposed reliability pass

1. **Measure and reduce avoidable contention:** fixed-size render timing/IPC telemetry, JUCE device xrun/load counters, per-strip maxima and UI-work timing; replace full 60 ms mixer rebuilds with meter-only updates. Compare Debug and Release under the same load.
2. **Fix reproduced frame-count corruption:** carry current block lengths through every route and add variable-size continuity tests.
3. **Repair ownership and lifecycle synchronization:** MIDI/inspector ownership, undo retirement/reinsertion, live plug-in state and reconfiguration policy.
4. **Harden the audio return:** correct rate metadata, mismatch diagnostics, generation-safe restart, measurable/click-controlled recovery, then the clocking strategy.
5. **Add control smoothing and enforce real-time budgets.**

Regression coverage should combine real audio-thread rendering with message-thread edits, stalled UI periods, MIDI floods, asynchronous plug-in loading, and undo/history disposal. Include randomized block lengths, 44.1/48/96 kHz, unequal producer/consumer block sizes, clock skew, pauses/restarts and deliberate overload. Assert continuity, ordering, memory safety, and overload reporting—not only final settings or average CPU use.

The existing integration suite is valuable functional coverage, but renders deterministic fixed-size blocks largely on the test/message thread. Its passing results do not establish real-time deadlines or concurrent UI/render safety. Vendor and Dorico acceptance should follow deterministic tests, with recorded counters at each audible glitch rather than relying on listening alone.
