<script>
    import ExpressionMapViewer from "./ExpressionMapViewer.svelte";

    // A patch's or layer's expression map, chosen from its library's maps
    // (docs/library-map-import.md). The list is short, so a drop-down.
    let {
        maps = [],          // this library's expression maps
        libraryId = "",
        selectedId = "",
        selectedName = "",
        noneLabel = "None",
        canLoadFromFile = false,
        onselect = () => {},
        onclear = () => {},
        onloadfile = () => {},
    } = $props();

    let viewing = $state(false);
    let selectedKnown = $derived(maps.some((map) => map.entityId === selectedId));
</script>

<div class="expression-select">
    <select
        aria-label="Expression map"
        value={selectedId}
        onchange={(event) => {
            const id = event.currentTarget.value;
            if (id) onselect(id);
            else onclear();
        }}
    >
        <option value="">{noneLabel}</option>
        {#if selectedId && !selectedKnown}
            <option value={selectedId}>{selectedName || selectedId} (not in library)</option>
        {/if}
        {#each maps as map (map.entityId)}
            <option value={map.entityId}>{map.name}</option>
        {/each}
    </select>
    <button disabled={!selectedKnown} title="View the selected expression map" onclick={() => (viewing = true)}>View</button>
    {#if canLoadFromFile}
        <button onclick={onloadfile}>Load from file…</button>
    {/if}
</div>
{#if viewing && selectedKnown}
    <ExpressionMapViewer entityId={selectedId} {libraryId} onclose={() => (viewing = false)} />
{/if}

<style>
    .expression-select { display: flex; gap: 6px; min-width: 0; }
    select { flex: 1; min-width: 0; }
    button { flex: none; }
</style>
