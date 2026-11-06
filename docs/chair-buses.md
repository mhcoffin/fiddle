# Chair buses

Status: design agreed, 2026-11-06 (see [Decisions](#decisions)). Phase 1
(engine, saving and migration) is implemented; the chair strip (phase 2) and
the lock (phase 3) are not. See [Phase 1 as built](#phase-1-as-built).

## Summary

Every chair gets its own stereo audio bus. All of the chair's layers feed that
bus. The bus has the same tools as an audio bus: inserts, sends, balance,
fader, meter, mute and solo. Its output goes to Master by default, or to an
audio bus such as Strings. The chair fader becomes this bus's fader.

The result is one place per chair to shape its sound: balance the layers, add
chair-wide effects, set the chair's level and send it to reverb.

```text
before                              after
layer ─┬─ output ─▶ Master/bus      layer ─┐
       └─ sends  ─▶ buses           layer ─┼─▶ chair bus ─┬─ output ─▶ Master/bus ─▶ Master
layer ─┬─ output ─▶ Master/bus      layer ─┘   (inserts,  └─ sends  ─▶ buses
       └─ sends  ─▶ buses                       fader)
```

What changes for the user:

- **Layer strips lose their Output selector and sends.** A layer always feeds
  its chair. It keeps its instrument, inserts, balance, fader, mute and solo.
- **A chair strip sits next to the layers.** It replaces today's thin chair
  fader and is shown for every chair, including chairs with one layer (whose
  routing now lives there).
- **The chair fader sets the chair bus level.** Moving it no longer rewrites
  layer gains.
- **The lock stays, as "Keep blend level".** It still lets you rebalance layers
  without changing the chair's loudness (see [the lock](#the-lock-keep-blend-level)).

## Model

### Chair bus

A chair bus is a `GroupBus` owned by a chair, with the same engine
(`StripAudioEngine` rack, pre- and post-fader inserts, balance, gain, meter).
It adds:

- `chairId`: the owning chair. The bus has no name of its own; the UI shows
  the chair's name.
- `directOutputBusId`: empty for Master, or an audio bus ID.
- `sends`: the same `AudioSend` list layers have today, pre- or post-fader.

Its fader runs from −∞ to +6 dB, the same as layer faders (audio buses go to
+12 dB).

Every chair owns exactly one chair bus, created with the chair and deleted
with it. A chair with no layers keeps its bus (silent, not rendered), so its
settings survive removing the last layer and adding another.

Audio buses (the Audio Buses bank) stay as they are and still feed only Master.

### Routing rules

Routing has two tiers, so cycles are impossible and no general cycle check is
needed:

| Source | Direct output | Sends |
| --- | --- | --- |
| Layer | its chair bus (fixed) | none |
| Chair bus | Master or one audio bus | audio buses |
| Audio bus | Master | none |

Nested audio buses (audio bus → audio bus) stay a separate backlog item.

### What moves where

| Today (layer) | After |
| --- | --- |
| `directOutputBusId` | chair bus `directOutputBusId` |
| `sends` | chair bus `sends` |
| inserts, balance, gain, mute, solo | unchanged, on the layer |
| chair fader (computed layer sum) | chair bus gain |
| lock target (`chairLevels`) | blend-level target, same storage |

## The lock: "Keep blend level"

Today the lock holds a target for the summed power of the layers (see
[gain.md](gain.md)). Moving the locked chair fader scales every layer, and
moving one layer compensates its siblings.

With a chair bus, those two jobs separate:

- **Chair level** is the chair bus fader. It is a plain gain after the layers
  are summed. It never changes layer gains, locked or not.
- **Blend level** is the summed layer power, the same quantity as today. When
  the chair is locked, Fiddle holds it at the captured target: changing one
  layer, or muting or unmuting one, compensates the audible siblings exactly as
  [gain.md](gain.md) describes now. That is the "rebalance without changing
  loudness" behaviour, and its algorithm (`lockedGroupGain.js`) is reused
  unchanged.

The blend target is captured when you lock. To raise or lower the whole chair
you use the chair fader, so there is no need to edit the target directly.
Unlocking and locking again re-captures it.

The chair strip shows:

- the lock button, under the chair fader where it is today
- a `Σ` readout of the blend level when the chair has two or more layers
- the amber off-target marker when the blend can't reach the target (for
  example one layer alone exceeds it), as now, but drawn next to the `Σ`
  readout rather than on the chair fader

Existing projects keep their stored targets. The chair bus starts at 0 dB, so
locked chairs sound exactly as before.

Multi-selected layer edits keep today's behaviour: they set the gains directly
and don't redefine locked targets.

## Mute and solo

Each level mutes and solos its own peers:

- **Layer solo** (unchanged): if any layer is soloed, non-soloed layers are
  silent, across the whole mixer.
- **Chair solo** (new): if any chair is soloed, the chair buses of non-soloed
  chairs are silent.
- **Audio bus solo** (unchanged): as today, including solo-safe returns. A
  chair routed to a soloed audio bus keeps its sends to other buses audible,
  just as a layer's do today.

A sound is heard only if it passes every level. Mute at any level silences
everything below it. **Clear Solos** clears all three levels.

## Latency compensation

Each layer renders ahead by its own rack latency plus everything downstream
of it. Today, downstream means its output bus and its sends' buses. After the
change it means:

```text
downstream(layer) = chair bus rack latency + downstream(chair bus)
downstream(chair bus) = max(output bus latency, each send's bus latency
                            − post-fader latency for pre-fader sends)
```

All layers of a chair share the same downstream value, so no delay lines are
needed between the layers and their chair bus. The existing per-route delay
lines (direct output vs. sends) move from the layer route to the chair bus
route, with the same rules, including keeping disabled sends in the latency
topology so that enabling one doesn't retime the dry path.

## Rendering

Per block:

1. Clear every audible chair bus and audio bus input.
2. Render layers (in parallel, as now) and add each into its chair bus.
3. Process each chair bus: inserts, fader, balance, meter. Then add it to its
   output and sends.
4. Process audio buses into Master. Then Master.

An empty chair bus (no inserts, 0 dB, centre) costs a buffer sum and a meter
per block, which is negligible even for 80 chairs. Chair buses with inserts
run on the audio thread in step 3 at first. If profiling shows a need, step 3
can run in the render pool like the layers.

A chair whose layers are all silent still processes its bus so effect tails
decay, as audio buses do now.

## UI

### Chair strip

The chair strip sits to the **right** of the chair's layers, where the signal
goes. It is one layer-strip wide and lines up with the layers section by
section, so every fader still aligns:

| Row | Layer strip | Chair strip |
| --- | --- | --- |
| Header | patch / library | chair name, "Chair" |
| VSTi | instrument | (blank) |
| Pre-fader | inserts, promoted sends | inserts, promoted sends |
| Post-fader | inserts, promoted sends | inserts, promoted sends |
| Output | — (removed) | — (row removed for all strips) |
| Balance | balance | balance |
| Fader | layer fader, meter | chair fader, meter |
| Below fader | gain value | gain value, `Σ` blend readout, lock |
| M/S | M S | M S |
| Bottom | Edit details… | Output selector |

Removing the Output row from every strip gives back about 68 px of height,
which goes to the faders.

The chair strip has its own Audio FX panel (the same panel buses use, with
sends) opened from its Pre-fader and Post-fader sections.

### Selection

Selecting a chair strip selects the chair. Changing the Output of a selected
chair applies to every selected chair, as the layer Output selector does now
for selected layers.

Add Bus's "Route the selected layers" becomes "Route the N selected chairs"
(selecting a layer counts as selecting its chair).

### Audio Buses bank

A bus's "N direct · M sends" now counts chairs. Deleting a bus returns the
chairs routed to it to Master and removes chair sends to it; Undo restores
both.

## Persistence and migration

The project version gains a chair bus record per chair: gain, balance, mute,
solo, inserts (with plug-in state), sends and direct output. Only chair buses
that differ from a new one are saved, so a project that never routed anything
keeps its version hashes. Layer records drop `directOutputBusId` and `sends`.
Lock state stays in project settings (`lockedChairIds`, `chairLevels`).

Opening a version saved before chair buses:

- Each chair's bus starts at 0 dB, centred, unmuted, with no inserts.
- **Output:** if all the chair's layers used the same output, the chair bus
  uses it. If they differed, it uses the most common one (Master on a tie).
- **Sends:** one chair send per destination any layer sent to. If the layers'
  sends to that destination agree, it copies them. Otherwise it takes the
  loudest enabled send's level and tap.
- Layer inserts, gains and lock targets are unchanged.
- Chairs whose routing or sends couldn't be carried over exactly are named in
  a mixer notice and in the Debug log, so you can check them. Nothing is
  saved until you save.

Fiddle doesn't attempt a sample-exact match for mixed routings: one chair bus
can't send only part of a chair.

## Undo

Every chair bus edit (gain, balance, mute, solo, inserts, sends, output) goes
through the existing bus command and undo paths. A locked layer edit and its
sibling compensation remain one undo step, as now.

## Phase 1 as built

- `GroupBus` serves both kinds of bus. A chair bus has `chairKey` (its chair
  ID), `directOutputBusId` and `sends`, ID `chair:<chair ID>`, and a +6 dB
  fader limit. `MixerModel` keeps chair buses apart from the audio buses:
  `getAllGroupBuses()` lists audio buses only, `getAllBuses()` both, and
  `getGroupBus(id)` finds either, so the bus gain, mute, solo, balance and
  insert commands already work on chair buses by ID.
- A chair bus is created the first time a layer of its chair appears and
  lasts until the project is cleared.
- The render graph sums layers into chair buses, then routes each chair bus
  with the delay rules layers used to have. A layer's MIDI lead covers its
  chair bus rack and everything after it.
- **Interim UI, until phase 2:** each layer still shows an Output selector and
  sends, but they read and edit its chair bus. Changing them on one layer
  changes every layer of that chair. Chair fader, inserts, mute and solo have
  no controls yet. Chair meters are in the meter feed as `stereoChairs`.
- Layers without a chair (tests and free-standing strips in very old
  versions) get a private chair bus keyed `strip:<layer ID>`. A restore can't
  match that by ID, so their output and sends stay on the layer record and
  migrate back exactly.
- Migration is `migrateChairRouting` in `ChairBusMigration.h`.

## Implementation phases

1. **Engine and persistence.** Chair buses in `MixerModel` and the published
   graph, layer → chair bus routing, chair bus outputs and sends, latency
   compensation, chair solo/mute, version save/load and migration. Buses have
   no pre-fader send tap today, so the bus engine gains one (layers already
   have it). C++ tests
   cover routing, the solo matrix, latency alignment using a test processor
   that reports latency, and migration of mixed routings.
2. **Chair strip UI.** The chair strip, removal of the layer Output row and
   layer sends, the chair Audio FX panel, selection, and Add Bus routing
   chairs. Browser tests cover fader alignment, chair strips beside one and
   several layers, and output changes on selected chairs.
3. **Lock.** Move the lock to blend level, add the `Σ` readout and marker on
   the chair strip, and update [gain.md](gain.md).
4. **Docs and smoke test.** Update [post-fader sends](post-fader-sends.md),
   [stem printing](stem-printing.md) and the [backlog](backlog.md), then a live
   test with Dorico playback: routing, sends, solo combinations, latency with
   a look-ahead limiter on a chair bus, and opening an older project.

Phase 1 is the largest and riskiest (render graph, latency, migration).
Phases 2 and 3 are mostly UI.

## Later, not in this design

- Chair buses as stem-printing sources.
- Collapsing a chair strip to a narrow tab, if the extra width on one-layer
  chairs proves a problem.
- Audio bus → audio bus routing (nested buses, in the backlog).

## Decisions

Agreed on 2026-11-06:

1. **Chair strip position:** to the right of the layers.
2. **Chair fader range:** tops out at +6 dB, like layer faders, so levels are
   easy to compare.
3. **One-layer chairs:** every chair gets the full-width chair strip for now,
   because it's simplest. A narrower form can come later (see
   [Later](#later-not-in-this-design)).
