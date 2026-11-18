<script>
    import { tick, untrack } from "svelte";
    import { dispatchCpp } from "./ipc.js";
    import { portal } from "./portal.js";
    import ExpressionMapViewer from "./ExpressionMapViewer.svelte";
    import PercussionMapViewer from "./PercussionMapViewer.svelte";
    import MapImportDialog from "./MapImportDialog.svelte";
    import { KIND_LABELS, PICKER_FILTERS, coverageNote, pickerSections, sourceFileName } from "./libraryMaps.js";

    // Choose one of Fiddle's maps for a patch or layer (docs/maps.md). Closed,
    // a button names the current map. Open, a panel attached to <body> offers
    // search, filters, suggestions from how maps are already used, favorites
    // and imports.
    let {
        kind = "expression",
        maps = [],              // every map, from setLibraryMaps
        selectedId = "",
        selectedName = "",
        noneLabel = "None",
        patchId = "",           // suggestions: maps this patch and its layers use
        libraryId = "",         // the This library filter
        instrument = "",        // suggestions: maps used for this Dorico instrument
        onselect = () => {},
        onclear = () => {},
    } = $props();

    const labels = $derived(KIND_LABELS[kind]);
    let selected = $derived(maps.find((map) => map.kind === kind && map.entityId === selectedId));
    let currentLabel = $derived(selected ? selected.name
        : selectedId ? `${selectedName || selectedId} (not in Fiddle's maps)` : noneLabel);
    let open = $state(false);
    let query = $state("");
    let filter = $state("all");
    // The highlighted row, by map ID ("" is None, null is nothing). An ID, not
    // an index, so a row that vanishes (a favorite unstarred under the
    // Favorites filter) leaves nothing highlighted rather than another row.
    let activeId = $state("");
    let viewing = $state(false);
    let importing = $state("");    // "", "file" or "dorico"
    let notice = $state(null);     // { text, ok }
    let trigger = $state(null);
    let panel = $state(null);
    let searchInput = $state(null);
    let position = $state({ left: 0, top: 0, width: 420, maxHeight: 420, above: false });

    let sections = $derived(open ? pickerSections(maps, { kind, patchId, libraryId, instrument }, { query, filter }) : []);
    let rows = $derived(sections.flatMap((section) => section.items));
    // Index into rows; -1 is None, -2 is nothing (including a map no longer listed).
    let active = $derived.by(() => {
        if (activeId === "") return -1;
        const index = activeId === null ? -1 : rows.findIndex((row) => row.map.entityId === activeId);
        return index >= 0 ? index : -2;
    });
    const highlight = (index) => { activeId = index === -1 ? "" : rows[index]?.map.entityId ?? null; };

    const place = () => {
        if (!trigger) return;
        const rect = trigger.getBoundingClientRect();
        const width = Math.min(Math.max(rect.width, 420), window.innerWidth - 16);
        const left = Math.min(Math.max(8, rect.left), window.innerWidth - width - 8);
        const below = window.innerHeight - rect.bottom - 8;
        const above = rect.top - 8;
        const flip = below < 320 && above > below;
        const maxHeight = Math.min(480, Math.max(200, flip ? above : below));
        position = { left, width, maxHeight, above: flip,
            top: flip ? Math.max(8, rect.top - 4 - maxHeight) : rect.bottom + 4 };
    };
    const openPanel = async () => {
        query = "";
        filter = "all";
        notice = null;
        place();
        open = true;
        await tick();
        activeId = rows.some((row) => row.map.entityId === selectedId) ? selectedId : "";
        searchInput?.focus();
        scrollActiveIntoView();
    };
    const close = () => { open = false; trigger?.focus(); };
    const choose = (map) => {
        if (map) { if (map.entityId !== selectedId) onselect(map.entityId); }
        else if (selectedId) onclear();
        close();
    };
    const toggleFavorite = (map) => dispatchCpp("setLibraryMapFavorite", kind, map.entityId, !map.favorite);
    const scrollActiveIntoView = async () => {
        await tick();
        panel?.querySelector(`[data-row="${active}"]`)?.scrollIntoView({ block: "nearest" });
    };
    const onKeydown = (event) => {
        if (event.key === "Escape") {
            event.preventDefault();
            event.stopPropagation(); // close the panel, not the dialog it sits in
            close();
        } else if (event.target !== searchInput) {
            // Filter and import buttons keep their own keyboard behaviour.
        } else if (event.key === "ArrowDown") {
            event.preventDefault();
            highlight(Math.min(rows.length - 1, Math.max(-1, active + 1)));
            scrollActiveIntoView();
        } else if (event.key === "ArrowUp") {
            event.preventDefault();
            highlight(Math.max(-1, active - 1));
            scrollActiveIntoView();
        } else if (event.key === "Enter") {
            event.preventDefault();
            // Only an explicit None highlight clears the map.
            if (active === -1) choose(null);
            else if (active >= 0) choose(rows[active].map);
        }
    };
    // Typing or filtering moves the highlight to the first match.
    $effect(() => {
        query; filter;
        untrack(() => { if (open) highlight(rows.length ? 0 : -1); });
    });
    $effect(() => {
        if (!open) return;
        const onPointer = (event) => {
            if (!panel?.contains(event.target) && !trigger?.contains(event.target)) open = false;
        };
        const onResize = () => place();
        window.addEventListener("pointerdown", onPointer, true);
        window.addEventListener("resize", onResize);
        return () => {
            window.removeEventListener("pointerdown", onPointer, true);
            window.removeEventListener("resize", onResize);
        };
    });
    let importRun = $state(0); // each import is a new dialog and request
    const startImport = (origin) => { open = false; importing = origin; importRun += 1; };
    const importDone = ({ ok, message, imported }) => {
        // One map imported from here is also chosen.
        if (ok && imported.length === 1) onselect(imported[0]);
        notice = { text: message, ok };
    };
    const detail = (map) => [map.creator, sourceFileName(map) || (map.sourcePath ? "" : "Made in Fiddle")]
        .filter(Boolean).join(" · ");
    let rowIndex = $derived(new Map(rows.map((row, index) => [row.map.entityId, index])));
</script>

<div class="map-picker">
    <button class="trigger" bind:this={trigger} aria-haspopup="listbox" aria-expanded={open}
        aria-label="{labels.noun[0].toUpperCase()}{labels.noun.slice(1)}: {currentLabel}"
        title={selected ? `${selected.name} (${selected.entityId})` : currentLabel}
        onclick={() => (open ? close() : openPanel())}>
        <span class="trigger-name" class:missing={selectedId && !selected}>{currentLabel}</span>
        {#if selected && coverageNote(selected, instrument)}<span class="trigger-note">{coverageNote(selected, instrument)}</span>{/if}
        <span class="chevron" aria-hidden="true">▾</span>
    </button>
    <button class="view" disabled={!selected} title="View the selected {labels.noun}" onclick={() => (viewing = true)}>View</button>
</div>
{#if notice}
    <p class="picker-notice" class:ok={notice.ok} role="status">{notice.text}
        <button class="dismiss" aria-label="Dismiss" onclick={() => (notice = null)}>✕</button></p>
{/if}

{#if open}
    <!-- svelte-ignore a11y_no_static_element_interactions -->
    <div class="panel" use:portal bind:this={panel} onkeydown={onKeydown}
        style="left: {position.left}px; top: {position.top}px; width: {position.width}px; max-height: {position.maxHeight}px">
        <div class="panel-head">
            <input bind:this={searchInput} type="search" bind:value={query} aria-label="Search {labels.plural}"
                placeholder="Search {labels.plural} by name, vendor or file" />
            <div class="filters" role="radiogroup" aria-label="Show">
                {#each PICKER_FILTERS as option (option.key)}
                    <button role="radio" aria-checked={filter === option.key} class:on={filter === option.key}
                        disabled={option.key === "library" && !libraryId}
                        onclick={() => { filter = option.key; searchInput?.focus(); }}>{option.label}</button>
                {/each}
            </div>
        </div>
        <ul class="rows" role="listbox" aria-label={labels.title}>
            <!-- The search box handles the keyboard for the whole list (↑ ↓ Enter Escape). -->
            <!-- svelte-ignore a11y_click_events_have_key_events -->
            <li role="option" aria-selected={!selectedId} data-row="-1" class="row none" class:active={active === -1}
                onpointerenter={() => highlight(-1)} onclick={() => choose(null)}>
                <span class="row-main"><span class="row-name">{noneLabel}</span></span>
            </li>
            {#each sections as section (section.key)}
                <li class="section" role="presentation">{section.title}</li>
                {#each section.items as item (item.map.entityId)}
                    {@const index = rowIndex.get(item.map.entityId)}
                    {@const note = coverageNote(item.map, instrument)}
                    <!-- svelte-ignore a11y_click_events_have_key_events -->
                    <li role="option" aria-selected={item.map.entityId === selectedId} data-row={index}
                        class="row" class:active={active === index} class:current={item.map.entityId === selectedId}
                        onpointerenter={() => highlight(index)} onclick={() => choose(item.map)}>
                        <!-- Starring keeps the focus in the search box. -->
                        <button class="star" class:starred={item.map.favorite} tabindex="-1"
                            onmousedown={(event) => event.preventDefault()}
                            aria-label={item.map.favorite ? `Remove ${item.map.name} from favorites` : `Add ${item.map.name} to favorites`}
                            aria-pressed={Boolean(item.map.favorite)}
                            onclick={(event) => { event.stopPropagation(); toggleFavorite(item.map); }}>{item.map.favorite ? "★" : "☆"}</button>
                        <span class="row-main">
                            <span class="row-name">{item.map.name}{#if item.map.entityId === selectedId}<span class="tag">Current</span>{/if}</span>
                            <span class="row-detail">
                                {#if item.reason}<span class="reason">{item.reason}</span>{/if}
                                {#if note}<span class="note">{note}</span>{/if}
                                {#if (item.map.uses || []).length}<span title={(item.map.references || []).join("\n")}>Used by {(item.map.uses || []).length}</span>{/if}
                                <span class="file">{detail(item.map)}</span>
                            </span>
                        </span>
                    </li>
                {/each}
            {/each}
            {#if !rows.length}
                <li class="empty" role="presentation">
                    {filter === "favorites" && !query ? `No favorite ${labels.plural} yet. Star one with ☆.`
                        : `No ${labels.plural} match. Import one below.`}
                </li>
            {/if}
        </ul>
        <div class="panel-foot">
            <button onclick={() => startImport("file")}>Import .doricolib…</button>
            <button onclick={() => startImport("dorico")}>Find in Dorico Library…</button>
        </div>
    </div>
{/if}

{#if importing}
    {#key importRun}
        <MapImportDialog {kind} origin={importing} onclose={() => (importing = "")} ondone={importDone} />
    {/key}
{/if}
{#if viewing && selected}
    {#if kind === "percussion"}
        <PercussionMapViewer map={selected.map} {instrument} onclose={() => (viewing = false)} />
    {:else}
        <ExpressionMapViewer entityId={selected.entityId} onclose={() => (viewing = false)} />
    {/if}
{/if}

<style>
    /* Overlays sit above the Library Manager's editor modal (z-index 99999). */
    .map-picker { display: flex; gap: 6px; min-width: 0; }
    .trigger { flex: 1; min-width: 0; display: flex; align-items: center; gap: 6px; text-align: left; cursor: pointer; }
    .trigger-name { flex: 1; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .trigger-name.missing { color: #fbbf24; }
    .trigger-note { flex: none; font-size: .72em; color: #fbbf24; }
    .chevron { flex: none; opacity: .7; }
    .view { flex: none; }
    .picker-notice { display: flex; align-items: center; justify-content: space-between; gap: 8px; margin: 6px 0 0;
        padding: 6px 8px; border-radius: 4px; font-size: .74rem; color: #ff8787; background: rgba(255,107,107,.1); }
    .picker-notice.ok { color: #69db7c; background: rgba(64,192,87,.1); }
    .dismiss { border: 0; background: none; color: inherit; cursor: pointer; padding: 0 4px; }

    .panel { position: fixed; z-index: 100300; display: flex; flex-direction: column; box-sizing: border-box;
        background: #0f1528; border: 1px solid #3b4a6b; border-radius: 8px; box-shadow: 0 14px 40px rgba(0,0,0,.55);
        color: #e2e8f0; font-family: "Inter", sans-serif; font-size: .8rem; overflow: hidden; }
    .panel-head { display: flex; flex-direction: column; gap: 8px; padding: 10px; border-bottom: 1px solid #2a3654; }
    .panel-head input { width: 100%; box-sizing: border-box; padding: 7px 9px; border-radius: 5px; border: 1px solid #3b4a6b;
        background: #16203a; color: #e2e8f0; font: inherit; }
    .filters { display: flex; gap: 6px; }
    .filters button, .panel-foot button { padding: 4px 10px; border-radius: 999px; border: 1px solid #3b4a6b;
        background: transparent; color: #a5b4d4; font: inherit; font-size: .74rem; cursor: pointer; }
    .filters button.on { background: #1e3a6e; border-color: #5b8def; color: #e2e8f0; }
    .filters button:disabled { opacity: .4; cursor: default; }
    .rows { list-style: none; margin: 0; padding: 4px 0; overflow-y: auto; flex: 1; min-height: 0; }
    .section { padding: 8px 12px 4px; color: #7c8db5; font-size: .66rem; font-weight: 700; letter-spacing: .07em;
        text-transform: uppercase; }
    .row { display: flex; align-items: flex-start; gap: 6px; padding: 6px 10px; cursor: pointer; }
    .row.active { background: #1c2b4d; }
    .row.current .row-name { color: #93c5fd; }
    .row.none .row-main { padding-left: 26px; }
    .row-main { flex: 1; min-width: 0; display: flex; flex-direction: column; gap: 2px; }
    .row-name { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .tag { margin-left: 6px; padding: 0 5px; border-radius: 3px; background: #1e3a6e; color: #bfdbfe; font-size: .66rem; }
    .row-detail { display: flex; flex-wrap: wrap; gap: 4px 10px; color: #7c8db5; font-size: .7rem; }
    .reason { color: #86efac; }
    .note { color: #fbbf24; }
    .file { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; max-width: 100%; }
    .star { flex: none; width: 20px; padding: 0; border: 0; background: none; color: #5b6b8f; font-size: .95rem;
        line-height: 1.2; cursor: pointer; }
    .star.starred, .star:hover { color: #fbbf24; }
    .empty { padding: 14px 12px; color: #7c8db5; text-align: center; }
    .panel-foot { display: flex; gap: 6px; justify-content: flex-end; padding: 8px 10px; border-top: 1px solid #2a3654; }
</style>
