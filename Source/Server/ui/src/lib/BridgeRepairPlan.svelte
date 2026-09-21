<script>
    import { onMount } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { auditStatuses } from "./bridgeReachability.js";
    let { plan, busy = false, partial = false, error = "", snapshotAt = "" } = $props();
    let preserve = $state(true), additions = $state([]), query = $state(""), lastId = $state(""), exported = $state("");
    $effect(() => {
        if (plan?.planId && plan.planId !== lastId) {
            lastId = plan.planId; preserve = !!plan.preserveIdentities;
            additions = [...(plan.selectedAdditions || [])]; exported = "";
        }
    });
    const same = (a, b) => JSON.stringify([...a].sort()) === JSON.stringify([...b].sort());
    let dirty = $derived(preserve !== !!plan?.preserveIdentities || !same(additions, plan?.selectedAdditions || []));
    let candidates = $derived((plan?.candidates || []).filter(row => !query.trim() || `${row.id} ${row.reason} ${row.affected.map(use => `${use.chair} ${use.layer} ${use.map}`).join(" ")}`.toLowerCase().includes(query.toLowerCase().trim())));
    const toggle = id => { additions = additions.includes(id) ? additions.filter(value => value !== id) : [...additions, id]; };
    onMount(() => onFromCpp("bridgeRepairExportResult", message => { exported = message; }));
</script>

<section aria-label="Bridge repair plan" class="repair">
    <div class="intro">
        <h2>Plan repairs without changing playback</h2>
        <p>Start with exact identities, then choose missing techniques to encode. Every current layer is checked against one proposed universal profile, shared across its chair.</p>
        <p class="note">This is a decoder/resolver simulation, not a generated Dorico map. Export saves a reviewable JSON plan; map generation and profile activation are not available yet. Refresh the catalog after changing assignments.</p>
        {#if snapshotAt}<small>Assignment snapshot: {new Date(snapshotAt).toLocaleString()}</small>{/if}
    </div>
    {#if plan}
        <div class="controls">
            <label><input type="checkbox" aria-label="Preserve exact technique IDs" bind:checked={preserve} disabled={busy || partial || !plan.metadataComplete} /> Preserve exact technique IDs and split compound techniques</label>
            <div class="actions"><button onclick={() => dispatchCpp("previewBridgeRepairs", preserve, additions)} disabled={busy || partial}>Preview selected repairs</button>
                <button onclick={() => dispatchCpp("exportBridgeRepairPlan", plan.planId)} disabled={busy || partial || dirty || plan.cancelled}>Export plan (JSON)…</button></div>
            {#if dirty}<p class="warning">Choices changed. Preview again to update the results and enable export.</p>{/if}
            {#if error}<p class="warning" role="alert">{error}</p>{/if}
            {#if exported}<p role="status">{exported}</p>{/if}
        </div>
        <div class="content">
            <div class="score" aria-label="Repair coverage comparison">
                <div><small>Current decoder witnesses</small><strong>{plan.before.witness} / {plan.before.total}</strong></div>
                <div><small>Projected witnesses</small><strong>{plan.after.witness} / {plan.after.total}</strong></div>
                <div><small>New witnesses / regressions</small><strong class:warning={plan.regressed > 0}>{plan.gained} / {plan.regressed}</strong></div>
            </div>
            {#each plan.warnings as warning}<p class="warning callout">{warning}</p>{/each}
            {#if plan.blockedIdentityStates?.length}<details class="card"><summary>Source-state collisions ({plan.blockedIdentityStates.length}) · left unchanged</summary>
                {#each plan.blockedIdentityStates as state}<article><strong>CC{state.cc} = {state.value}</strong><p class="ids">Source IDs: {state.sourceIDs.join(" / ")}<br />Retained interpretation: {state.retained.join(" + ")}</p><p>{state.reason}</p></article>{/each}
            </details>{/if}
            <details class="card"><summary>Decoder-only identity repairs ({plan.identityChanges.length} CC states){plan.canonicalPreview ? " · included in preview" : " · not included"}</summary>
                <p>Keep the same controller numbers and values, but preserve source IDs, case and namespaces. Compound source IDs become separate techniques. Existing reset/default suppression stays unchanged.</p>
                <div class="table-scroll"><table><thead><tr><th>State</th><th>Current IDs</th><th>Proposed IDs</th></tr></thead><tbody>
                    {#each plan.identityChanges as change}<tr><td>CC{change.cc} = {change.value}</td><td class="ids">{change.before.join(" + ")}</td><td class="ids">{change.after.join(" + ")}</td></tr>{/each}
                </tbody></table></div>
            </details>
            <div class="card">
                <h3>Missing encodings</h3>
                <p>{plan.freeControllers.length} unused reserved controllers: {plan.freeControllers.map(cc => `CC${cc}`).join(", ") || "none"}. {plan.allocated} allocated in this preview.</p>
                <p class="note">Each selected addition uses its own controller (127 on / 0 off). Running out here means this conservative allocation is full—not that more efficient shared dimensions or chair profiles are impossible. Definitions, notation and cancellation still require review.</p>
                <label class="search">Find a technique or affected layer<input type="search" aria-label="Search repair candidates" bind:value={query} /></label>
                <div class="table-scroll"><table><thead><tr><th>Include</th><th>Required identity / affected layers</th><th>Proposed action or decision needed</th></tr></thead><tbody>
                    {#each candidates.slice(0, 80) as candidate (candidate.id)}<tr>
                        <td><input type="checkbox" aria-label={`Include ${candidate.id}`} checked={additions.includes(candidate.id)} disabled={!candidate.eligible || busy || partial} onchange={() => toggle(candidate.id)} /></td>
                        <td><strong class="ids">{candidate.id}</strong><small>{candidate.affected.length} map entry requirement(s) · {candidate.appearanceCount} known score marking(s)</small>
                            <details><summary>Affected entries</summary>{#each candidate.affected.slice(0, 100) as use}<p>{use.chair} / {use.layer}<br />{use.map}: {use.entry || `Entry #${use.index + 1}`}</p>{/each}{#if candidate.affected.length > 100}<p>First 100 shown; all are in the exported plan.</p>{/if}</details>
                        </td>
                        <td><p class:warning={!candidate.eligible}>{candidate.reason}</p>{#if candidate.cc != null}<strong>Proposed CC{candidate.cc}: 127 on, 0 off</strong>{/if}</td>
                    </tr>{:else}<tr><td colspan="3">No missing identities in this view. Other unresolved entries may still need review below.</td></tr>{/each}
                </tbody></table></div>
                {#if candidates.length > 80}<p>Showing 80 of {candidates.length}; narrow the search. Selection is retained across searches.</p>{/if}
            </div>
            <details class="card" open={plan.regressed > 0}><summary>Changed results ({plan.changes.length})</summary>
                <p>Results use the actual instrument-map resolver with a proposed decoder. A lost witness is a migration warning—not something to hide with an automatic alias. No transition or audible playback test is implied.</p>
                {#each plan.changes.slice(0, 200) as change}<article class:regression={change.regression}>
                    <strong>{change.chair} / {change.layer} — {change.entry || `Entry #${change.index + 1}`}</strong><small>{change.map}</small>
                    <p>{auditStatuses[change.before]} → {auditStatuses[change.after]}{change.regression ? " · REGRESSION" : ""}</p><p>{change.reason}</p>
                    {#if change.witness}<p class="ids">Example state: {change.witness.cc.map(cc => `CC${cc.cc}=${cc.value}`).join(", ")}</p>{/if}
                </article>{:else}<p>No entry changed status.</p>{/each}
                {#if plan.changes.length > 200}<p>First 200 shown; all are in the exported plan.</p>{/if}
            </details>
            <details class="card"><summary>Still needs review ({plan.remaining.length} entries)</summary>
                <p>Encoding conflicts need dimension/combination design. Selection issues need resolver or instrument-map review. Missing definitions need the correct library data. None is automatically repaired by adding arbitrary switches.</p>
                {#each plan.remaining.slice(0, 100) as row}<article><strong>{row.chairName} / {row.layerName}: {row.name || `Entry #${row.index + 1}`}</strong><small>{row.mapName}</small><p>{auditStatuses[row.status]} — {row.reason}</p></article>{/each}
                {#if plan.remaining.length > 100}<p>First 100 shown; all are in the exported plan.</p>{/if}
            </details>
        </div>
    {:else}<p class="intro">Complete a catalog scan to build a repair plan.</p>{/if}
</section>

<style>
    .repair { display:flex; flex:1; flex-direction:column; min-height:0; font-size:13px; } .intro, .controls { padding:12px 24px; border-bottom:1px solid #334155; } h2 { margin:0; font-size:18px; } h3 { font-size:15px; margin:0; } p { color:#aebdd0; line-height:1.5; margin:8px 0; } small { display:block; font-size:11px; color:#94a3b8; margin:5px 0; } .note { font-size:12px; } .controls { display:flex; flex-wrap:wrap; align-items:center; gap:12px; } .controls p { width:100%; margin:0; } label { display:flex; gap:8px; align-items:center; } .actions { display:flex; gap:8px; flex-wrap:wrap; } button, input[type=search] { font:inherit; color:inherit; background:#172337; border:1px solid #475569; border-radius:6px; padding:8px 10px; } button { cursor:pointer; } button:disabled, input:disabled { opacity:.4; cursor:default; } :focus-visible { outline:2px solid #7dd3fc; outline-offset:2px; } .content { flex:1; overflow:auto; padding:16px 24px; min-height:0; } .score { display:flex; gap:24px; flex-wrap:wrap; margin-bottom:16px; } .score strong { font-size:24px; } .warning { color:#fcd9a5; } .callout { background:#302719; padding:12px; border-radius:6px; } .card { border:1px solid #334155; border-radius:8px; padding:14px; margin:12px 0; } summary { cursor:pointer; color:#bae6fd; } .search { align-items:stretch; flex-direction:column; margin:14px 0; } table { border-collapse:collapse; width:100%; } th, td { padding:10px; text-align:left; border-bottom:1px solid #273449; vertical-align:top; } th { background:#172337; } td { font-size:12px; } .table-scroll { overflow:auto; } .ids { overflow-wrap:anywhere; } article { padding:10px 0; border-bottom:1px solid #334155; overflow-wrap:anywhere; } .regression { border-left:3px solid #f59e0b; padding-left:12px; }
    @media(max-width:750px) { .repair { flex:none; } .content { overflow:visible; padding:12px; } .intro, .controls { padding:12px; } .score { gap:16px; } th, td { padding:6px; } }
</style>
