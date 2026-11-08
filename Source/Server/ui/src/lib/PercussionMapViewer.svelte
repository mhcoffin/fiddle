<script>
    import { onMount, tick } from "svelte";
    import { filterPercussionEntries, formatTechniques, instrumentLabel, pitchName } from "./percussionMaps.js";

    // Read-only view of a library percussion map (docs/library-percussion-maps.md):
    // which pitch and keyswitches each instrument + technique set plays.
    // Editing is in the Library Manager.
    let { map, instrument = "", onclose = () => {} } = $props();

    let dialog = $state();
    let searchInput = $state();
    let query = $state("");
    const canonical = (id) => String(id || "").split(".alias.")[0];
    let entries = $derived(map?.entries || []);
    let instruments = $derived([...new Set(entries.map((entry) => entry.instrument))]);
    // A multi-instrument map that covers this layer's chair can show just its rows.
    let canNarrow = $derived(Boolean(instrument) && instruments.length > 1
        && instruments.some((id) => canonical(id) === canonical(instrument)));
    let onlyThisInstrument = $state(true);
    let shown = $derived(filterPercussionEntries(entries, query, canNarrow && onlyThisInstrument ? instrument : ""));

    const closeViewer = () => {
        if (dialog?.open) dialog.close();
        else onclose();
    };

    onMount(async () => {
        dialog?.showModal();
        await tick();
        searchInput?.focus();
    });
</script>

<dialog
    bind:this={dialog}
    class="viewer"
    aria-labelledby="percussion-map-viewer-title"
    onkeydown={(event) => {
        // Close just the viewer; a panel underneath (the Library Manager
        // editor) must not also see this Escape.
        if (event.key !== "Escape") return;
        event.preventDefault();
        event.stopPropagation();
        closeViewer();
    }}
    onclick={(event) => { if (event.target === dialog) closeViewer(); }}
    onclose={onclose}
>
    <div class="viewer-panel">
        <header>
            <div class="heading">
                <p class="eyebrow">Percussion map</p>
                <h2 id="percussion-map-viewer-title">{map?.name || "Percussion map"}</h2>
                <p class="metadata">{[
                    map?.entityId,
                    map?.version ? `Version ${map.version}` : "",
                    `${entries.length} ${entries.length === 1 ? "entry" : "entries"}`,
                    `${instruments.length} ${instruments.length === 1 ? "instrument" : "instruments"}`,
                ].filter(Boolean).join(" · ")}</p>
            </div>
            <button type="button" class="close" onclick={closeViewer}>Close</button>
        </header>

        <p class="description">Each note plays the entry whose techniques best match it: an exact combination beats its parts, and ties go to the earlier row. A technique with no entry plays the Natural entry. Edit maps in the Library Manager.</p>

        <div class="toolbar">
            <label for="percussion-map-search">Find an entry</label>
            <input
                bind:this={searchInput}
                bind:value={query}
                id="percussion-map-search"
                type="search"
                placeholder="Instrument, technique, pitch or name…"
                autocomplete="off"
            />
            {#if canNarrow}
                <label class="narrow"><input type="checkbox" bind:checked={onlyThisInstrument} /> Only {instrumentLabel(instrument)}</label>
            {/if}
            <span>{shown.length} of {entries.length}</span>
        </div>

        <div class="table-scroll">
            <table>
                <thead>
                    <tr>
                        <th>#</th>
                        <th>Instrument</th>
                        <th>Playback techniques</th>
                        <th>Pitch</th>
                        <th>Keyswitches</th>
                        <th>Entry name</th>
                    </tr>
                </thead>
                <tbody>
                    {#each shown as entry (entry.row)}
                        <tr class:this-instrument={instrument && canonical(entry.instrument) === canonical(instrument)}>
                            <td class="row-number">{entry.row}</td>
                            <td title={entry.instrument}>{instrumentLabel(entry.instrument)}</td>
                            <td>{formatTechniques(entry.techniques).split("+").join(" + ") || "—"}</td>
                            <td class="pitch">{entry.pitch} <span>{pitchName(entry.pitch)}</span></td>
                            <td>{(entry.keyswitches || []).length ? entry.keyswitches.map((key) => `${key} (${pitchName(key)})`).join(", ") : "—"}</td>
                            <td>{entry.name || "—"}{#if entry.variant} <span class="variant">{entry.variant}</span>{/if}</td>
                        </tr>
                    {:else}
                        <tr><td colspan="6" class="empty">{entries.length ? `No entries match “${query}”.` : "This map has no entries."}</td></tr>
                    {/each}
                </tbody>
            </table>
        </div>
    </div>
</dialog>

<style>
    .viewer {
        width: min(1000px, calc(100vw - 32px));
        height: min(720px, calc(100vh - 32px));
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
    .metadata { margin:5px 0 0; color:#94a3b8; font-size:.78rem; overflow-wrap:anywhere; }
    .close { padding:7px 13px; border:1px solid #475569; border-radius:5px; background:#1e293b; color:#dbeafe; cursor:pointer; }
    .close:hover, .close:focus-visible { border-color:#60a5fa; color:#fff; outline:none; }
    .description { margin:0; color:#cbd5e1; font-size:.82rem; line-height:1.45; }
    .toolbar { display:flex; flex-wrap:wrap; align-items:center; gap:10px; }
    .toolbar > label { color:#cbd5e1; font-size:.78rem; font-weight:650; }
    .toolbar input[type="search"] { flex:0 1 380px; min-width:180px; min-height:36px; padding:7px 10px; border:1px solid #475569; border-radius:5px; background:#020617; color:#f8fafc; font:inherit; font-size:.85rem; }
    .toolbar input[type="search"]:focus { border-color:#60a5fa; outline:2px solid rgba(96,165,250,.2); }
    .toolbar .narrow { display:flex; align-items:center; gap:6px; font-weight:500; }
    .toolbar span { color:#64748b; font-size:.74rem; }
    .table-scroll { flex:1; min-height:0; overflow:auto; border:1px solid #263449; border-radius:7px; background:#0f172a; }
    table { width:100%; border-collapse:collapse; font-size:.78rem; }
    th { position:sticky; top:0; z-index:1; padding:8px 9px; border-bottom:1px solid #334155; background:#172033; color:#94a3b8; font-size:.68rem; letter-spacing:.035em; text-align:left; text-transform:uppercase; }
    td { padding:7px 9px; border-bottom:1px solid #1e293b; color:#cbd5e1; vertical-align:top; }
    tr.this-instrument td { color:#e2e8f0; }
    .row-number { width:32px; color:#64748b; font-variant-numeric:tabular-nums; }
    .pitch { white-space:nowrap; font-variant-numeric:tabular-nums; }
    .pitch span, .variant { color:#94a3b8; }
    .variant { margin-left:4px; font-size:.7rem; }
    .empty { padding:24px; color:#94a3b8; text-align:center; }
</style>
