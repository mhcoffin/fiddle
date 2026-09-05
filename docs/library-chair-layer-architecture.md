# Library, chair, and layer architecture

Status: accepted for implementation, September 2026.

## Problem

The original Library Manager combines three separate concerns:

1. cataloguing the sounds available in a sample library;
2. deciding which instruments and chairs Dorico should address; and
3. creating the mixer strips that layer those sounds.

That model cannot naturally route a solo patch to a section chair or create
more than one layer from the same library on a chair. It also makes ordinary
library editing rebuild the Dorico playback template.

## Terms

### Library patch

A reusable description of one available sound. It belongs to a sample library
and records a stable patch ID, display name, canonical instrument, optional
descriptive character, VST instrument, default plug-in state, and default
expression map. A patch does not select a Dorico destination.

### Chair

A stable MIDI source exposed to Dorico. A chair records a stable chair ID,
Dorico instrument identity, solo-or-section destination, ordinal/display name,
family, display order, and MIDI port/channel assignment. A chair may exist
without layers.

"Chair" is an internal and UI shorthand for a Dorico-addressable instrument
destination; a string section such as Violin I is included even though it is
not literally one player's chair.

### Layer

An independent assignment of a library patch to a chair. A layer owns a stable
layer ID and its mixer state: activation, mute, solo, gain, plug-in instance
state, expression-map override, Lua processors, inserts, sends, and output
routing. Multiple layers on one chair receive the same incoming MIDI.

## Required relationships

- A chair may have zero or more layers.
- A library patch may be instantiated on zero or more chairs.
- A chair may contain several patches from the same library.
- A patch described as solo may be assigned to a section chair, and vice versa.
- Assigning one patch to two chairs creates two independent run-time plug-in
  instances and two independently mixable layers.

For example:

```text
Violin I
|- Elite Violins I
|- Hollywood Strings Violins II
`- Synchron Solo Violin 1

Solo Violin 1
`- Synchron Solo Violin 1 (a separate layer and plug-in instance)
```

## Identity and persistence invariants

- Patch, chair, and layer IDs are immutable UUIDs.
- A layer is identified by its layer ID, never by a mutable label or by the
  `(port, channel, library)` tuple.
- A chair retains its MIDI assignment across display reordering and restarts.
- Deleting and re-adding the same Dorico destination may reclaim its prior MIDI
  assignment from the channel graveyard.
- Branch/version state includes the chair roster, including empty chairs, and
  the complete ordered set of layers.
- Restoring a saved Dorico project recreates its saved Fiddle state when the
  referenced version remains available.
- A saved layer remains restorable when its catalog patch has subsequently
  been removed. It is shown as a missing catalog reference instead of being
  silently discarded.

### Version snapshot implementation

Versioned Fiddle state carries a schema-versioned routing snapshot alongside
the master-audio state and content-addressed strip blobs. The routing snapshot
stores the ordered chair roster, stable MIDI assignments, immutable layer IDs,
patch references, and fallback patch/library labels. Each layer points to its
strip blob rather than duplicating opaque VST state.

Versions made before this schema remain readable through the legacy strip
restore path. When such a version is the head of a checked-out branch, Fiddle
marks it dirty once so the next Fiddle or Dorico save records its current
chair/layer topology. Catalog rows are not versioned: restoring a layer whose
patch was removed retains the patch ID and saved labels and identifies it as a
missing catalog reference.

## Template boundary

The Dorico playback template is derived only from chairs. Adding, removing, or
changing the identity of a chair marks the installed template out of date.
Adding, removing, configuring, activating, or mixing layers does not require a
template rebuild.

Reordering chairs for display must not silently renumber MIDI assignments.
Channel compaction is an explicit exceptional operation because existing
Dorico projects may retain the old assignments.

## Library-default behavior

Assigning a patch creates an independent layer initialized from the patch's
VST instrument, saved state, and expression map. Later catalog edits do not
silently rewrite an existing layer. The mixer may offer an explicit
"Reset from Library Patch" operation.

The Library Manager must use a dedicated preview host. Previewing or editing a
catalog patch must not create a persistent mixer layer.

## Deletion behavior

- Removing a layer does not remove its chair or catalog patch.
- Removing a chair also removes its current layers and requires explicit user
  confirmation; it marks the Dorico template out of date.
- Removing a referenced catalog patch is blocked until its layer assignments
  are removed or replaced. The UI reports the number and names of references.
- Removing a library follows the same referenced-patch rule.

## Mixer presentation

The mixer presents `family -> chair -> layers`. Its existing groups inferred
from shared MIDI input become explicit chair groups. Chair headers provide
large Add Layer, edit, and delete affordances. Each child layer keeps the
existing strip-level mixing and processing controls.

Library activation remains a convenience operation over all current layers
from that library; it is not part of layer identity.

## Deferred plug-in preset diagnostics

Fiddle asks hosted VST instruments for their current program and program name,
but many modern instruments manage presets entirely inside their own editor and
expose only opaque state to the host. In that case Fiddle cannot reliably tell
whether the intended sample-library patch is actually loaded. The Library
Manager patch name describes the expected configuration; it must not be
presented as proof of the plug-in's current internal preset.

A future UI/diagnostics pass should:

- display the current program/preset name whenever the plug-in reports a
  meaningful one, including plug-ins that expose only one named program;
- avoid inventing a preset name when the plug-in does not report one; and
- consider a non-alarming "MIDI received, no audio output" diagnostic to help
  identify empty or incompletely configured instrument instances. Such a
  diagnostic needs timing and level thresholds that avoid false warnings for
  rests, silent articulations, muted paths, and deliberately quiet patches.
