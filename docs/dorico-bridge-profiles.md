# Dorico bridge profiles: existing tools and retained design constraints

## Implemented authoring foundation (2026-10-16)

The [Bridge maps workspace](bridge-map-authoring.md) stores immutable Dorico XML
revisions, derives decoders directly from them, and versions per-input assignments.
Import/export and advanced XML editing are available. Dorico-export comparisons
are explicit user-attested checks; they do not inspect live host assignments.
The legacy audits described below have not been adapted to these compiled maps.
Chairs template installation now installs the pinned map files and uses their IDs
in both endpoint and preset XML, preserving physical routing. Production host
acceptance and structured editing remain future work. This section supersedes implementation-pending statements
below for source-derived decoding, persistence and explicit assignment only.

## Current direction: four orchestral section profiles (2026-09-24)

The authoritative plan is [Orchestral bridge maps](orchestral-bridge-maps.md).
Create profiles for woodwinds, brass, bowed orchestral strings, and pitched
percussion using the hand-built map's 25 bases plus appropriate built-in PT
add-ons, grouped into musical dimensions. One conduit belongs to each dimension.
Unpitched percussion, other instrument profiles, and custom PTs are deferred.

**Superseded:** universal all-built-in generation and the 2026-09-23 proposal to
generate chair-specific base-only Cartesian products. The blanket assumption
that upstream add-ons are unreliable is withdrawn: independent-conduit add-ons
worked in our bounded tests; specific shared-conduit schemes failed. The
[chair coverage document](chair-coverage-algorithm.md) records the old sizing
analyzer, not the current generation algorithm. Its product count is not the
new profiles' entry count. Off/reset ordering remains a focused test gate.

Generate bridge maps, matched decoders, endpoint configurations, and an ordered
playback template as an immutable versioned package. The user applies the PBT
in Dorico; preserve the established chair allocation order across updates.
An added layer that fits existing coverage should not require a Dorico update.
Old projects retain their package rather than having its artifacts overwritten.

The focused r6 live checks have passed with an explicit vibrato return after
the temporary non-vibrato span. Automatic restoration without that marking did
not work in the tested score/map. This is bounded experimental evidence, not
production-profile or installer acceptance; see the live-results document below.

## Historical work and retained evidence

Develop the built-in Dorico vocabulary and its musical mutual-exclusion
dimensions first; user-defined/vendor techniques are a separate second project.
The [built-in inventory and proposed grouping table](dorico-built-in-technique-groups.md)
covers the 298 definitions in the installed Dorico version, retaining uncertain
combinations and reset/alias questions explicitly. It is a review draft, not an
implemented protocol or an installable map.

The earlier
[Strings allocation and isolated Dorico A/B experiment](../experiments/dorico-strings/README.md)
records an 18-CC draft budget, separate versus packed add-on probes, matched offline
decoder tables, and a capture checklist. These probes are incompatible with
normal server launches. Strings F r6 now has live host captures and an explicit,
session-only server opt-in for controlled acceptance; it is not activated or
automatically assigned to any project. See [live results and remaining gates](../experiments/dorico-strings/live-results.md).
Old “next test” instructions are historical. Use the current plan's work order;
host validation still precedes any production rollout.

The audit and repair planner described below remain implemented diagnostic
tools. Their independent-CC additions are not the chosen architecture for the
built-in vocabulary. Review musical dimensions before controller allocation,
then use assigned-map coverage as a regression check rather than allowing vendor
maps to define the upstream protocol. The advanced configuration workflow below
remains relevant to the later custom-technique project.

## Invariants

- A Dorico chair has **one bridge expression map/profile**. Every Fiddle layer
  in that chair receives the same decoded musical intent. Layers retain their
  different **instrument expression maps** to translate intent to VSTi MIDI.
- Default to one of the four reusable section profiles, explicitly assigned
  to the chair. Do not regenerate a map for each layer combination.
- Coverage checks include **all layers** in a chair, including inactive layers.
  A section profile cannot silently discard another layer's needs; unsupported
  or custom requirements remain visible without automatically expanding scope.
- Preserve canonical Dorico playback-technique IDs. Custom IDs remain visible
  diagnostics but are deferred for generation. Display names are not identity.
- CC102–119 remain the reserved CC budget. Profile-specific keyswitch conduits
  are an experimentally tested option with explicit musical-range reservations;
  never globally steal musical notes or appropriate other instrument CCs.
  Measure combinations and independent dimensions, not just technique names.
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

## Legacy diagnostic: repair planning and projected coverage

This tool remains implemented, but its independent-CC proposals are **not the
current section-profile allocation plan**. Descriptions below document behavior,
not instructions to apply its proposals as the next development step.

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

The separate Bridge maps workspace now implements source-derived decoding,
persistence, explicit assignment and export verification. Repair-plan JSON remains
a diagnostic document and is not accepted as a bridge map.

## Retained target assignment/versioning workflow

The authoritative next implementation steps are in
[the current plan](orchestral-bridge-maps.md#next-work-in-order). The following
describes the intended eventual production workflow, not already available UI.

1. **Select a section profile.** Assign woodwinds, brass, orchestral strings,
   or pitched percussion to each chair. Report requirements outside its scope.
2. **Review vocabulary and allocation.** Use curated built-in dimensions, not
   vendor mutex groups wholesale. Report exact switch/conduit counts. Advanced
   custom vocabulary configuration is deferred to the later project.
3. **Check chair coverage.** Check every layer against that section profile,
   distinguishing missing input identities from downstream library fallbacks.
   The old Cartesian analyzer does not calculate the new map's size.
4. **Compile a matched pair.** The immutable Dorico expression-map document is
   the source for Fiddle's canonical-ID decoder. Include profile ID,
   schema version, stable mapping revision/fingerprint and source requirements.
   Do not silently reinterpret CC values when layers or libraries change.
5. **Install the coordinated package.** Generate immutable bridge maps, decoder
   specifications, endpoint configurations, and the ordered playback template.
   Guide the user through restarting Dorico when required and applying the PBT.
   The exported files, installed library, and maps embedded in a score remain
   distinct states. Never mark Dorico synchronized merely because files were
   written; design a safe verification mechanism separately.
6. **Validate and maintain.** Provide an acceptance score/checklist for individual
   techniques, combinations, resets, mid-passage starts and multiple layers.
   Persist configuration with project history; warn when edits introduce new
   requirements, but regenerate only on an explicit user action. Keep prior
   versions available for rollback and older Dorico projects.

Configuration should be a rarely needed advanced tool, not a new routine step
before playback. Future profile assignment must be keyed by chair and resolved
before layer fan-out; a layer-level override would violate the shared input.

## Program numbers and optional profile selection (design only)

Program numbers already have a role in the bridge: instrument identification.
Do not introduce a competing interpretation as arbitrary modifier bits. A future
family/instrument profile can instead be associated with that existing identity.
No program assignments or runtime behavior have been changed for this design.

Current implementation evidence:

- `FiddleController` exposes 128 programs per logical channel across 16 ports
  of 16 channels. Raw numbers are 0–127; user-visible numbering must be explicit.
- `FiddleProcessor::process` takes the **last point** of each program parameter
  queue per block and forwards a timestamped event with port and channel. This
  is not currently a lossless channel for many sequential commands in one block.
- `ProgramStateReplay.h` supports replaying saved program identity on reconnect.
- `InstrumentMapper` resolves names from Dorico's Fiddle `presets.xml`;
  `MainComponent` uses them for the UI. Its assignment cache and that UI message
  currently key by channel alone, not port/channel. Do not reuse this as a
  multi-port profile registry without fixing that distinction.

Proposed lookup: endpoint + port + channel + program → explicit instrument
identity → configured profile ID/revision. Use a stable registry, not guessed
General MIDI numbering or fuzzy display-name matching. A program number alone
does not prove which expression-map revision is assigned in Dorico; validation
and persistence of the matched map/decoder pair are still required.

Resolve the profile before chair layer fan-out. All layers retain one upstream
profile, chosen to cover their combined needs. Program changes are not a way
to switch one layer independently to another upstream map.

If a profile reserves command notes, consume them and their releases before
musical-note processing only under an explicitly configured matching profile.
Never globally steal those pitches. Unknown/mismatched experimental profiles
must be reported rather than decoded by guessing; legacy assignments retain
their legacy interpretation. Profile changes require reset/reconstruction of
protocol state, safe handling of held musical notes and their eventual releases,
and ordered changes before the next note. Reconnect/seek must reconstruct the
whole state, not just the program. Existing downstream VSTi program/keyswitch
actions remain separate from this upstream protocol.

The isolated [keyswitch probes](../experiments/dorico-strings/README.md) now have
bounded export/live evidence; their old procedures are not a new test request.
Future production tests must cover program selection/replay, simultaneous same-channel
traffic on different ports, and instrument changes while notes are held.

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
