<script>
    import { onMount } from "svelte";
    import BridgeReachability from "./BridgeReachability.svelte";
    import BridgeRepairPlan from "./BridgeRepairPlan.svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { catalogRows, defaultCatalogScope, definitionLabel, mapsInScope, sourceLabels } from "./techniqueCatalog.js";

    let { visible = true, onClose = () => {} } = $props();
    let state = $state({ running: false, files: 0 });
    let includeReference = $state(false), scope = $state("project"), query = $state(""), status = $state("all");
    let selectedId = $state(""), page = $state(0), mapQuery = $state(""), mapIndex = $state("");
    let initialized = false;
    let view = $state("audit");
    let report = $derived(state.report);
    let rows = $derived(catalogRows(report, { scope, query, status, mapIndex }));
    let pageCount = $derived(Math.max(1, Math.ceil(rows.length / 80)));
    let currentPage = $derived(Math.min(page, pageCount - 1));
    let displayed = $derived(rows.slice(currentPage * 80, (currentPage + 1) * 80));
    let selected = $derived(rows.find(row => row.id === selectedId) || displayed[0]);
    let scopedMaps = $derived(mapsInScope(report, scope));
    let detailMaps = $derived((selected?.maps || []).filter(index => scopedMaps.has(index) && (mapIndex === "" || index === Number(mapIndex))));
    let mapChoices = $derived((report?.maps || []).map((map, index) => ({ ...map, index }))
        .filter(map => scopedMaps.has(map.index) && (!mapQuery.trim() || `${map.name} ${map.creator} ${report.sources[map.source]?.path}`.toLowerCase().includes(mapQuery.toLowerCase().trim())))
        .sort((a, b) => a.name.localeCompare(b.name)));
    const sourceText = index => {
        const source = report?.sources[index];
        return source ? `${sourceLabels[source.kind] || source.kind}: ${source.path}${source.snapshot ? " (saved XML snapshot)" : ""}` : "Unknown source";
    };
    const scopeChange = () => { page = 0; mapIndex = ""; };
    onMount(() => onFromCpp("setTechniqueCatalogState", value => {
        state = value;
        includeReference = !!value.includeReference;
        if (value.report && !value.report.cancelled && !value.report.error && !initialized) {
            scope = defaultCatalogScope(value.report); initialized = true;
        }
    }));
    $effect(() => {
        if (!visible) return;
        dispatchCpp("requestTechniqueCatalogState");
        const timer = setInterval(() => dispatchCpp("requestTechniqueCatalogState"), 750);
        return () => clearInterval(timer);
    });
    const scan = () => { page = 0; mapIndex = ""; selectedId = ""; dispatchCpp("scanTechniqueCatalog", includeReference); };
</script>

<main aria-label="Technique Catalog">
    <header>
        <div><h1>Technique Catalog</h1><p>Dorico vocabulary and Fiddle map coverage · read-only</p></div>
        <button onclick={onClose}>Close</button>
    </header>
    <div class="scan-controls">
        <label><input type="checkbox" bind:checked={includeReference} disabled={state.running} /> Include ~/Art-Conductor reference collection</label>
        <div class="actions">
            <button onclick={() => dispatchCpp("addTechniqueCatalogSource")} disabled={state.running}>Add source…</button>
            <button onclick={scan} disabled={state.running}>Refresh catalog</button>
            {#if state.running}<button onclick={() => dispatchCpp("cancelTechniqueCatalogScan")}>Cancel scan</button>{/if}
        </div>
        <p class="hint">Installed maps are not proof of installed instruments. VSL and Art Conductor alternatives remain separate. Refresh after changing Fiddle assignments; project-local Dorico edits may require an exported library.</p>
    </div>
    {#if state.running}<div class="notice" role="status">{state.previewing ? "Calculating repair preview from the existing snapshot" : `Scanning in the background · ${state.files} files read`}. No playback settings are being changed.</div>
    {:else if report?.error}<div class="notice warning" role="alert">{report.error}</div>
    {:else if report?.cancelled}<div class="notice warning" role="status">Scan cancelled. Results below are partial; refresh for a complete report.</div>{/if}
    {#if report?.techniques}
        <nav aria-label="Catalog view"><button class:active={view === "audit"} aria-pressed={view === "audit"} onclick={() => { view = "audit"; }}>Assigned-map audit</button><button class:active={view === "repairs"} aria-pressed={view === "repairs"} onclick={() => { view = "repairs"; }}>Repair plan</button><button class:active={view === "vocabulary"} aria-pressed={view === "vocabulary"} onclick={() => { view = "vocabulary"; }}>Vocabulary catalog</button></nav>
        {#if view === "audit"}
            <BridgeReachability audit={report.reachability} />
        {:else if view === "repairs"}
            <BridgeRepairPlan plan={report.repairPlan} busy={state.running} partial={report.cancelled} error={report.repairError} snapshotAt={report.scannedAt} />
        {:else}
        <div class="summary">{report.techniques.length} playback identities · {report.maps.length} map definitions · {report.files} files
            <span>Snapshot {new Date(report.scannedAt).toLocaleString()}</span></div>
        <details class="sources"><summary>Sources and scan notes ({report.issues.length + report.unresolvedUsage.length})</summary>
            <p>The bridge column describes explicit reserved-CC switches in Fiddle's loaded map. It does not prove Dorico emits every combination, or that an existing Dorico project uses that map version. Aliases, inheritance and file precedence are recorded, not resolved.</p>
            {#each report.roots as source}<div class:warning={!source.exists}>{sourceLabels[source.kind]} · {source.path}{source.exists ? "" : " — not found"}</div>{/each}
            {#each report.issues as issue}<div class="warning">{issue.path}: {issue.message}</div>{/each}
            {#each report.unresolvedUsage as use}<div class="warning">Unresolved {use.scope} assignment: {use.label} — {use.mapId}</div>{/each}
        </details>
        <div class="filters">
            <label>Scope<select aria-label="Scope" bind:value={scope} onchange={scopeChange}>
                <option value="project">Current project assignments</option><option value="library">Fiddle library assignments</option>
                <option value="installed">Installed Dorico maps</option><option value="reference">Reference / additional maps</option><option value="all">All discovered vocabulary</option>
            </select></label>
            <label>Coverage<select aria-label="Coverage" bind:value={status} onchange={() => { page = 0; }}><option value="all">All techniques</option><option value="gap">Not declared in bridge</option><option value="declared">Declared in bridge</option><option value="missing">Definition not found</option><option value="variants">Definition variants</option></select></label>
            <label class="search">Search techniques<input type="search" bind:value={query} oninput={() => { page = 0; }} placeholder="Name, notation text, ID or map name" /></label>
        </div>
        <div class="map-filter">
            <label>Find a map<input type="search" bind:value={mapQuery} placeholder="Map, creator or source path" /></label>
            <label>Map<select aria-label="Map" bind:value={mapIndex} onchange={() => { page = 0; }}>
                <option value="">All maps in scope</option>
                {#each mapChoices.slice(0, 100) as map}<option value={String(map.index)}>{map.name} · {sourceLabels[report.sources[map.source].kind]} · #{map.index + 1}</option>{/each}
                {#if mapIndex !== "" && !mapChoices.slice(0, 100).some(map => String(map.index) === mapIndex)}<option value={mapIndex}>{report.maps[Number(mapIndex)]?.name} (selected)</option>{/if}
            </select></label><span>{mapChoices.length} maps{mapChoices.length > 100 ? "; showing first 100—narrow the search" : ""}</span>
        </div>
        <div class="workspace">
            <section class="listing" aria-label="Playback techniques">
                <div class="table-scroll"><table><thead><tr><th>Playback technique</th><th>Fiddle bridge</th><th>Definition</th></tr></thead><tbody>
                    {#each displayed as row (row.id)}<tr class:selected={selected?.id === row.id}>
                        <td><button class="technique" onclick={() => { selectedId = row.id; }}>{row.names[0] || row.id}<small>{row.id}</small></button></td>
                        <td><span class:gap={!row.declared}>{row.declared ? "Declared" : "Not declared"}</span></td>
                        <td>{definitionLabel(row)}</td>
                    </tr>{:else}<tr><td colspan="3" class="empty">No techniques in this view. Try another scope or filter.</td></tr>{/each}
                </tbody></table></div>
                <footer><span>{rows.length} techniques · page {currentPage + 1} of {pageCount}</span><button disabled={currentPage === 0} onclick={() => { page = currentPage - 1; }}>Previous</button><button disabled={currentPage + 1 >= pageCount} onclick={() => { page = currentPage + 1; }}>Next</button></footer>
            </section>
            <aside aria-label="Technique details">
                {#if selected}
                    <h2>{selected.names.join(" / ") || selected.id}</h2><code>{selected.id}</code>
                    <p>{selected.declared ? "Explicit switch found in Fiddle's bridge map." : "No explicit reserved-CC switch found in Fiddle's bridge map."} This is a coverage audit, not a playback test.</p>
                    <h3>Playback definitions</h3>
                    {#each selected.definitions as definition}
                        <article><strong>{definition.name}</strong><p>{definition.articulationType || "Type not specified"} · {definition.groupType || "No category"}</p>
                            <dl><dt>Alias</dt><dd>{definition.alias || "None"}</dd><dt>Fallback</dt><dd>{definition.fallback || "None"}</dd><dt>Parent</dt><dd>{definition.parent || "None"}</dd><dt>Inheritance mask</dt><dd>{definition.inheritanceMask || "Unspecified"}</dd><dt>Exclusion memberships</dt><dd>{definition.groups.join(", ") || "None declared here"}</dd></dl>
                            <details><summary>{definition.sources.length} source(s)</summary>{#each definition.sources.slice(0, 100) as index}<p class="path">{sourceText(index)}</p>{/each}{#if definition.sources.length > 100}<p>First 100 sources shown; use the map source filter to inspect further map references.</p>{/if}</details>
                        </article>
                    {:else}<p class="warning">No definition found in scanned sources. This does not prove it is absent from Dorico.</p>{/each}
                    <h3>Score markings ({selected.appearances.length})</h3>
                    {#each selected.appearances as appearance}<article><strong>{appearance.name}</strong><p>{appearance.text || "(symbol)"} · {appearance.type}</p><code>{appearance.id}</code>
                        <p>Always has duration: {appearance.alwaysHasDuration || "unspecified"}<br />Continuation: {appearance.continuation || "unspecified"}<br />Duration line: {appearance.durationLine || "none"}<br />Transition line: {appearance.transitionLine || "none"}</p>
                        <details><summary>{appearance.sources.length} source(s)</summary>{#each appearance.sources.slice(0, 100) as index}<p class="path">{sourceText(index)}</p>{/each}{#if appearance.sources.length > 100}<p>First 100 sources shown.</p>{/if}</details>
                    </article>{/each}
                    <h3>Maps in this scope</h3>
                    {#each detailMaps.slice(0, 100) as index}
                        {@const map = report.maps[index]}
                        <details class="map-detail"><summary>{map.name} · #{index + 1}</summary>
                            <p>{map.creator || "Creator unspecified"} · version {map.version || "unspecified"}</p><code>{map.id}</code><p class="path">{sourceText(map.source)}</p>
                            {#each map.usage as use}<p>{use.scope}: {use.label}{use.exactSource ? "" : " (identity match only)"}</p>{/each}
                            <p>Automatic exclusions: {map.autoMutualExclusion || "unspecified"}</p>
                            {#each map.groups.map(g => report.groups[g]).filter(group => group.techniques.includes(selected.id)) as group}<p><strong>{group.name || group.id}</strong>: {group.techniques.join(", ")}</p>{/each}
                        </details>
                    {/each}
                    {#if detailMaps.length > 100}<p>First 100 map references shown. Use the map filter to inspect a particular source.</p>{/if}
                {:else}<p>Select a technique to inspect its definitions and sources.</p>{/if}
            </aside>
        </div>
        {/if}
    {:else if !state.running}<p class="empty">Refresh the catalog to inspect installed techniques.</p>{/if}
</main>

<style>
    main { height:100vh; display:flex; flex-direction:column; overflow:hidden; background:#0b1220; font-size:13px; }
    nav { display:flex; flex-wrap:wrap; padding:8px 24px; gap:8px; border-bottom:1px solid #334155; } nav .active { background:#17324a; border-color:#7dd3fc; }
    header { display:flex; align-items:center; justify-content:space-between; padding:18px 24px; gap:16px; background:#111c2e; border-bottom:1px solid #334155; }
    h1 { font-size:23px; margin:0; color:#f1f5f9; } h2 { font-size:19px; margin:0 0 8px; } h3 { margin:24px 0 10px; font-size:14px; color:#bae6fd; }
    p { margin:7px 0; color:#aebdd0; line-height:1.5; } button, input, select { font:inherit; color:inherit; background:#172337; border:1px solid #475569; border-radius:6px; padding:8px 10px; } button { cursor:pointer; } button:disabled { opacity:.4; cursor:default; } button:hover:not(:disabled) { border-color:#7dd3fc; } :focus-visible { outline:2px solid #7dd3fc; outline-offset:2px; }
    .scan-controls, .filters, .map-filter { padding:12px 24px; display:flex; gap:12px; align-items:center; flex-wrap:wrap; border-bottom:1px solid #273449; } .actions { display:flex; gap:8px; } .hint { width:100%; font-size:12px; margin:0; } label { display:flex; gap:6px; align-items:center; } .filters label, .map-filter label { align-items:stretch; flex-direction:column; } .search { flex:1; min-width:180px; } .map-filter select { max-width:420px; } .map-filter span { color:#94a3b8; font-size:12px; }
    .summary, .notice { padding:10px 24px; background:#111c2e; } .summary span { float:right; color:#94a3b8; font-size:12px; } .warning, .gap { color:#fcd9a5; } .sources { padding:8px 24px; max-height:180px; overflow:auto; overflow-wrap:anywhere; color:#aebdd0; } summary { cursor:pointer; } .sources > div { font-size:12px; margin:5px 0; }
    .workspace { display:grid; grid-template-columns:minmax(0, 1.35fr) minmax(300px, 1fr); min-height:0; flex:1; border-top:1px solid #334155; } .listing { display:flex; flex-direction:column; min-height:0; min-width:0; } .table-scroll { overflow:auto; flex:1; } table { width:100%; border-collapse:collapse; } th { position:sticky; top:0; background:#172337; text-align:left; font-size:12px; z-index:1; } th, td { padding:10px 12px; border-bottom:1px solid #273449; } td { font-size:12px; } .technique { text-align:left; border:0; background:transparent; padding:0; width:100%; font-weight:600; overflow-wrap:anywhere; } small { display:block; font-size:10px; font-weight:400; color:#94a3b8; } tr.selected { background:#17324a; } .empty { padding:24px; color:#94a3b8; } footer { display:flex; align-items:center; gap:8px; padding:10px 14px; border-top:1px solid #334155; } footer span { flex:1; color:#94a3b8; font-size:12px; }
    aside { overflow:auto; border-left:1px solid #334155; background:#0f1929; padding:20px; overflow-wrap:anywhere; } article { padding:12px; margin:8px 0; border:1px solid #334155; border-radius:8px; } code { font-size:11px; color:#94a3b8; } dl { display:grid; grid-template-columns:100px 1fr; gap:6px 10px; font-size:12px; } dt { color:#94a3b8; } dd { margin:0; } .path { font-size:11px; overflow-wrap:anywhere; } .map-detail { margin:8px 0; border-bottom:1px solid #334155; padding-bottom:8px; }
    @media (max-width:750px) { main { overflow:auto; } .workspace { display:block; flex:none; min-height:400px; } .table-scroll { max-height:400px; } aside { border-left:0; border-top:1px solid #334155; } .map-filter select { max-width:100%; } .map-filter label { min-width:0; width:100%; } .summary span { float:none; display:block; } }
</style>
