# Audio safety pass — September 2026

This implements the correctness/lifecycle portion of the real-time audit, on top
of host-driven render ahead. It does not resolve the separately observed WebKit
UI freeze or certify closed-source plug-ins as wait-free.

## Exact frame counts

Instrument scratch storage remains preallocated, but instruments and strip FX
receive a view with exactly the current block length. Group buses similarly
separate summing storage from the current-length view. Zero-sized blocks do not
advance MIDI or DSP; oversized top-level blocks return silence without advancing
processor state. Production render-ahead uses its prepared fixed block size.

Tests exercise varying lengths (including zero and oversized input), a counting
instrument, real sample delays through strip/bus/Master effects, and note-on/off
deadlines on both direct and grouped routes.

## Undo and graph ownership

Published graph generations retain shared ownership of their strips and buses.
Undo holds another reference rather than exclusive ownership. Discarding history
cannot destroy an object still reachable from an in-flight graph. All graph
reclamation remains on the message thread; rendering copies no shared pointers.
Undo handles must likewise be released on the message thread.

Reinsertion does not re-prepare an object at an unchanged rate/block size.
Re-preparation when settings differ passes through the control boundary below.
A test pauses a processor inside a render, removes/reinserts its strip and bus,
discards undo history, and checks both lifetime and absence of re-preparation.

Manual acceptance exposed a separate, pre-existing gap: the chair-layer Remove
handler bypassed undo altogether. It now uses an action retaining both the
assignment row and the live strip, with persistence and chair/UI updates on
execute, undo and redo. A regression checks three removal/undo cycles, original
player/FX identity, ordering, bus routing, gain, SQLite reopening and clearing
held notes before the restored player resumes.

Adding a chair layer had the same undo gap. Addition now creates its catalog
copy only once and uses the retained-strip action in reverse for undo/redo.
Initial player state is applied by the strip's own load completion, so undoing
before asynchronous loading finishes cannot skip patch restoration. Tests cover
normal, pending and missing players, catalog edits while undone, and interleaved
add/remove history.

## MIDI annotation and inspection

Each strip has a separate recursive mutex for its **non-audio** MIDI/control
domain. Annotation, incoming-switch tracking, held keyswitches, expression-map
replacement, Lua-chain edits and inspector clearing use that same boundary.
DSP never acquires it. Inspector data is copied under the lock and formatted
afterward; capture-log serialization likewise formats a detached snapshot.
Each layer now receives its own input-note copy, avoiding cross-layer mutation.
Shared sample-rate metadata is atomic.

This is a conservative race repair, not the larger command-queue/single-owner
MIDI redesign. Expensive Lua execution can still delay MIDI processing or a UI
edit waiting for this lock. Further batching/backpressure belongs with the UI
and MIDI performance work. Concurrent map/inspector/panic/routing tests cover
the repaired ownership boundary.

## Live state and lifecycle policy

`AudioProcessingGate` is a process-wide, nested boundary for hosted processing.
The control thread requests quiescence and waits for an existing render to
finish. New render attempts do not wait or spin: the worker defers them and
publishes no block. Once control work finishes, the worker replenishes its
reserve normally. The render entry protocol closes the request/reader race.

Fiddle-initiated state capture/application, program changes, preparation and
editor creation/destruction run on the control side. State APIs remain on the
message thread, not an arbitrary background worker. Parameter/program metadata
queries also use the boundary. The reserve can cover short operations; a long
vendor call can exhaust it and cause an explicitly observable interruption.
Stop playback for large state changes. This is not a seamless snapshot promise.
Audio performance reports the longest control-boundary duration (including
startup and waiting for an existing render), retained for the process lifetime.

Mute/solo scalar persistence can use cached instrument/effect bytes. Explicit
saves and version snapshots retain fresh capture; stale bytes are not silently
substituted for an exact Dorico save. A save now freezes every instrument and
effect state once under one control interval, then reuses those exact in-memory
bytes for both session-database persistence and the version snapshot. Existing
background snapshot rebuilds can still capture state, but now do so through the
safe boundary.

Tests concurrently render while capturing/applying state and re-preparing,
assert control calls stay on their owner thread, and check no state capture
overlaps processing. An actual worker test checks that control suspension does
not publish silence or advance the producer cursor.

Vendor-private threads, direct editor parameter activity and JUCE's internally
initiated VST3 restart callbacks are outside this boundary. A processor that
synchronously waits for the UI from its render call can still obstruct control
quiescence. These constraints must remain part of compatibility testing.

## Validation and follow-up

Release validation: 51 stable CTest entries pass; the mixer and hosted-slot tests
also passed 20 consecutive runs. The rendered browser regression passes. The
ThreadSanitizer build of both concurrency test executables runs successfully
without race reports in the exercised scenarios. This is targeted coverage, not
a claim that all possible application or vendor interleavings have been tested.

The stable suite includes the new cases in `mixer_integration` and
`hosted_plugin_slot`. Configure a separate build with
`-DFIDDLE_TEST_THREAD_SANITIZER=ON`, build those two targets, and run them under
CTest for instrumented concurrency checking. Dependencies must be available as
for the normal build; existing local FetchContent source overrides can be reused.

Remaining work includes the recovered UI freeze, stronger actual-VST3 host tests,
MIDI queue work/allocation budgets, gain/bypass smoothing, and vendor lifecycle
acceptance. Harmonic analysis remains deferred.

For manual acceptance, stop playback and restart the Release FiddleServer (no
native-plugin reinstall). Play the existing score, inspect MIDI while changing
an expression map, exercise remove/undo/redo for a layer and bus, then save and
reopen the project. For a save during playback, inspect the longest control pause
and return-underrun counters: state capture is safe but not guaranteed seamless.
