# Fiddle backlog

This is the prioritized product backlog for making Fiddle a practical end-to-end
stereo mixing environment for Dorico projects. Features already implemented are
documented elsewhere; this list records the remaining work and intended order.

## Priority 0 — deliverable audio

- **Validate Dorico offline export.** Detect and report VST3 offline processing.
  Determine whether a later host-driven request/response renderer can support
  Dorico's faster-than-real-time export without shared-memory underruns.

## Priority 1 — complete the conventional stereo mixer

- **Pre-fader sends — implemented, awaiting smoke test.** Per-send pre/post tap,
  correct rack-latency compensation, explicit mute/solo behavior, persistence,
  Undo/Redo, and promoted controls in the corresponding strip section. See
  [send smoke tests](post-fader-sends.md#pre-fader-smoke-test).
- **Master and channel metering.** Add independent left/right meters, persistent
  resettable clip indicators, and Master true-peak plus integrated/short-term
  LUFS measurements.

## Priority 2 — finish and deliver complex mixes

- **Mix automation.** Automate at least fader, stereo balance, mute, send level,
  and plug-in bypass against the Dorico timeline. Investigate whether Dorico CC
  or VST parameters can provide a useful first control path before building a
  full automation-lane editor.
- **Stem printing.** Print selected group buses and the Master to synchronized
  WAV files in one real-time pass, with clear names and identical start times.
- **Input trim and gain staging.** Add a trim before pre-fader inserts so the
  level driving compressors and saturation is independent of the mix fader.
- **General link/VCA groups.** Support arbitrary temporary or persistent linked
  fader groups beyond the existing chair-layer level lock.

## Priority 3 — advanced routing and delivery

- Nested group buses with cycle prevention.
- Effect sidechain routing.
- Host-driven faster-than-real-time export, if the Dorico offline-processing
  experiment proves the architecture practical.
- Optional stereo-width/dual-pan utilities and more detailed phase/correlation
  diagnostics.

## Deferred scope

- Surround or immersive output.
- Multi-output instruments.
- External audio tracks, recording, comping, and waveform editing.
- Freeze/bounce-in-place and out-of-process plug-in crash isolation.
- Expand the expression-map viewer into a full editor while preserving unknown
  `.doricolib` XML fields and providing validation, Undo/Redo, and safe writes.

## Recently completed

- **Stereo balance.** Channels and group/FX buses have smoothed balance
  controls, persistence and Undo/Redo. See
  [balance behavior and smoke test](stereo-balance.md). Stereo width or dual-pan
  controls remain deferred.

- **Aux/FX buses and post-fader sends.** Shared effects with independent send
  levels, enable/destination, latency compensation, solo-safe returns, tails,
  persistence, and Undo/Redo. Sends can be promoted to aligned mixer-strip
  sections with a compact knob/readout and destination arrow.

- **Real-time Master printing.** Record Fiddle's final stereo Master output to
  a 32-bit floating-point WAV while Dorico plays normally. Printing is armed
  from Fiddle and gated sample-accurately by Dorico's latency-compensated
  transport boundaries. Low-priority disk writing runs behind a bounded FIFO,
  queue overflow is visible, and stopping drains the queue and finalizes the
  WAV without periodic header rewrites during playback.
