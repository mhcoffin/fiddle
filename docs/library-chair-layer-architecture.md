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

