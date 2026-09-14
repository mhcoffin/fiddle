# Lightweight audio diagnostics

The current experimental engine uses [host-driven render ahead](audio-render-ahead.md).
Its Audio performance panel shows queued/target reserve, effective playback delay,
and skipped frames. Both the server and native plugin must now be updated together.
Historical comparison sections below describe the earlier callback-driven engine.

The main mixer toolbar's **Audio CPU** button reports render wall time divided
by audio block duration. At 48 kHz, a 480-frame block has 10 ms to complete; 5 ms
of work is 50%, and 12 ms is 120%. It is not whole-machine CPU utilization and
does not show how many cores are busy. It includes waiting inside instruments.

Click the button for a generously sized **Audio performance** panel. Its Close,
Mark crackle and Copy report controls remain accessible while the detail area
scrolls. Escape closes the modal and returns focus to the toolbar.

## Using it for a listening test

1. Build/restart FiddleServer. Install the rebuilt Fiddle native VST3 while Dorico
   is closed, then reopen Dorico and the test score. Version-1 native plugins
   cannot consume the new version-2 audio stream.
2. Open Audio CPU and check that the Dorico return fields show numbers, not an
   unavailable/stale warning. Confirm that sample rates agree.
3. Loop the score. Priming silence is excluded from underrun-silence counts. Note the
   starting counts: measurements are cumulative, not reset for each transport
   start or loop boundary.
4. When a crackle occurs, click **Mark crackle** as soon as convenient. The button
   records local UTC time and the latest received counters; it does not claim
   a sample-accurate timestamp. Do not attach a thread profiler during this test.
5. Shortly afterward click **Copy report** and paste the report into the task.
   It contains up to 120 observations (at most one per second) and ten listening
   marks. UI stalls can leave gaps in observation times. Copying is explicit;
   nothing is uploaded or automatically written to disk.

## Reading the numbers

- **Audio CPU / recent peak:** average work/budget and largest single-block
  percentage in the latest approximately 250 ms of processed audio. Values over
  100% are not clamped. Stopped, missing and stale are shown explicitly.
- **Highest block load / longest render:** lifetime maxima, retained across
  audio-device restarts. Server restart creates fresh counters.
- **Deadline overruns:** completed renders whose measured work exceeded their
  block duration. With a reserve this does not necessarily cause an audible gap.
- **Long callback gaps:** starts separated by more than 1.5 times the previous
  block's duration. A clue to scheduling/overload, not a precise driver xrun
  count. Hidden for the render-ahead worker, whose waits for consumption are intentional.
- **JUCE device xruns:** JUCE's device/native and load-measurer accounting. It can
  overlap the separate overrun counter; do not add the counts together.
- **Return-ring overflow blocks:** server blocks rejected because the shared
  ring lacked space. Ring-unavailable blocks are counted separately.
- **Underrun episodes:** the native consumer could not satisfy a block. It counts
  consecutive silent blocks as one episode, advancing the cursor through them.
- **Safety-mute episodes/frames:** low reserve latches silence until the reserve
  is effectively full. `minimumQueuedFrames` records the lowest callback-time
  occupancy seen by the Dorico plugin. A 10-ms fade follows recovery.
- **Recovery silence** (JSON `bufferingFrames`): frames replaced by silence during
  low-water/underrun recovery, excluding initial priming. **Unavailable-ring
  silence** is separate. These do not
  count silence genuinely produced by instruments or the score.
- **Dropped outgoing MIDI events:** native relay queue overloads.
- **Dropped diagnostic reports:** a stalled UI filled the fixed reporting queue.
  Audio never waits for room. Cumulative totals survive discarded reports;
  individual short-window observations can be lost.

Return counters last for the native processor object's lifetime; recreating it
resets them. They are independent atomic reads, so a report can briefly straddle
a callback. Return telemetry is labelled stale after three seconds without a
fresh report. A connected but stopped audio host may continue to report unchanged
counts. Neither telemetry freshness nor an empty ring alone proves a dropout.

## Implementation boundaries

The render recorder has one writer and a preallocated 256-packet SPSC queue.
Callbacks do arithmetic and two monotonic clock reads (one replaces the existing
clock read), publish approximately four small packets per second, and never log,
allocate, query the database, serialize JSON, or wait for the UI for diagnostics.
The message thread drains a bounded number of packets and sends a compact message
only to the main mixer at most four times per second.

The native consumer increments lock-free atomic counters. A provider on the
existing TCP relay thread serializes them at most once per second. The server
handles these as telemetry, not MIDI or project edits. The shared audio-ring ABI
is now version 2. The previous callback-side diagnostic logging path was removed.

## Meter-only refresh comparison

The subsequent meter-refresh change sends only strip/bus IDs and atomic peak/hold
levels plus the Master peak, approximately every 60 ms, to the main mixer. It
permits only one meter evaluation in flight, skipping ticks while the web view
is busy. Meter data is separate from configuration in Svelte, so chair grouping
and plugin lists are no longer rebuilt on meter ticks. Configuration still updates
on edits, loading, undo/redo, editor visibility and observed latency changes.

The pre-change listening report (September 9, marks at 18:42:58–18:44:26 PDT)
showed 75–82% load at seven marks, five deadline overruns, 26 long callback gaps
and eight return underrun episodes by the end of the report. All observed blocks
were 512 frames at 44.1 kHz on both sides; no MIDI queue drops or ring overflows
were reported. Counts are cumulative and mark timestamps lag actual audio events.

For comparison, restart **FiddleServer only**, leave the score, instrument count,
audio settings and listening procedure unchanged, and repeat the same passage.
The installed native plugin does not need another update for the meter change.
Native return counters may retain their previous totals: compare their increase
during the new run, not the absolute number. Copy the new report promptly so its
rolling history still covers the glitches.

Audio processing, block handling, clocking and gain behavior were not changed by
this UI optimization. Short-block correction, synchronization repairs and
clock-drift compensation remain separate work from the real-time audit.

## Release-build comparison

The meter-only follow-up (September 9, 19:05:45–19:07:36 PDT) still showed
eight new return underruns (9 → 17), eight deadline overruns and 27 long callback
gaps. Load at the seven listening marks was 74–84%; the longest render was
12.97 ms against a 512-frame/44.1-kHz budget of 11.61 ms. There were no reported
MIDI drops or ring overflows. The runs cover different intervals, so these totals
do not establish a regression, but they do not demonstrate a resolved dropout
problem either.

The next controlled variable is the server build configuration. The existing
`build` directory is Debug (`-g`, without optimization). A separate
`build/release` directory is configured with `CMAKE_BUILD_TYPE=Release`
(`-O3 -DNDEBUG`), using the same dependency sources already in `build/_deps` via
`FETCHCONTENT_SOURCE_DIR_*` overrides and disconnected fetching. Debug artefacts,
the installed Dorico plugin, audio settings and project data are not replaced.

Once that build and its tests have finished:

1. Stop Dorico playback and save any wanted changes. **Quit** FiddleServer;
   Restart would reopen the same Debug app rather than switch builds.
2. Launch the specific Release app (no compilation or plugin installation):

   ```sh
   open /Users/mhc/fiddle/build/release/FiddleServer_artefacts/Release/FiddleServer.app
   ```

3. Leave Dorico open and let its existing Fiddle plugin reconnect. Repeat the
   same passage, instrument count and audio settings. Note the initial return
   underrun count, mark glitches and copy the report promptly. Do not compile
   or run regression tests during the listening comparison.
4. To go back, quit Release and open
   `/Users/mhc/fiddle/build/FiddleServer_artefacts/Debug/FiddleServer.app`.

Restart within the Release app continues using Release. `./fiddle.sh` still
builds/launches the original Debug configuration and also reinstalls the native
plugin; do not use it for this comparison.

To rebuild and test the already-configured Release directory:

```sh
cmake --build /Users/mhc/fiddle/build/release --target FiddleServer FiddleTests --parallel 6
ctest --test-dir /Users/mhc/fiddle/build/release -L stable --output-on-failure
```

The automated tests use isolated fixtures; they do not establish that the live
Dorico/vendor-plugin workload is glitch-free. This comparison changes compiler
optimization and Debug-only checks, not the audio processing design.

Validation (September 9): the separate Release server and `FiddleTests` built
successfully, and all 48 then-existing stable CTest entries passed. The subsequent
live Release listening comparison is summarized below.

## Audio settings and per-plugin timing

The Release listening follow-up still produced seven new underruns (18 → 25),
six overruns and 27 long gaps. Its maximum render was 15.39 ms; higher-load
sample median was about 70% versus about 76% in the preceding Debug run. These
are not precisely aligned recordings and do not establish a measured speedup.

Audio Performance now identifies the build configuration and actual audio device,
sample rate and buffer size. **Audio Settings…** opens a native JUCE device
selector, also available in the **View** menu. Stop playback before changing a
setting: changes apply immediately, may briefly interrupt audio, and re-prepare
the instruments and all FX racks. Only the device's supported settings are shown.
The connection does not resample; keep Fiddle and Dorico at the same sample rate.

Explicit choices are stored in
`~/Library/Application Support/Fiddle/audio-device.xml`, independently of the
database, project versions and undo. With no saved choice, startup still uses
the default device. An unavailable saved device falls back to the default and
shows a diagnostic warning without overwriting the saved choice. A malformed
file is also preserved until the user makes a new choice. Settings are written
through a temporary file, not directly over the existing file. Opening the window
again brings it forward; it does not toggle it closed.

The processing table covers each loaded instrument and strip/bus/Master effect.
It displays recent average and peak milliseconds, lifetime maximum, and observed
processing block size. Bypassed/unprocessed entries show no recent measurement.
Timings include time spent waiting inside a vendor plugin. Each runtime uses a
bounded 32-packet queue; audio never waits for the message thread. Timing capture
does not query plugin programs, parameters or serialized state. Queues publish
after about 250 ms of processed audio. Dropped reports and cumulative call counts
are retained in the copied report; per-runtime maxima reset when it is replaced.

The top-level callback separately sums time inside instrument/effect calls and
reports the remainder as **other Fiddle work**. The latter includes graph,
mixing, MIDI and diagnostic overhead. This is one callback-local measurement;
do not reconstruct it by adding independent per-plugin peaks. Callback timing
also records the last long start-to-start gap and the render immediately before
it, helping distinguish a long preceding render from late callback scheduling.
There is at most one diagnostic web-view evaluation in flight, so a stalled UI
cannot create an unbounded JavaScript evaluation backlog.

### Next listening comparison

1. Restart the rebuilt **Release** server; no native-plugin reinstall is needed.
   Confirm Audio Performance says Release and shows the expected device/rate.
2. Keep the existing 512-frame setup for one same-score pass with the new timings.
   Mark glitches and copy the report promptly.
3. Stop playback. In Audio Settings select 1024 frames, if offered; leave device,
   sample rate, score and plugin-private settings unchanged. Verify the displayed
   actual buffer size and Dorico's sample rate before playing. Shared hardware
   settings may also affect Dorico, so check its settings rather than assuming.
4. Copy the second report after a comparable passage. Counters may survive a
   device reconfiguration: compare increases, not lifetime totals. For fresh
   server maxima, restart Fiddle after changing the setting; the explicit choice
   should survive. Native return counts still have their own lifetime.

Validation: the updated Release server builds; all **50 stable CTest entries**
pass. New tests use an isolated fake device for preference restoration and
missing-device fallback, and production mixer paths for timing and buffer/rate
propagation through instruments and all FX locations. The optional rendered UI
regression exercises a 30-plugin table, fixed dialog actions, copying the report
and opening settings. Live device switching and vendor behavior still require
the listening comparison; the automated tests do not use the real audio device.
