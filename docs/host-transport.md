# Host transport for hosted plug-ins

Status: implemented 2026-11-18. Works with Vienna Performer in Dorico: it
captures the passage and its render plays back cleanly.

## Why

Plug-ins Fiddle hosts used to get no playhead (`getPlayHead()` returned
null). Most instruments don't care, but some follow the host's timeline.
Vienna Performer, for example, captures the MIDI its track plays "from the
playhead onward" and files each performance at a score position. Without a
playhead it played its preview sound but never captured anything.

## What a plug-in sees

Every plug-in installed through `HostedPluginSlot` gets `hostPlayHead()`.
That covers instruments, strip and bus inserts, and Master. It reports:

| Field | Value |
| --- | --- |
| playing | Dorico's transport, for the block being rendered |
| time in seconds / samples | Dorico's playback position, at Fiddle's sample rate |
| bpm, time signature | Dorico's `ProcessContext` tempo and signature |
| ppq, bar start, bar count | Derived from seconds at that constant tempo |
| recording, looping | Always false |

Dorico reports only the project's initial tempo, and its own beat clock
(`projectTimeMusic`) also runs at that rate (see `FiddleProcessor.cpp`). So
ppq is right only before the first tempo change. Seconds and samples stay
right throughout. A plug-in that files material by beats will drift after a
tempo mark.

## How the position is worked out

FiddleNative already sends `TransportStart` and `TransportStop` with Dorico's
`projectTimeSamples`. FiddleServer handles them in `MainComponent`:

- **Start:** `hostTransport().start(startMs, startSeconds, startFrame)`.
  `startMs` is the presentation time at which the start's notes are scheduled
  (the same `getDelayedTriggerTimeMs()`). `startSeconds` is Dorico's position
  divided by Dorico's sample rate. `startFrame` is the render-ahead stream
  frame that presents at `startMs` (`RenderAheadEngine::frameAt`).
  - FiddleNative sends the rate with START (`TransportEvent.sample_rate`), so
    it is right even before the first diagnostics arrive.
  - An older FiddleNative leaves the rate out. The server then uses the rate
    from diagnostics, or else its own device rate. A disconnect forgets the
    diagnostics rate.
  - The log shows `[Transport] Dorico started at N s`.
- **Stop:** `stop(stopMs, stopFrame)` at the presentation time a Dorico event
  arriving now plays at, and its stream frame. A lost connection stops the
  transport too.
- **Tempo:** `setTempo(bpm, numerator, denominator)`.

A block presented at time `t` sits at `startSeconds + (t − startMs) / 1000`.

- **Counting frames.** A block's presentation time is projected from
  Dorico's clock, which wanders by a few samples from block to block. The
  render-ahead engine's stream frames are contiguous, so when a run and a
  block both have frames, the position counts them instead: the block at
  frame `f` sits at sample `round(startSeconds × rate) + (f − startFrame)`.
  Each block then starts exactly where the one before it ended.
  - This matters. Vienna Performer reads any jump in the playhead as a
    relocation. With positions from clock time, about one block in ten
    jumped by 1–5 samples. Its captures came out as a handful of 30–75 ms
    notes, and its renders played as bursts of static.
  - A trace of 83 s of playback after the fix showed every step was exactly
    one block (64 samples).
  - Frame times are converted once, at start and stop. Blocks without a
    frame (tests, previews, an older caller) and runs without one fall back
    to presentation time.
  - **Streams.** Each render-ahead stream counts frames from its own start,
    and a device restart (a new device, rate or buffer size) starts a new
    one. Frames therefore carry their stream's id (`StreamFrame`), and a run
    counts only frames of the stream it started in. If the device restarts
    mid-run, the rest of that run uses presentation time, and a stop in the
    new stream ends it by time too. The next start counts frames again.

- **Recent runs:** presentation times run up to the playback delay ahead.
  After a quick stop and restart, blocks of the previous run are therefore
  still to be rendered when the next run is published. The transport keeps
  its last `kRuns` (4) runs. Each block takes the newest run it reaches, and
  a start ends any run still going.

- **Overlapping blocks play.** A block that overlaps `[startMs, stopMs)`
  counts as playing. Dorico's start rarely falls on a block boundary, and the
  run's first notes must not arrive in a stopped block. The block before them
  therefore starts slightly short of `startSeconds`; the position is clamped
  at 0.
- **Outside a run:** before a run, the playhead rests at its start. After a
  stop, it rests where playback stopped.
- **Latency:** each plug-in sees the score time of what reaches it.
  - Each strip schedules its MIDI early by its insert-rack and downstream bus
    latency (`MixerStrip::midiAdvanceMs()`). Its instrument sees
    `t + midiAdvanceMs()`, so a note and the playhead in the same block agree.
    Its frame is advanced by `midiAdvanceSamples()`.
  - A chair bus's rack sees `t +` its own latency and everything after it.
  - An audio bus's rack sees `t +` its own latency. Master sees `t`.
  - Within any rack, an insert sees its rack's time minus the latency of the
    inserts before it (`BlockScope::Shift`).

`MixerModel::processBlock` and `MixerStrip::processBlock` name the block's time
with a thread-local `TransportPlayHead::BlockScope`. Plug-ins query the
playhead from inside `processBlock` on that thread; JUCE's VST3 and AU wrappers
do. A query from another thread, such as an editor, gets the most recently
rendered block, by presentation time only (no frame).

`HostTransport` is one process-wide object, because there is one Dorico. Its
writers take a spin lock. Render threads read it through a two-copy seqcount
latch: while a writer updates one copy, readers take the other. A writer
descheduled mid-update therefore never stalls a render thread.

## Known limits

- **Tempo:** ppq ignores Dorico's tempo changes (above).
- **Note timing:** Fiddle schedules Dorico's notes when they arrive, not at
  their sample position in Dorico's block. The run's start is anchored the
  same way. The playhead agrees with the notes Fiddle plays, but both can be
  up to one Dorico block from the score's exact time. Scheduling notes from
  Dorico's sample positions would fix both; that is separate work.
- **Clock drift:** the position counts Fiddle's stream frames from the start
  and is not re-anchored during playback. Drift between Dorico's audio clock
  and Fiddle's is a few ms over several minutes.
- **No loop or record state.** Dorico doesn't loop playback, and Fiddle never
  claims to record.
- **Fiddle's mute and solo don't stop MIDI.** Strips process MIDI even when
  silenced, so a capturing plug-in on a muted Fiddle layer still captures.
  This is unlike a DAW, where muting a track feeds the plug-in silence.

## Tests

`ProgramStateReplayTest` checks that START carries Dorico's sample rate.

`MixerIntegrationTest` covers this at 1 and 2 render workers:

- "plug-ins follow Dorico's transport" runs with and without strip latency.
  - It checks play state, seconds, samples, bpm, ppq and signature for each
    block before, during and after a run.
  - It checks that the run's first note arrives in a playing block at the
    run's start.
- "the transport keeps runs still to be rendered" restarts before the
  previous run is rendered, starts without a stop, and drops old runs.
- "each insert sees its input's score time" checks the offsets for an insert
  after a look-ahead insert, a chair-bus insert, an audio-bus insert and the
  instrument.
- "positions count stream frames, not clock time" jitters each block's
  presentation time and checks that positions still step by exactly one
  block, with and without strip latency.
- "runs and blocks without frames use presentation time" covers the
  fallback.
- "frames compare only within a render stream" covers a device restart
  during a run, and the stop and next start after it.
