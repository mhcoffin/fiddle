# Experimental parallel layer rendering

Audio Performance → Rendering workers offers 1 (default), 2 or 4 total rendering
threads, including the render-ahead coordinator. This is a local database setting
(`audio_render_workers`), not project state. Stop Dorico and disarm printing before
changing it. Startup failure falls back to one worker; unavailable real-time
scheduling falls back to high priority and is reported in the panel.

The coordinator and persistent helpers claim independent strips from a bounded
index range. Each strip has stereo scratch storage allocated when its routing
graph is published, never in the rendering call. Each instrument and its ordered
FX chain runs once per block; MIDI and DSP state are never processed concurrently
for the same instance. Muted strips still advance their processors normally.
Helpers join the coordinator's admitted processing-gate scope. Control requests
cannot skip part of an in-flight block or serialize/destroy a plugin while any
helper is using it. The coordinator pins the routing graph until all helpers finish.

All outputs are summed in original strip order after the helpers finish. Buses,
Master, printing and ring publication then run on the coordinator. The native
Dorico callback never waits for a helper; it consumes the existing audio reserve
or applies safety muting. The coordinator uses completion events, not a busy-spin
barrier. No plugin is forcibly terminated if it stalls. Worker configuration and
device re-preparation happen under the existing control gate, between blocks.

This is not a general parallel graph scheduler: inserts within one strip and the
bus/Master stages remain serial. macOS workgroup integration is not yet enabled;
the helpers request JUCE real-time thread scheduling independently. Plugin-internal
threading, vendor locks, memory bandwidth and wake-up overhead can limit gains.

Audio CPU still measures elapsed time to produce a block, including helper waits.
In parallel mode, plugin load is summed work across threads and can exceed elapsed
load. `parallelRendering` marks those reporting windows and `otherLoad` is null in
JSON, because subtracting overlapping work would be misleading. Per-plugin costs
remain valid. Worker counts and scheduling status are included in copied reports.

## Listening comparison

Keep sample rate, 256-sample buffer, device, score and playback range unchanged.
Compare 1, 2 and 4 workers with printing disarmed. Stop before each switch and
allow the reserve to refill; exclude that transition from counter comparisons.
Use counter differences within each run, not lifetime totals. Capture Audio CPU,
recent peaks, queued reserve, safety-mute frames/episodes and control-gate waits.
Then repeat the best setting with printing armed. Revert to 1 if a plugin behaves
incorrectly. Synthetic tests do not establish performance with vendor instruments.

Coverage includes sample-exact serial/parallel comparison at 64/256/512 frames,
MIDI deadlines, strip and bus routing, FX latency, gain, mute/solo, bypass,
removal/reinsertion and worker-count changes; a concurrent control request tests
completion of every admitted job and summed diagnostic work. The real render-ahead
worker is also exercised in all three modes, including transport-bounded WAV
printing, control pauses, sample-rate/block-size changes and stream remapping.

## September 16, 2026 listening results

The user's 39-instrument Vienna Synchron Player project was tested on a
24-core Mac at 44.1 kHz with a Scarlett 6i6 USB and 256-sample processing.
Single-worker rendering approached or exceeded its time budget and exhausted
the reserve even after playback-only parameter monitoring stopped taking the
control gate.

- Two workers: about 56% median busy-passage load, no new rendering overruns,
  underruns, safety mutes or skipped frames in the supplied two-minute report.
- Four workers: about 30% median busy-passage load; highest recorded block about
  50%. No new overruns, underruns, safety mutes or skipped frames.
- Four workers with recording: busy-passage load remained about 30%; highest
  recorded block about 55%. The report showed no new audio failures, and the
  user confirmed the exported WAV sounded correct.
- Sampled reserve stayed between approximately 52 and 64 ms in all three runs.
- The user subsequently reported clean playback at 128 samples as well. No
  diagnostic report or recording validation at 128 samples was supplied.

These are successful tests of this project, not a guarantee for every plugin or
score. Keep the one-worker fallback and compare cumulative counter differences.
