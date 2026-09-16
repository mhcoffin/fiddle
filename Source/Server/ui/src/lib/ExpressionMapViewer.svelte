<script>
    import { onMount, tick } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import {
        expressionMapActionsText,
        expressionMapDynamicsText,
        filterExpressionMapCombinations,
        selectExpressionMapCombination,
    } from "./expressionMapViewer.js";

    let { entityId, onclose = () => {} } = $props();

    let dialog = $state();
    let searchInput = $state();
    let data = $state(null);
    let loading = $state(true);
    let query = $state("");
    let requestedIndex = $state(null);

    let filtered = $derived.by(() => {
        const combinations = data?.combinations ?? [];
        return filterExpressionMapCombinations(combinations, query);
    });

    let selected = $derived(selectExpressionMapCombination(filtered, requestedIndex));

    const closeViewer = () => {
        if (dialog?.open) dialog.close();
        else onclose();
    };

    const handleBackdropClick = (event) => {
        if (event.target === dialog) closeViewer();
    };

    onMount(() => {
        const unsubscribe = onFromCpp("setExpressionMapDetails", async (details) => {
            if (details?.entityID !== entityId) return;
            data = details;
            loading = false;
            requestedIndex = details?.combinations?.[0]?.index ?? null;
            await tick();
            searchInput?.focus();
        });

        dialog?.showModal();
        dispatchCpp("requestExpressionMapDetails", entityId);
        return unsubscribe;
    });
</script>

<dialog
    bind:this={dialog}
    class="viewer"
    aria-labelledby="expression-map-viewer-title"
    onclick={handleBackdropClick}
    onclose={onclose}
>
    <div class="viewer-panel">
        <header>
            <div class="heading">
                <p class="eyebrow">Expression map</p>
                <h2 id="expression-map-viewer-title">{data?.name || "Loading…"}</h2>
                {#if data?.found}
                    <p class="metadata">
                        {data.creator || "Unknown creator"}
                        {#if data.version} · Version {data.version}{/if}
                        · {data.combinations?.length ?? 0} switches
                    </p>
                {/if}
            </div>
            <button type="button" class="close" onclick={closeViewer}>Close</button>
        </header>

        {#if loading}
            <div class="message">Loading expression map…</div>
        {:else if !data?.found}
            <div class="message error">
                <strong>Expression map unavailable</strong>
                <span>{data?.error || "Fiddle could not load this expression map."}</span>
            </div>
        {:else}
            {#if data.description}
                <p class="description">{data.description}</p>
            {/if}

            <div class="toolbar">
                <label for="expression-map-search">Find a switch</label>
                <input
                    bind:this={searchInput}
                    bind:value={query}
                    id="expression-map-search"
                    type="search"
                    placeholder="Technique, MIDI action, or condition…"
                    autocomplete="off"
                />
                <span>{filtered.length} of {data.combinations.length}</span>
            </div>

            <div class="workspace">
                <section class="switches" aria-label="Technique switches">
                    <div class="table-scroll">
                        <table>
                            <thead>
                                <tr>
                                    <th>Type</th>
                                    <th>Name</th>
                                    <th>Playback techniques</th>
                                    <th>Switch on</th>
                                    <th>Switch off</th>
                                </tr>
                            </thead>
                            <tbody>
                                {#each filtered as combination (combination.index)}
                                    <tr class:selected={selected?.index === combination.index}>
                                        <td><span class:addon={combination.isAddOn} class="kind">{combination.isAddOn ? "Add-on" : "Base"}</span></td>
                                        <td>
                                            <button
                                                type="button"
                                                class="select-row"
                                                onclick={() => (requestedIndex = combination.index)}
                                            >{combination.name || "Unnamed switch"}</button>
                                        </td>
                                        <td>{combination.techniqueIDs?.join(" + ") || "—"}</td>
                                        <td>{expressionMapActionsText(combination.switchOnActions)}</td>
                                        <td>{expressionMapActionsText(combination.switchOffActions)}</td>
                                    </tr>
                                {:else}
                                    <tr><td colspan="5" class="empty">No switches match “{query}”.</td></tr>
                                {/each}
                            </tbody>
                        </table>
                    </div>
                </section>

                <aside class="details" aria-label="Selected switch details">
                    {#if selected}
                        <div class="detail-heading">
                            <span>{selected.isAddOn ? "Add-on switch" : "Base switch"}</span>
                            <h3>{selected.name || "Unnamed switch"}</h3>
                        </div>

                        <dl>
                            <div><dt>Switch on</dt><dd>{expressionMapActionsText(selected.switchOnActions)}</dd></div>
                            <div><dt>Switch off</dt><dd>{expressionMapActionsText(selected.switchOffActions)}</dd></div>
                            <div><dt>Condition</dt><dd>{selected.condition || "None"}</dd></div>
                            <div><dt>Pre-roll</dt><dd>{selected.ticksBefore || 0} ticks · {selected.millisecondsBefore || 0} ms</dd></div>
                            <div><dt>Velocity range</dt><dd>{selected.velocityMin}–{selected.velocityMax}</dd></div>
                            <div><dt>Pitch range</dt><dd>{selected.pitchMin}–{selected.pitchMax}</dd></div>
                            <div><dt>Transpose</dt><dd>{selected.transpose > 0 ? "+" : ""}{selected.transpose} semitones</dd></div>
                            <div><dt>Primary dynamics</dt><dd>{expressionMapDynamicsText(selected.volumeType, selected.volumeCC)}</dd></div>
                            <div><dt>Secondary dynamics</dt><dd>{expressionMapDynamicsText(selected.volumeType2, selected.volumeCC2)}</dd></div>
                            <div><dt>Velocity factor</dt><dd>{selected.velocityFactor}</dd></div>
                            <div><dt>Length factor</dt><dd>{selected.lengthFactor}</dd></div>
                            <div><dt>Monophonic</dt><dd>{selected.monophonic ? "Yes" : "No"}</dd></div>
                            <div><dt>Base switch ID</dt><dd>{selected.baseSwitchID}</dd></div>
                        </dl>
                    {:else}
                        <div class="message">Select a switch to inspect it.</div>
                    {/if}
                </aside>
            </div>

            <details class="map-settings">
                <summary>Map settings</summary>
                <div class="settings-grid">
                    <section>
                        <h3>General</h3>
                        <dl>
                            <div><dt>Entity ID</dt><dd>{data.entityID}</dd></div>
                            <div><dt>Source</dt><dd class="path">{data.sourcePath || "—"}</dd></div>
                            <div><dt>Pitch-bend range</dt><dd>{data.pitchBendRange} semitones</dd></div>
                            <div><dt>Automatic mutual exclusion</dt><dd>{data.autoMutualExclusion ? "On" : "Off"}</dd></div>
                        </dl>
                    </section>
                    <section>
                        <h3>Playback durations</h3>
                        <dl>
                            {#each Object.entries(data.timing ?? {}) as [name, percent]}
                                <div><dt>{name}</dt><dd>{percent}%</dd></div>
                            {/each}
                        </dl>
                    </section>
                    <section>
                        <h3>Mutual-exclusion groups</h3>
                        {#if data.mutualExclusionGroups?.length}
                            {#each data.mutualExclusionGroups as group}
                                <div class="meg">
                                    <strong>{group.name}</strong>
                                    <span>{group.techniqueIDs.join(", ")}</span>
                                    {#if group.defaultID}<small>Default: {group.defaultID}</small>{/if}
                                </div>
                            {/each}
                        {:else}
                            <p class="muted">No explicit groups.</p>
                        {/if}
                    </section>
                </div>
            </details>
        {/if}
    </div>
</dialog>

<style>
    .viewer {
        width: min(1180px, calc(100vw - 32px));
        height: min(790px, calc(100vh - 32px));
        max-width: none;
        max-height: none;
        padding: 0;
        overflow: hidden;
        border: 1px solid #475569;
        border-radius: 10px;
        background: #0b1220;
        color: #e2e8f0;
        box-shadow: 0 24px 80px rgba(0, 0, 0, 0.7);
    }
    .viewer::backdrop { background: rgba(2, 6, 23, 0.78); }
    .viewer-panel { display:flex; flex-direction:column; height:100%; box-sizing:border-box; padding:20px; gap:14px; }
    header { display:flex; align-items:flex-start; justify-content:space-between; gap:20px; }
    .heading { min-width:0; }
    .eyebrow { margin:0 0 3px; color:#60a5fa; font-size:.7rem; font-weight:750; letter-spacing:.09em; text-transform:uppercase; }
    h2 { margin:0; overflow:hidden; color:#f8fafc; font-size:1.25rem; text-overflow:ellipsis; white-space:nowrap; }
    .metadata { margin:5px 0 0; color:#94a3b8; font-size:.78rem; }
    .close { padding:7px 13px; border:1px solid #475569; border-radius:5px; background:#1e293b; color:#dbeafe; cursor:pointer; }
    .close:hover, .close:focus-visible { border-color:#60a5fa; color:#fff; outline:none; }
    .description { margin:0; color:#cbd5e1; font-size:.82rem; line-height:1.45; }
    .toolbar { display:grid; grid-template-columns:auto minmax(220px, 430px) 1fr; align-items:center; gap:10px; }
    .toolbar label { color:#cbd5e1; font-size:.78rem; font-weight:650; }
    .toolbar input { min-height:36px; padding:7px 10px; border:1px solid #475569; border-radius:5px; background:#020617; color:#f8fafc; font:inherit; font-size:.85rem; }
    .toolbar input:focus { border-color:#60a5fa; outline:2px solid rgba(96,165,250,.2); }
    .toolbar span { color:#64748b; font-size:.74rem; }
    .workspace { display:grid; grid-template-columns:minmax(0, 1.7fr) minmax(270px, .7fr); flex:1; min-height:0; overflow:hidden; border:1px solid #263449; border-radius:7px; }
    .switches { min-width:0; min-height:0; background:#0f172a; }
    .table-scroll { width:100%; height:100%; overflow:auto; }
    table { width:100%; border-collapse:collapse; table-layout:fixed; font-size:.76rem; }
    th { position:sticky; top:0; z-index:1; padding:8px 9px; border-bottom:1px solid #334155; background:#172033; color:#94a3b8; font-size:.68rem; letter-spacing:.035em; text-align:left; text-transform:uppercase; }
    th:nth-child(1) { width:58px; }
    th:nth-child(2) { width:20%; }
    th:nth-child(3) { width:25%; }
    th:nth-child(4), th:nth-child(5) { width:20%; }
    td { padding:7px 9px; overflow:hidden; border-bottom:1px solid #1e293b; color:#cbd5e1; text-overflow:ellipsis; white-space:nowrap; }
    tr.selected td { background:rgba(37,99,235,.18); color:#dbeafe; }
    .kind { display:inline-block; padding:2px 5px; border-radius:4px; background:#243147; color:#cbd5e1; font-size:.63rem; font-weight:700; text-transform:uppercase; }
    .kind.addon { background:#3b2745; color:#f0abfc; }
    .select-row { width:100%; padding:0; overflow:hidden; border:0; background:transparent; color:inherit; font:inherit; font-weight:650; text-align:left; text-overflow:ellipsis; white-space:nowrap; cursor:pointer; }
    .select-row:focus-visible { color:#93c5fd; outline:1px solid #60a5fa; outline-offset:2px; }
    .empty { padding:28px; color:#94a3b8; text-align:center; }
    .details { min-width:0; overflow-y:auto; padding:16px; border-left:1px solid #263449; background:#111a2a; }
    .detail-heading span { color:#60a5fa; font-size:.65rem; font-weight:750; letter-spacing:.06em; text-transform:uppercase; }
    .detail-heading h3 { margin:4px 0 14px; color:#f8fafc; font-size:1rem; }
    dl { margin:0; }
    dl > div { display:grid; grid-template-columns:minmax(95px, .8fr) minmax(0, 1.3fr); gap:10px; padding:7px 0; border-top:1px solid #263449; }
    dt { color:#94a3b8; font-size:.7rem; }
    dd { margin:0; overflow-wrap:anywhere; color:#e2e8f0; font-size:.74rem; }
    .map-settings { flex:0 0 auto; max-height:190px; overflow:auto; border:1px solid #263449; border-radius:6px; background:#0f172a; }
    .map-settings summary { position:sticky; top:0; z-index:1; padding:9px 12px; background:#172033; color:#cbd5e1; font-size:.76rem; font-weight:700; cursor:pointer; }
    .settings-grid { display:grid; grid-template-columns:1fr .75fr 1.2fr; gap:20px; padding:13px; }
    .settings-grid h3 { margin:0 0 8px; color:#93c5fd; font-size:.76rem; }
    .settings-grid dl > div { padding:4px 0; }
    .path { font-family:ui-monospace, SFMono-Regular, Menlo, monospace; font-size:.65rem; }
    .meg { display:flex; flex-direction:column; gap:2px; padding:6px 0; border-top:1px solid #263449; font-size:.7rem; }
    .meg span, .muted { color:#94a3b8; }
    .meg small { color:#60a5fa; }
    .message { display:flex; flex:1; align-items:center; justify-content:center; color:#94a3b8; }
    .message.error { flex-direction:column; gap:7px; color:#fca5a5; }
    .message.error span { color:#cbd5e1; }
    @media (max-width: 820px) {
        .workspace { grid-template-columns:1fr; overflow:auto; }
        .switches { min-height:310px; }
        .details { border-top:1px solid #263449; border-left:0; }
        .settings-grid { grid-template-columns:1fr; }
    }
</style>
