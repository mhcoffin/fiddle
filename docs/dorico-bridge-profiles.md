# Dorico bridge profiles: requirements and configuration design

## Invariants

- A Dorico chair has **one bridge expression map/profile**. Every Fiddle layer
  in that chair receives the same decoded musical intent. Layers retain their
  different **instrument expression maps** to translate intent to VSTi MIDI.
- Default to one universal profile. Specialized profiles (e.g. strings/winds)
  remain an escape hatch, not a requirement or an automatic split by family.
- Profile requirements are the union of **all layers** in a chair, including
  inactive layers. A profile cannot silently discard another layer's needs.
- Preserve canonical Dorico playback-technique IDs, including custom IDs.
  Display names are labels, never identity or a reason to merge definitions.
- CC102–119 are the musical protocol. Do not appropriate instrument CCs to
  increase capacity. Measure combinations and independent dimensions, not
  just the count of technique names.
- Optional reference collections do not determine requirements and do not
  establish ownership/installation of patches. VSL/Art Conductor alternatives
  are separate sources, not definitions to automatically overwrite or union.

## Implemented first stage: assigned-map audit

Open View → Windows → Technique Catalog → Assigned-map audit (the default).
Refresh snapshots the **actual parsed maps assigned to current project layers**
and the decoder loaded in memory. It does not substitute a newer vendor file
with the same map ID. Each chair lists its union of required identities; each
layer's base/add-on entries are inspected separately. Unmapped layers are shown.
Saved library assignments remain visible in the vocabulary catalog; the new
reachability analysis currently covers current-project layers only.

The worker in `BridgeReachability` is read-only, independent of the mixer and
SQLite, and uses `ExpressionMapData::resolveMatch`, not an audit-only matcher.
The legacy display-to-ID conversion is shared with playback. For the current
unique-dimension CC102 protocol, declared/default CC states are decoded with
default suppression and CC102's modulo-five length category. Unknown CC values
and arbitrary Lua-generated techniques are not part of the search.

For bases, test their technique set; for add-ons, test the union with each base.
Also test Natural as the reset/absent state, since the resolver has a Natural
fallback. Search all compatible decoder states for each such request until a
witness is found. Normalize equivalent value states. Cap work at 4,096 search
nodes per entry / 100,000 per layer map, with cooperative cancellation.

Results deliberately separate evidence from proof:

- **Decoder witness:** an explicit CC state decodes to a request which the real
  resolver selects. Shows CC values, decoded IDs, length category and base.
- **Missing identity:** a required ID is absent from the decoder's non-default
  vocabulary. Accidental fallback selection is not transmission of that intent.
- **Encoding conflict:** no state expresses the tested exact base request,
  even though individual required identities exist. This includes competing
  values on one dimension or unavoidable extra identities.
- **Selection needs review:** the request can be expressed, but its tested
  lengths choose another entry (or none). Other supersets are not exhausted;
  this is **not** a proof of universal unreachability.
- **Uncertain:** unsupported decoder layout/condition, search cap, or an add-on
  without a witness. Do not present a bounded search failure as impossibility.

This does not yet model Dorico's switch selection/transitions, project-local
technique definitions, arbitrary Lua transformations, MIDI action execution,
or whether the instrument responds correctly. Successful selection is not a
claim about all semantics of unsupported expression-map fields. Existing
playback and the bundled Dorico map are unchanged.

## Implemented second stage: repair planning and projected coverage

The **Repair plan** tab compares current decoder witnesses with a proposed
decoder using the same resolver and all current chair layers. The default
proposal preserves exact source identities (case and namespaces) and splits
compound source IDs. This is a simulation only; legacy playback is unchanged.
Source IDs and all reserved controllers mentioned in the loaded bridge XML
are retained as metadata alongside the current decoder. Missing metadata blocks
the canonical preview rather than guessing. Colliding states are listed and
retain their legacy interpretation in the proposal, allowing unrelated states
to be repaired without choosing an alias for the collision.

Eligible missing identities can be selected for independent CC on/off proposals
(127 on, 0 off), allocated deterministically from unused CC102–119 controllers.
Raw reserved-controller uses count even if the decoder doesn't expose them as
a dimension. Existing values are not reallocated. If selections exceed free
controllers, **none** is allocated; the user chooses fewer or defers to a more
efficient encoding design. This is not a measurement of total protocol capacity.

The planner does not guess aliases, inherited definitions, conflicting local
definitions, or the meaning of an identity sharing a reset/default value. Those
items receive concrete review instructions. Reference-only definitions do not
authorize a generated switch. Score-marking counts are inventory, not proof
that a notation is available in a particular Dorico project.

Use **Preview selected repairs** after changing choices. Recalculation runs on
the worker using the existing snapshot, without rescanning files. Coverage
gains and regressions are both shown, including the affected chair/layer/map
entry. A repair for one layer never hides a regression in another. Review rows
retain the underlying audit's limits; no exhaustive reachability or audible
playback claim is made. Refresh the catalog for changed project assignments.

**Export plan (JSON)** opens an explicit Save dialog and exports the reviewed
snapshot only. Changed/unpreviewed choices, running scans and partial results
cannot be exported. The versioned artifact includes proposed decoder states,
chosen additions, affected entries, blockers, coverage changes, and snapshot
time. Its plan ID identifies the review document, not an installed-profile
handshake or cryptographic signature. The artifact has `installable: false`:
it is **not** a Dorico map or an importable Fiddle decoder. No files are written
until the user chooses a destination; no automatic installation occurs.

Matched Dorico map generation, decoder activation, profile persistence and the
assignment/validation workflow below remain to be implemented. Keeping this
boundary explicit prevents exporting a map that the running decoder cannot use.

## Target user-facing workflow

1. **Review requirements.** Start from current chairs; optionally include saved
   libraries for a broader reusable profile. Identify missing identities,
   combinations, alias choices and conflicting vendor definitions. User choices
   are explicit and provenance-preserving.
2. **Configure vocabulary.** Offer sensible defaults with advanced exclusions,
   aliases and custom techniques. Do not import vendor mutex groups wholesale:
   instrument limitations need not constrain the upstream musical vocabulary.
3. **Preview profile.** Prefer a universal allocation; report capacity conflicts
   and loss of coverage before offering specialized chair-level profiles.
   Every layer must be checked against its chair's proposed profile.
4. **Generate a matched pair.** One versioned specification produces both the
   Dorico expression map and Fiddle's canonical-ID decoder. Include profile ID,
   schema version, stable mapping revision/fingerprint and source requirements.
   Do not silently reinterpret CC values when layers or libraries change.
5. **Export and assign.** Explicitly export a new map and guide the user through
   assigning it to the relevant chairs in Dorico. The exported file, installed
   Dorico library and map embedded in a score are distinct states. Show which
   profile/revision Fiddle expects, and never mark Dorico synchronized merely
   because a file was exported. Design a safe verification mechanism separately.
6. **Validate and maintain.** Provide an acceptance score/checklist for individual
   techniques, combinations, resets, mid-passage starts and multiple layers.
   Persist configuration with project history; warn when edits introduce new
   requirements, but regenerate only on an explicit user action. Keep prior
   versions available for rollback and older Dorico projects.

Configuration should be a rarely needed advanced tool, not a new routine step
before playback. Future profile assignment must be keyed by chair and resolved
before layer fan-out; a layer-level override would violate the shared input.

## Verification

`BridgeReachabilityTest` covers defaults/Natural, length-conditioned choices,
shadowed entries, same-controller conflicts, custom-ID loss, add-ons, chair-wide
unions, unmapped layers, cancellation, and unsupported decoders. Witness states
are replayed through the production resolver. Browser fixtures cover default
entry, result/chair/search filtering, layer identity, CC examples and narrow UI.
`ExpressionMapTest` also loads the bundled bridge and replays reported CC states
through `NoteStreamTracker`, checking the actual collected IDs, length and match.
`BridgeRepairPlanTest` checks canonical/compound-ID gains, cross-layer regressions,
default collisions, alias/definition blockers, explicit choices, deterministic
allocation, raw CC reservations and capacity failure without partial allocation.
The catalog worker test checks that re-preview retains the snapshot without a
rescan. Browser fixtures verify choice/preview/export gating and narrow layout.
