# Host-driven render ahead (experimental)

The server's device callback no longer runs the mixer. A dedicated worker renders
complete blocks into a version-2 shared ring, paced by the Dorico plugin's sample
consumption. The native audio callback only copies available samples or clears
its output; it never waits for the worker, UI, a mutex, or a condition variable.
The worker waits briefly when the reserve is full and refills immediately when
more than one block is needed. It requests JUCE real-time scheduling, with a
high-priority fallback reported in Audio performance.

## Reserve and musical time

The initial policy targets at least 60 ms, rounded up to complete server blocks,
with a minimum of two blocks and a maximum of half the 16,384-frame ring. At
44.1 kHz, both 512- and 1,024-frame processing use 3,072 frames (69.7 ms). This is
an experimental safety margin, not a guarantee or a magic 10-ms threshold.

Dorico publishes a coherent sample-position/monotonic-time pair on each pull.
The worker projects each block's presentation time from that pair, rather than
using its wake-up time. MIDI is scheduled against presentation time. A requested
1,000-ms delay remains 1,000 ms: approximately 70 ms becomes completed audio,
leaving approximately 930 ms for MIDI lookahead/processing (less FX latency).
The previous extra 40-ms subtraction has been removed.

If the requested delay is too short, the effective delay reported to Dorico is
clamped to reserve plus the longest compensated FX path. Both values appear in
Audio performance. This does not add new compensation for instrument-internal
latency. Live mute, gain, editor changes and stop messages cannot change samples
already rendered: their audio response can be delayed by the queued reserve.

## Failure and lifecycle behavior

- A short rendering stall spends the reserve; spare processing capacity refills
  it. Sustained load above real time will still underrun.
- On underrun Dorico emits a silent block and advances its sample cursor. Missing
  audio is not replayed late. The worker skips an obsolete unrendered interval;
  this cannot reconstruct missed instrument/effect evolution. Skipped frames and
  underrun silence are observable counters.
- Priming is silent audio, not an underrun. A stopped host leaves a bounded full
  reserve; rendering does not run arbitrarily far into the future.
- Device reconfiguration stops and joins the worker before preparing processors.
  A fresh mmap inode and stream ID are published for each generation. Old readers
  finish on their old inactive mapping; cursor resets never race those readers.
- Sample rates must agree. A mismatch produces silence, not wrong-speed audio.
- Only a connected native instance with the advertised stream ID consumes audio.
  Server and native plugin must both be updated; version-1 binaries cannot use
  this handoff. No database migration is involved.

This isolates rendering from CoreAudio callback cadence; it does not remove
vendor-plugin locks, make arbitrary state capture safe, provide offline export,
or guarantee freedom from OS scheduling stalls. Callback-time clock observations
also carry host scheduling jitter. The remaining real-time audit still applies.

## Automated coverage and listening check

`render_ahead` uses virtual host time and injected processing stalls to check
reserve refill, variable host blocks, bounded production, starvation recovery,
sample continuity and a concurrent producer/consumer transfer. `audio_diagnostics`
checks native silence/episode accounting. `mixer_integration` runs the actual
worker with deterministic instruments and an isolated mmap, verifies a note
starts at one second independently of reserve, verifies note-off, and restarts
at another rate/block size while an old mapping remains open.

For listening, close Dorico, install the rebuilt native plugin, and restart the
Release server before reopening the unchanged score. Keep 1,024-frame settings
for the first comparison. Check Audio performance for protocol warnings, matching
sample rates and approximately 70 ms target reserve. Play/loop for several
minutes, exercise the UI and stop/restart mid-note. Copy the diagnostic report;
compare increases in underruns/skipped frames, not lifetime totals. A subsequent
512-frame comparison should change only that setting, with playback stopped.

## September 9 listening results and remaining issue

The same 36-instrument/43-plugin setup was tried at 1,024, 512 and 256 frames.
The user heard no glitches at any setting. The 1,024-frame report covered 123
seconds with no render overruns or return underruns. The 512-frame playback
window covered 126 seconds with four render overruns but no return underruns or
skipped frames; sampled reserve was 58–70 ms. Reconfiguration produced temporary
unavailable-stream silence, which did not continue afterward.

The 256-frame follow-up retained only a low-load window, not the detailed active
playback interval. Cumulative render overruns had risen from 4 to 313, while
return underruns and skipped frames remained zero. This supports reserve
effectiveness but is not a complete sustained-load measurement.

An unresolved UI freeze occurred with the Audio performance panel open and
subsequently recovered. Live stack samples showed a responsive native event loop,
ongoing rendering, and a busy WebKit content process executing JavaScript.
Diagnostic samples continued arriving roughly once per second. Investigate UI
event/log traffic and diagnostics rendering; the exact cause is not established.
Do not treat this audio improvement as resolution of that separate UI issue.
