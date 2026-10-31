# Fiddle backlog

This is the prioritized product backlog for making Fiddle a practical end-to-end
stereo mixing environment for Dorico projects. Features already implemented are
documented elsewhere; this list records the remaining work and intended order.

## Expression-map foundation — current focus

- **Orchestral bridge maps — current plan (2026-09-24).** Follow
  [Orchestral bridge maps](orchestral-bridge-maps.md), the authoritative decision
  record. Woodwinds, brass, bowed orchestral strings, pitched percussion, and
  (2026-10-31) unpitched percussion r1 — single instruments, with a one-pitch
  percussion map so the expression map carries everything; drum-set kits, the
  entry keyswitch as a conduit, and library-side percussion maps are later
  work. Other instruments are deferred. Start with the
  hand-built map's 25 bases, then built-in PT add-ons with meaningful mutex
  groups and one CC/KS conduit per dimension. All layers of a chair share its
  section profile. **Supersedes universal-map and chair-specific base-only
  Cartesian generation.** Independent-conduit add-ons passed bounded tests;
  shared-conduit failures are not a general rejection of add-ons.
  **Routing scope (2026-10-15): one Dorico voice per MIDI channel.** Multiple
  voices sharing a channel are outside fidelity acceptance: treat Dorico output
  as practically unpredictable and do not require exact duplication by the bridge.
  Shared-voice overlap/d1/d2 results are historical; further isolation of those
  cases is no longer a prerequisite.
  **Strings curation (2026-10-16):** [r3](../experiments/orchestral-strings-bridge/r3/README.md)
  is the current candidate: 32 add-on dimensions + one base, 33/52 conduits,
  19 spare, 87 add-on switches and 91 distinct encoded PT IDs. Nineteen fringe
  PTs are deferred with explicit reasons; genuine string pizzicato variants
  remain. Historical r2 allocations/captures are frozen. A guitar bridge and
  a structured bridge editor are later work. Source-derived decoding and advanced
  XML revision editing are implemented; r3 host acceptance is pending.
  **Woodwinds/brass (2026-10-16):** [r1 candidates](../experiments/orchestral-winds-bridge/README.md)
  are generated with complete inventories and allocation tables. Woodwinds:
  35 add-on dimensions + base, 36 conduits, 16 spare, 68 encoded PT IDs. Brass:
  36 add-on dimensions + base, 37 conduits, 15 spare, 75 encoded PT IDs. Shared
  PTs have identical encodings across these maps and strings r3. Local checks
  pass; source-derived runtime decoders exist, with host acceptance pending.
  Pitched percussion is the remaining ungenerated section candidate.
  Next: focused Off/cancellation/composition and note-length transition tests
  within one voice per channel; four explicit instrument,
  vocabulary and allocation tables with exact switch/conduit counts; matched
  versioned map/decoder generation and host acceptance; then coordinated
  endpoint/PBT installation. Retain the [built-in inventory](dorico-built-in-technique-groups.md)
  and [historical live evidence](../experiments/dorico-strings/live-results.md).
  The four production profiles are not yet generated or installed.
- **Bridge authoring foundation — implemented (2026-10-16).** The
  [Bridge maps workspace](bridge-map-authoring.md) supports templates, map/bundle
  import/export, advanced XML revisions, versioned assignments, and session-only
  Dorico export checks. Remaining: structured editing, compiled-profile coverage,
  host acceptance, and a portable full-project host snapshot. Chairs template
  installation now uses pinned map IDs in both endpoint and preset XML, installs
  immutable map files, preserves routing, and flags changed pins for reinstallation.
- **Custom PT configuration — second project.** Extend the reviewed built-in
  dimensions where appropriate, with an advanced user-facing
  review/preview/export/assignment workflow, explicit updates, and rollback.
  Read-only vocabulary catalog, current-chair audit, and repair-plan preview/
  export and the chair-sizing analyzer are already implemented; exported plans
  are review JSON, not installable maps. Their allocation/product assumptions
  are not the current generation strategy. Keep them as legacy diagnostics;
  update their UI labels or replace them before presenting section-profile
  readiness or entry estimates. Do not allocate one CC per missing PT. See
  [design and audit limits](dorico-bridge-profiles.md).

## Priority 0 — deliverable audio

- **Validate Dorico offline export.** Detect and report VST3 offline processing.
  Determine whether a later host-driven request/response renderer can support
  Dorico's faster-than-real-time export without shared-memory underruns.

## Priority 1 — complete the conventional stereo mixer

- **Investigate send-alignment regression tests.** The 2026-09-19 full test run
  fails the pre/post-send sample-alignment/device-reprepare and send-path
  continuity assertions in `MixerIntegrationTest.cpp`. Printing/tail regressions
  pass; these two failures still need diagnosis before claiming a clean suite.
- **Pre-fader sends — implemented, awaiting smoke test.** Per-send pre/post tap,
  correct rack-latency compensation, explicit mute/solo behavior, persistence,
  Undo/Redo, and promoted controls in the corresponding strip section. See
  [send smoke tests](post-fader-sends.md#pre-fader-smoke-test).
- **Master and channel metering — implemented.** Independent
  left/right meters, latched resettable clip indicators, and Master true-peak
  plus integrated/short-term LUFS with inline Master readings and manual reset.
  Short playback/UI smoke test passed after the debug-feed responsiveness fix.
  See [metering smoke test](metering.md#smoke-test) for extended checks.

## Priority 2 — finish and deliver complex mixes

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

## Before the first release to other users

- **Dorico's maximum staccato duration (decided 2026-10-29: live with it).**
  Playback Options ▸ **Maximum duration for staccato playback technique** (on by
  default at a quarter note, apparently exclusive) can't be overridden in an
  expression map. With it on, longer staccato notes reach Fiddle as Natural and,
  with the bridge maps' staccato length at 100%, play full length. Turning it off
  is project-wide, so in a mixed playback template (Fiddle first, another
  library as default) the other library gets Staccato on long notes. For now the
  bridge maps stay as they are and Fiddle projects turn the option off. A Dorico
  feature request for an expression-map override is filed. Before a release:
  check whether Dorico added the override; otherwise choose between leaving the
  bridge maps' staccato length to the project (long staccatos then arrive
  shortened, as Dorico plays them) and documenting the limitation. Also decide
  whether Fiddle applies the maximum per layer (a project setting).

## Deferred scope

- Mix automation. Existing score dynamics and CC1/7/11 cover the immediate
  musical needs. Revisit effect/send/parameter automation when a concrete use
  case warrants it; preserve the isolated CC102–119 musical protocol and do not
  appropriate instrument controllers implicitly.
- Unpitched percussion r2: drum-set kits (`instrument.kit.*`, one pitch per
  kit component in the percussion map), the percussion-map entry keyswitch as
  a conduit (Dorico sends it with the hit, velocity 127, same length), and
  library-side percussion maps for driving instruments. Not needed after the
  one-pitch design: stroke × roll combination codes.
- Surround or immersive output.
- Multi-output instruments.
- External audio tracks, recording, comping, and waveform editing.
- Freeze/bounce-in-place and out-of-process plug-in crash isolation.
- Expand the expression-map viewer into a full editor while preserving unknown
  `.doricolib` XML fields and providing validation, Undo/Redo, and safe writes.

## Recently completed

- **Stem printing.** Print selected group/FX buses and optionally Master to
  synchronized stereo WAVs in one real-time pass. Includes sample-cursor loss
  detection and a 0–60 second effect-tail control with knob/text entry. User
  smoke test passed on 2026-09-19. See [stem printing](stem-printing.md).

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
