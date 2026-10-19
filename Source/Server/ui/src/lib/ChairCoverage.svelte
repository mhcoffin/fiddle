<script>
    let { coverage } = $props();
    let chairId = $state("");
    let witnessIndex = $state(0);
    let chair = $derived(coverage?.chairs?.find(c => c.id === chairId) || coverage?.chairs?.[0]);
    let witness = $derived(chair?.witnesses?.[witnessIndex] || chair?.witnesses?.[0]);
    $effect(() => {
        if (chair && chairId !== chair.id) { chairId = chair.id; witnessIndex = 0; }
        if (chair && witnessIndex >= chair.witnesses.length) witnessIndex = 0;
    });
    const requestName = ids => ids?.length ? ids.join(" + ") : "Unspecified (no PTs)";
    const lengths = ["Very short", "Short", "Medium", "Long", "Very long"];
    const count = n => typeof n === "number" ? n.toLocaleString(undefined, { maximumSignificantDigits: 12 }) : n;
</script>

<section aria-label="Chair coverage" class="coverage">
    <h2>How large a bridge map does each chair need?</h2>
    <p>This read-only sizing model combines the requirements of every layer, including inactive layers and Fiddle’s duration processing. Each request goes unchanged to every library’s production resolver at all five note lengths. No bridge maps are generated or installed.</p>
    <p class="notice">A completed enumeration is not an installation-ready map. Cancellation/default rules, host behavior, and unreviewed dimensions still need review. Row counts and MIDI allocation are not yet calculated; installed compatibility is unknown.</p>
    {#if chair}
        <label>Chair <select aria-label="Coverage chair" bind:value={chairId} onchange={() => { witnessIndex = 0; }}>
            {#each coverage.chairs as item}<option value={item.id}>{item.name}</option>{/each}
        </select></label>
        <div class="stats" aria-label="Chair coverage counts">
            <strong>{chair.dimensions.length} dimensions · {chair.vocabulary.length} PTs · {chair.layers.length} layers</strong>
            <span>{count(chair.estimatedStates)} candidate requests (includes unspecified)</span>
            <span>{count(chair.completedStates)} completely evaluated · {count(chair.resolverCalls)} resolver calls</span>
            <span class:warning={chair.enumerationStatus !== "complete"}>{chair.enumerationStatus === "complete" ? "Enumeration complete for this sizing domain" : `Partial / unknown — ${chair.stopReason}`}</span>
        </div>
        <details><summary>Dimensions and vocabulary provenance</summary>
            <p>Each dimension has an additional unspecified value. Only the implemented firm enums exclude combinations. Other PTs remain separate flags; this can overestimate the required domain.</p>
            {#each chair.dimensions as dimension}<div class="dimension"><strong>{dimension.name}</strong> · {dimension.kind} · {dimension.stateCount} states<br />{dimension.values.join(", ")}</div>{/each}
            {#each chair.vocabulary as pt}<details class="provenance"><summary>{pt.id}{pt.builtin ? "" : " — custom / definition missing"}</summary>
                {#each pt.reasons as reason}<p>{reason.layerName} · {reason.mapName} · {reason.entryName || reason.reason}{reason.entry >= 0 ? ` (#${reason.entry + 1})` : ""}</p>{/each}
            </details>{/each}
        </details>
        <details open><summary>Review notes ({chair.issueCount})</summary>
            <div class="issues">{#each chair.issues as issue}<p class="warning"><strong>{issue.code}</strong>: {issue.message}
                {#if issue.layerId}<span> · {chair.layers.find(l => l.id === issue.layerId)?.name || issue.layerId}{issue.entry >= 0 ? ` · entry ${issue.entry + 1}` : ""}</span>{/if}
                {#if issue.techniques.length}<span> · {issue.techniques.join(" + ")}</span>{/if}
            </p>{/each}</div>
            {#if chair.issueCount > chair.issues.length}<p>First {chair.issues.length} notes shown.</p>{/if}
        </details>
        <h3>Library interpretation</h3>
        <p>These counts are layer × length evaluations, not distinct requests. “Covered” means selected entries contain exactly the request; it does not prove that all MIDI actions or the resulting sound are supported.</p>
        <div class="table-scroll"><table><thead><tr><th>Layer / map</th><th>Covered</th><th>Partial</th><th>Fallback</th><th>No match</th></tr></thead><tbody>
            {#each chair.layers as layer}<tr><td>{layer.name}<small>{layer.assigned ? layer.mapName : "No assigned map — unknown"}</small></td>
                <td>{layer.outcomes.covered || 0}</td><td>{layer.outcomes.partial || 0}</td><td>{layer.outcomes.fallback || 0}</td><td>{layer.outcomes["no-match"] || 0}</td></tr>{/each}
        </tbody></table></div>
        <h3>Concrete requests</h3>
        <p>A bounded sample of initial and multi-technique requests from this scan; not an exhaustive list. Unmatched PTs may still affect Fiddle’s duration processing.</p>
        {#if witness}
            <label>Example <select aria-label="Coverage example" bind:value={witnessIndex}>
                {#each chair.witnesses as item, index}<option value={index}>{requestName(item.techniques)}</option>{/each}
            </select></label>
            <p class="request">{requestName(witness.techniques)}</p>
            <div class="table-scroll"><table aria-label="Coverage interpretations"><thead><tr><th>Layer</th><th>Length</th><th>Selection</th><th>Result / unmatched PTs</th></tr></thead><tbody>
                {#each witness.interpretations as result}<tr><td>{result.layerName}</td><td>{lengths[result.length]}</td>
                    <td>{result.base?.name || "No base"}{#each result.addons as addon}<small>+ {addon.name}</small>{/each}</td>
                    <td>{result.outcome}<small>{result.unmatched.join(", ")}</small>{#if result.introduced.length}<small>Selected extra IDs: {result.introduced.join(", ")}</small>{/if}</td></tr>{/each}
            </tbody></table></div>
        {:else}<p>No complete request examples retained. Refresh after resolving missing inputs or increasing analysis budgets.</p>{/if}
        <p class="footnote">{coverage.modelRevision} · budgets: {count(coverage.limits.statesPerChair)} requests/chair, {count(coverage.limits.resolverCalls)} resolver calls/scan, {coverage.limits.milliseconds / 1000}s cooperative work limit. Refresh after changing assignments.</p>
    {:else}<p>Refresh the catalog with layers assigned to chairs to analyze their requirements.</p>{/if}
</section>

<style>
    .coverage { flex:1; min-height:0; overflow:auto; padding:20px 24px; color:#cbd5e1; }
    h2 { margin:0 0 10px; color:#f1f5f9; font-size:20px; } h3 { color:#bae6fd; margin:22px 0 8px; }
    p { line-height:1.5; color:#aebdd0; } .notice { padding:12px; background:#162437; border-left:3px solid #7dd3fc; }
    label { display:flex; align-items:center; gap:10px; min-width:0; } select { min-width:0; max-width:100%; flex:1; font:inherit; color:inherit; background:#172337; border:1px solid #475569; border-radius:6px; padding:8px; }
    .stats { display:flex; flex-direction:column; gap:7px; padding:16px 0; } .stats strong { color:#e2e8f0; }
    details { border-top:1px solid #334155; padding:12px 0; } summary { cursor:pointer; color:#bae6fd; }
    .dimension { margin:8px 0; padding:8px; background:#111c2e; overflow-wrap:anywhere; }
    .provenance { padding:8px; } .issues { max-height:240px; overflow:auto; } .warning { color:#fcd9a5; } .issues p { font-size:12px; overflow-wrap:anywhere; }
    .table-scroll { overflow:auto; } table { border-collapse:collapse; width:100%; font-size:12px; } td, th { padding:9px; border-bottom:1px solid #273449; text-align:left; vertical-align:top; overflow-wrap:anywhere; } th { background:#172337; }
    small { display:block; color:#94a3b8; margin-top:4px; } .request { overflow-wrap:anywhere; color:#7dd3fc; } .footnote { font-size:11px; margin-top:22px; }
    :focus-visible { outline:2px solid #7dd3fc; outline-offset:2px; }
    @media (max-width:750px) { .coverage { flex:none; overflow:visible; padding:16px; } }
</style>
