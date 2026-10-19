# Chair coverage — superseded generation proposal / existing diagnostic

> **Superseded as a bridge-generation strategy, 2026-09-24.** The current plan is
> [four orchestral section bridge maps](orchestral-bridge-maps.md), using a small
> base set plus mutex-grouped add-ons on independent conduits. The assumptions
> below of chair-specific generation, no upstream add-ons, and Cartesian-product
> base enumeration are historical, not instructions for further implementation.
> The implemented analyzer remains read-only; its candidate-product count is not
> an estimate of the new maps' entry counts. Snapshot/provenance, per-layer
> behavior, budget honesty, and versioning ideas remain useful.

Status: design and first read-only sizing implementation, 2026-09-23.
No playback runtime, generator, installation, or Dorico-project changes.

## Implemented first slice

Open **View → Windows → Technique Catalog → Chair coverage** and refresh the
catalog. `ChairCoverage` runs on the existing low-priority catalog worker using
immutable assigned-map snapshots, including inactive layers. Its `chair-sizing-v1`
model uses the 20 firm Mutex rows and the 25 Independent IDs from the inventory.
All other identities remain separate flags with review diagnostics. It includes
the five PTs consumed directly by Fiddle's duration processing, regardless of
whether a library switch names them.

The report estimates the local product, enumerates requests lazily, evaluates
the full unchanged request in each layer at all five lengths using `resolveMatch`,
and shows bounded examples with base/add-on indices, unmatched and extra selected
identities, and per-layer outcome counts. Conflicting source conjunctions are
reported before enumeration. Natural remains a distinct flag with an unresolved
reset-semantics warning; it is not equated with unspecified.

Default budgets are 4,096 states per chair, 100,000 resolver calls and five seconds
for the whole analysis, and 16 witnesses per chair / 2 MiB of witnesses per report.
These are configurable in the C++ `Limits` API, not yet user-facing settings.
Time/cancellation checks are cooperative between resolver calls; an individual
resolver call is not preempted. Partial work is never called complete. Outcome
counts may include work from a partially evaluated state; examples only contain
fully evaluated states. Estimates are floating-point sizing figures for very
large products, not exact integer package identities. The XML fingerprint is a
diagnostic hash, not a cryptographic or installation identity.

This is intentionally **not an installation-readiness assessment**. Counterpart
and cancellation dependency closure, full raw-XML capability checking, project-local
host mutex/alias constraints, metadata/wire compilation, and installed-manifest
comparison remain unimplemented. Every report says so; installed compatibility
is always unknown. Custom IDs, absent definitions/maps/XML, Lua dependencies,
unknown actions, malformed/unsupported conditions, and ignored add-on conditions
have explicit notes. Enumeration completeness applies only to the displayed
sizing domain. No state is dropped because libraries currently select equal MIDI.

Validation: `ChairCoverageTest`, catalog/reachability/repair native regressions,
and the isolated `browser-tests/techniqueCatalog.mjs` UI test. The native test
also accepts explicitly supplied `.doricolib` files as layers of a **test chair**;
that mode is not discovery of current project assignments.

## Historical decision and scope — superseded

Generate a **bridge map** for a chair's requirements, not a universal map of
every built-in technique. All layers in a chair receive the same decoded
musical intent. Their **library expression maps** remain separate consumers.
Identical chair requirements may share a bridge map; section-wide merging is
an explicit union-and-reanalysis, not an assumption that section members match.

Start with built-in playback identities and the proposed musical dimensions.
User/vendor-defined identities remain visible diagnostics, not silently renamed
or discarded. Supporting their definitions is a later extension.

Assume **no Dorico add-on switches** in generated bridge maps. This does not
ban downstream vendor add-ons: Fiddle already resolves those itself. Static
analysis does not simulate or prove Dorico's expression-map selection algorithm.

The workflow is chair/layer configuration, coverage review, then a versioned
package of bridge maps, decoder specifications, endpoint configurations, and a
playback template. Top-to-bottom template allocation and the need to restart
Dorico after template updates are design constraints supplied by the user;
this proposal does not independently verify installation APIs or host behavior.

## 1. What coverage promises

A requirement is a **musical request**, not a vendor switch index. Distinguish:

1. **Transport coverage:** canonical PT identities and required note metadata
   survive the bridge for every request in its declared supported domain.
2. **Layer interpretation:** the production Fiddle resolver selects a base and
   ordered add-ons, or reports fallback/unmatched/no match for that request.
3. **Host validation:** Dorico actually emits the correct commands, including
   transitions, initialization, repeated playback, and seeking.

A fallback in one layer does not invalidate correct transport, nor prove that
the request is dispensable. Conversely, transmitting a PT does not promise
that a library implements its sound. Report these outcomes separately.

For a supported canonical PT set S and note metadata M, require
`decode(encode(S, M)) == (S, M)` for the fields in the declared contract.
Absent/unspecified is distinct from an explicit negative such as Non vibrato.
Natural is a separately reviewed reset/default concept, not an automatic command
to clear every independent dimension.

The domain is explicit. It does not promise correct behavior for every arbitrary
extra PT a composer can add. Unlisted combinations may cause Dorico to choose a
fallback; Fiddle cannot necessarily detect information the host never sent.

## 2. Immutable analysis input

Take a non-audio-thread snapshot containing:

- Chair UUID and all assigned layers, including muted/disabled layers. Do not
  infer irrelevance from today's mute state.
- Each layer's actual saved map XML plus parsed map, content fingerprint,
  identity, ordered entries, source provenance, and parser capability report.
  Do not substitute another installed file with the same name or entity ID.
- The canonical built-in catalog revision; approved dimension membership;
  explicit aliases, compounds, defaults/cancellations, and compatibility rules.
  Preserve provisional and unresolved rules as such.
- PT/metadata consumers outside map selection: built-in duration handling and
  processors. A Lua processor with undeclared dependencies makes completeness
  unknown; the algorithm must not infer arbitrary script behavior.
- Declared note metadata contract (initially the existing five written-length
  categories), profile command-note reservations, and supported musical range.
- Existing bridge coverage/encoding manifest, if any, and explicit user policy.

Missing maps and unknown fields are reported. Absence of a map is not evidence
that a layer consumes no techniques, especially with processors present.

## 3. Extract requirements with provenance

Collect exact PT IDs from every base and add-on entry. Split serialized
combinations using the existing parser. Do not lowercase canonical IDs or merge
by display name. Retain a reverse index from each requirement to layer, map,
entry, condition, and originating definition.

Close this set over **declared dependencies**:

- PTs consumed directly by Fiddle's timing or other supported processing, even
  if no explicit vendor entry mentions them.
- Reviewed alias/fallback/compound rules needed to interpret the source IDs.
  Dorico fallbacks are not automatically Fiddle aliases; unresolved distinctions
  need review rather than a fabricated equivalence.
- Required cancellation/default identities for selected dimensions. For example,
  a muted-only library entry does not establish how an unmuting notation reaches
  the bridge. A reviewed rule must cover its off/return behavior.
- Explicit user requests to retain additional musical distinctions.

Do not include every alternative in every family merely because one member is
used. Equally, do not omit a cancellation counterpart merely because no vendor
switch names it. Each added dependency needs an explainable rule.

The output is a chair vocabulary U and auxiliary metadata domain, with reasons
for every inclusion. This stage can be useful even when later analysis is blocked.

## 4. Check dimensions before enumerating

Assign each retained PT to an approved dimension value. Each dimension includes
unspecified. Independent flags are separate dimensions. A reviewed compound
value can represent an exact set of IDs; names alone do not establish compounds.

For every source conjunction, check representability against the dimension
model **before** pruning incompatible states. Two required values on one enum
produce a conflict with the precise source entries and rule involved.

Possible resolutions must be explicit: correct an alias, split an overly broad
dimension, add a justified compound value, or exclude a documented requirement.
Do not import each vendor's mutex groups as global musical truth. A provisional
group is not grounds for silently rejecting a combination. Unknown/custom IDs
remain blockers to a complete built-ins-only report for the affected scope.

Separately compare the proposed dimensions with the host's effective definition
constraints. If Dorico cancels two PTs before map selection, adding a combination
entry cannot by itself make both reach Fiddle. Project-local overrides and
overlapping host groups require review; do not change global defaults here.

## 5. Construct the supported request domain

For each active dimension d, let V[d] be its required values plus unspecified.
The conservative domain is:

`C = { vector in product(V[d]) | satisfies approved compatibility constraints }`

Expand each vector to its exact canonical PT set. Deduplicate only genuinely
identical representations with identical reset semantics. Pair these requests
with the required metadata cases (initially five length categories).

**Do not limit C to vendor rows, base-plus-one-add-on, or pairs of techniques.**
Different layers can consume different parts of a joint request; multiple
downstream add-ons may also be relevant together. Nor may lack of a combined
vendor row be used as evidence that a musical combination is impossible.

Examples (assuming no extra processing dependencies):

| Layer requirements | Conservative chair domain |
| --- | --- |
| A: legato; B: muted | {}, legato, muted, legato + muted: 4 states |
| A: legato; B: muted; C: flautando | 8 states, including the three-way combination |
| A and B both need legato | 2 states, not 4; shared requirements deduplicate |
| One source row requires non-vibrato + wide-vibrato | Conflict under the vibrato enum, not a silently omitted row |

This is a **chair-local** cross product, not a polynomial escape from the
underlying problem. Ten genuinely independent binary requirements still give
1,024 states; twenty give 1,048,576. Vendor maps alone cannot establish a smaller
complete domain. Restricting an oversized chair requires justified musical
constraints or an explicit, versioned user coverage policy.

Enumerate lazily with constraint-based backtracking, canonical ordering, and
memoization. Estimate the unpruned product before search. Record separately:
estimated combinations, visited states, resolver calls, completed states,
possible generated rows, and serialized size estimate. These are not all the
same count: encoding note-length conditions can multiply host rows.

Use independent configurable limits on states, resolver work, output rows,
memory, and elapsed work, with cancellation. Initially retain the existing
audit's 100,000-work scale as a provisional analysis budget, not a Dorico map
size limit or usability claim. A capped result is **partial/unknown**, never
"unsupported" or "complete". Show dominant dimensions and concrete witnesses.

## 6. Evaluate every layer, without altering the request

For every S in C and metadata case M, feed the same canonical input into every
layer. In particular, do not project S down to just the PTs named by that layer.
Use the production resolver and, for behavioral checks, the production annotator
test harness; do not build a second approximate matcher in JavaScript/Python.

Record base and ordered add-ons by stable snapshot entry identity, unmatched
PTs, no-match cases, condition support, and applicable processing diagnostics.
Keep selected actions, release actions, timing and dynamics settings available
for explanation. A witness proves what current Fiddle does, not what a vendor
intended or which sound the instrument actually produces.

Two current-code details make naive optimization unsafe:

- `resolveMatch` tests exact set equality, total request size, priorities, and
  subsets. Even an ID absent from one layer's entries can affect its path.
- `ExpressionMapAnnotator::selectCategoryPercent` consumes articulation PTs
  directly. Equal base/add-on selections need not imply equal timing.

Keep the original canonical request even if two states produce the same patch
choices today. Do not merge them by current MIDI output: release behavior,
redundancy tracking, timing, processors, later layers, and future events may
distinguish them. Such equivalence can be a display grouping, not permission
to remove bridge states.

Unsupported conditions/actions remain explicit limitations. The current audit's
condition recognizer is narrower than the updated production condition parser;
implementation must use an accurate capability check rather than copying that
old recognizer or advertising all parsed fields as implemented.

## 7. Separate semantic coverage from map compilation

The coverage result is a request set/constraint manifest and evidence, not yet
an installable map. A later compiler proposes ordinary **base switches** for
the exact supported combinations, without upstream add-ons. Each selected row
should establish the complete relevant vector (including reset values), plus
the required metadata. Do not depend on inheriting stale dimension values.

The compiler must separately validate controller/key budgets, musical note
reservations, effective host mutex behavior, length predicates, entry collisions,
and reset ordering. One semantic state is not necessarily one host row. Logical
enumeration alone does not prove Dorico will choose the proposed row.

Transitions need their own evidence: each value/default change, binary on/off,
overlapping dimensions, note-local attributes, chords, stop/repeat, mid-passage
starts, and multiple endpoints. For small domains, exhaustively simulate protocol
transitions; for larger ones, use structural checks and generated test witnesses
and state exactly what remains untested in the host.

The r6 test established that this score/map does **not** automatically restore
the preceding mutex member after a duration-limited override. Explicit vibrato
restatement worked. Do not add a decoder history stack to infer missing musical
intent. This is part of the user-facing bridge semantics and acceptance score.
Fresh initialization also remains a separate gate: the MIDI export had channel
and first-event-order caveats despite the successful focused live checks.

## 8. Does adding a layer require a Dorico update?

Reanalyze the new immutable chair snapshot, then compare its supported request
domain and metadata contract to the installed manifest, not just PT-name lists.

**Fiddle-only change** requires all of:

- New required domain is contained in installed supported domain; inclusion is
  proved by complete enumeration or a sound constraint implication check.
- The installed encoding/decoder contract still reconstructs those requests
  unchanged; relevant host validation applies to that same artifact revision.
- No endpoint allocation, musical-range reservation, or decoder activation
  change is required; no unresolved dependency is being hidden.

Then evaluate the new layer's downstream behavior and show warnings independently.
Its patch actions can change without requiring new upstream messages. Gain/pan
changes do not create coverage requirements. Removing a layer normally leaves
the broader installed map reusable; do not shrink it automatically.

If inclusion fails, produce a missing-request witness and its provenance. If
analysis is incomplete, say compatibility is unknown, not "up to date". Even an
unchanged vocabulary can need an update if a prior explicit policy excluded a
now-required combination. Unresolved vendor semantics are a review problem;
regenerating a bridge map does not automatically fix them.

Keep coverage, wire encoding, layer behavior, and installation/allocation
fingerprints separate. Bind the immutable package/decoder manifest to the Fiddle
project version. A new package gets new artifact identities; older packages and
embedded Dorico maps are not overwritten. Existing numeric meanings should
remain stable where possible, but identical code assignments do not make two
different host-selection tables the same artifact.

## 9. Historical implementation boundary and acceptance cases

First implement a pure cancellable analyzer over snapshots. Output chair-scoped
vocabulary/dependencies, conflicts, domain counts, request witnesses, per-layer
interpretations, and installed-coverage compatibility. No XML generation,
profile activation, PBT changes, or installation in that first step.

Test at least:

- Cross-layer two- and three-way combinations absent from individual map rows.
- Multiple downstream add-ons, duplicate layers, inactive layers, and map-less
  layers; missing sources never become successful empty requirements.
- Same-dimension source conflict, provisional groups, alias collisions, custom
  IDs, unspecified versus explicit negatives, and Natural/default semantics.
- All length categories, compound conditions, conditioned downstream add-ons,
  unsupported fields, and timing consumers outside switch selection.
- Equal selected patch but distinct intent; extra IDs affecting matching;
  script dependencies without a declared contract.
- Budget/cancellation reports, deterministic ordering, and sound deduplication.
- Adding an already-covered layer without changing bridge identity; a genuinely
  new combination triggering an update; removal preserving an older superset.
- Immutable package reuse across projects and an old project retaining its pair.

Only after representative real chairs have measured manageable domain sizes
should the next phase compile/install maps and test host selection. If those
sizes are excessive, the right next discussion is explicit coverage policies,
not invisible pruning or a return to unverified add-on composition.

## Evidence used for this proposal

- [Existing audit and profile design](dorico-bridge-profiles.md).
- [Built-in inventory and provisional dimensions](dorico-built-in-technique-groups.md).
- `Source/Server/BridgeReachability.{h,cpp}`: immutable snapshots, real resolver,
  bounded witness search; not a complete chair-domain enumerator.
- `Source/Server/ExpressionMapData.h`: raw-set matching and condition handling.
- `Source/Server/ExpressionMapAnnotator.h`: no mutex-default expansion, timing,
  note-on/off actions, and match redundancy state.
- [Live r6 evidence](../experiments/dorico-strings/live-results.md) and
  [export evidence](../experiments/dorico-strings/results/reliability-r6-corrected.md).
