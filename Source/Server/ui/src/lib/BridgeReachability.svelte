<script>
    import { auditRows, auditCounts, auditStatuses } from "./bridgeReachability.js";
    let { audit } = $props();
    let chairId = $state(""), status = $state("all"), query = $state(""), selectedKey = $state(""), page = $state(0);
    let all = $derived(auditRows(audit, { chairId }));
    let counts = $derived(auditCounts(all));
    let rows = $derived(auditRows(audit, { chairId, status, query }));
    let pageCount = $derived(Math.max(1, Math.ceil(rows.length / 60)));
    let currentPage = $derived(Math.min(page, pageCount - 1));
    let displayed = $derived(rows.slice(currentPage * 60, (currentPage + 1) * 60));
    let selected = $derived(rows.find(row => row.key === selectedKey) || displayed[0]);
    let chairs = $derived((audit?.chairs || []).filter(chair => !chairId || chair.id === chairId));
    let requirements = $derived([...new Set(chairs.flatMap(chair => chair.required || []))].sort());
    let unassigned = $derived(chairs.flatMap(chair => chair.layers.filter(layer => !layer.assigned).map(layer => `${chair.name} / ${layer.name}`)));
    const lengths = ["Very short", "Short", "Medium", "Long", "Very long"];
    $effect(() => {
        if (chairId && !(audit?.chairs || []).some(chair => chair.id === chairId)) chairId = "";
    });
</script>

<section class="audit" aria-label="Assigned-map audit">
    <div class="intro">
        <h2>What can your current chairs request?</h2>
        <p>One Dorico bridge profile per chair, shared by every layer. Each layer keeps its own instrument map. This snapshot includes inactive layers.</p>
        <details><summary>How to interpret this audit</summary>
            <p>Checks use Fiddle's loaded decoder and the actual instrument-map resolver. A decoder witness is an example CC state that selects an entry—not yet a generated Dorico switch or a playback test. Missing identities and encoding conflicts describe required musical intent, even if a fallback happens to select the same entry.</p>
            <p>Requests test each base's techniques, or each base combined with an add-on, across declared/default controller states and encoded note lengths. Other supersets, Dorico transitions, Lua transformations, MIDI action execution and instrument sound are not verified. A selection warning is not proof that an entry can never be selected.</p>
            <p>Current profile: Built-in universal. Profile generation, export and chair assignment will come in the configuration workflow; this audit changes nothing.</p>
        </details>
        {#if !audit?.decoderAvailable}<p class="warning">No supported decoder snapshot. Refresh with Fiddle's bridge map loaded.</p>{/if}
    </div>
    <div class="filters">
        <label>Chair<select aria-label="Audit chair" bind:value={chairId} onchange={() => { page = 0; }}><option value="">All current chairs</option>{#each audit?.chairs || [] as chair}<option value={chair.id}>{chair.name}</option>{/each}</select></label>
        <label>Result<select aria-label="Audit result" bind:value={status} onchange={() => { page = 0; }}><option value="all">All entries</option><option value="attention">Needs attention</option>{#each Object.entries(auditStatuses) as [value, label]}<option {value}>{label}</option>{/each}</select></label>
        <label class="search">Search entries<input aria-label="Search audit entries" type="search" bind:value={query} oninput={() => { page = 0; }} placeholder="Layer, map, entry or technique ID" /></label>
    </div>
    <div class="stats">{all.length} assigned-map entries · {counts.witness} decoder witnesses · {all.length - counts.witness} to review
        <details><summary>{requirements.length} required playback identities across {chairs.length} chair(s)</summary><p class="ids">{requirements.join(", ") || "None"}</p></details>
        {#if unassigned.length}<details class="warning"><summary>{unassigned.length} layer(s) without an instrument expression map</summary><p>{unassigned.join(" · ")}</p></details>{/if}
    </div>
    <div class="workspace">
        <div class="listing">
            <div class="scroll"><table><thead><tr><th>Chair / layer / map entry</th><th>Result</th></tr></thead><tbody>
                {#each displayed as row (row.key)}<tr class:selected={selected?.key === row.key}>
                    <td><button onclick={() => { selectedKey = row.key; }}><small>{row.chairName} / {row.layerName}</small>{row.name || "Unnamed entry"}<small>{row.mapName} · {row.addOn ? "Add-on" : "Base"} #{row.index + 1}</small></button></td>
                    <td class:warning={row.status !== "witness"}>{auditStatuses[row.status]}</td>
                </tr>{:else}<tr><td colspan="2" class="empty">No entries in this view. Assign instrument maps to layers, refresh the catalog, or adjust the filters.</td></tr>{/each}
            </tbody></table></div>
            <footer><span>{rows.length} entries · page {currentPage + 1} of {pageCount}</span><button disabled={currentPage === 0} onclick={() => { page = currentPage - 1; }}>Previous</button><button disabled={currentPage + 1 >= pageCount} onclick={() => { page = currentPage + 1; }}>Next</button></footer>
        </div>
        <aside aria-label="Reachability details">
            {#if selected}
                <small>{selected.chairName} / {selected.layerName}</small><h3>{selected.name || "Unnamed entry"}</h3>
                <p>{selected.mapName} · {selected.addOn ? "Add-on" : "Base"} #{selected.index + 1}</p><code>{selected.mapId}</code>
                <h4 class:warning={selected.status !== "witness"}>{auditStatuses[selected.status]}</h4><p>{selected.reason}</p>
                <h4>Required techniques</h4><p class="ids">{selected.techniques.join(" + ") || "Empty (default)"}</p>
                {#if selected.missing?.length}<h4>Not produced by the decoder</h4><p class="ids warning">{selected.missing.join(", ")}</p>{/if}
                <h4>Condition</h4><p>{selected.condition || "None"}</p>
                {#if selected.alternative}<p>Example competing selection: {selected.alternative}</p>{/if}
                {#if selected.witness}
                    <h4>Example decoder input</h4><p>Length: {lengths[selected.witness.length]}<br />Decoded: {selected.witness.techniques.join(" + ") || "No explicit techniques (reset/default)"}<br />Selected base: {selected.witness.base || "Unnamed"}</p>
                    <div class="ccs">{#each selected.witness.cc as cc}<code>CC{cc.cc} = {cc.value}</code>{/each}</div>
                    <p class="note">This is a state example, not a MIDI sequence to paste into Dorico. Ordering and switch-off behavior still need bridge-generation and playback tests.</p>
                {/if}
            {:else}<p>Select an entry to inspect its requirements and result.</p>{/if}
        </aside>
    </div>
</section>

<style>
    .audit { display:flex; flex:1; flex-direction:column; min-height:0; font-size:13px; }
    .intro, .filters, .stats { padding:12px 24px; border-bottom:1px solid #334155; } .intro { max-height:210px; overflow:auto; } h2 { margin:0; font-size:18px; } h3 { margin:8px 0; font-size:18px; } h4 { margin:20px 0 8px; color:#bae6fd; } p { color:#aebdd0; line-height:1.5; margin:8px 0; } summary { cursor:pointer; color:#aebdd0; } details { margin-top:8px; } .ids { overflow-wrap:anywhere; }
    .filters { display:flex; gap:12px; flex-wrap:wrap; } label { display:flex; flex-direction:column; gap:6px; min-width:0; } .search { flex:1; min-width:180px; } select { max-width:320px; } input, select, button { font:inherit; color:inherit; background:#172337; border:1px solid #475569; border-radius:6px; padding:8px 10px; } button { cursor:pointer; } button:disabled { opacity:.4; cursor:default; } :focus-visible { outline:2px solid #7dd3fc; outline-offset:2px; }
    .workspace { display:grid; grid-template-columns:minmax(0,1.2fr) minmax(300px,1fr); flex:1; min-height:0; } .listing { display:flex; flex-direction:column; min-height:0; min-width:0; } .scroll { overflow:auto; flex:1; } table { border-collapse:collapse; width:100%; } th { position:sticky; top:0; background:#172337; text-align:left; } th, td { padding:10px 12px; border-bottom:1px solid #273449; } td { font-size:12px; } td button { background:transparent; border:0; width:100%; padding:0; text-align:left; overflow-wrap:anywhere; } small { display:block; font-size:11px; color:#94a3b8; margin:3px 0; } .selected { background:#17324a; }
    aside { overflow:auto; background:#0f1929; border-left:1px solid #334155; padding:20px; overflow-wrap:anywhere; } code { color:#aebdd0; font-size:11px; } .ccs { display:flex; flex-wrap:wrap; gap:8px; } .ccs code { padding:6px 8px; border:1px solid #334155; border-radius:5px; } .note { font-size:12px; } .warning { color:#fcd9a5; } .empty { padding:24px; color:#94a3b8; } footer { display:flex; align-items:center; gap:8px; padding:10px 14px; border-top:1px solid #334155; } footer span { flex:1; font-size:12px; color:#94a3b8; } .stats { max-height:130px; overflow:auto; }
    @media(max-width:750px) { .audit { flex:none; } .workspace { display:block; } .scroll { max-height:400px; } aside { border-left:0; border-top:1px solid #334155; } .intro { max-height:none; } .filters label { width:100%; } select { max-width:100%; } }
</style>
