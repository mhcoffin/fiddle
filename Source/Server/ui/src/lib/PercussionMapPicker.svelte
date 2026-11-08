<script>
    import PercussionMapViewer from "./PercussionMapViewer.svelte";
    import { sortForInstrument } from "./percussionMaps.js";

    // A layer's library percussion map: maps covering the chair's instrument
    // first. Maps are created and imported in the Library Manager.
    let {
        maps = [],
        instrument = "",
        selectedId = "",
        selectedName = "",
        noneLabel = "No percussion map (pitch 36 passes through)",
        canLoadFromFile = true,
        onselect = () => {},
        onclear = () => {},
        onloadfile = () => {},
    } = $props();

    const canonical = (id) => String(id || "").split(".alias.")[0];
    const covers = (map) => (map.instruments || []).some((id) => canonical(id) === canonical(instrument));
    let ordered = $derived(sortForInstrument(maps, instrument));
    let selectedKnown = $derived(maps.some((map) => map.entityId === selectedId));
    let selectedMap = $derived(maps.find((map) => map.entityId === selectedId)?.map);
    let viewing = $state(false);
</script>

<div class="percussion-picker">
    <select
        aria-label="Percussion map"
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
        {#each ordered as map}
            <option value={map.entityId}>
                {map.name}{!instrument || covers(map) ? "" : (map.instruments || []).length === 1 ? " (other instrument)" : " (does not cover this instrument)"}
            </option>
        {/each}
    </select>
    <button disabled={!selectedMap} title="View the selected percussion map" onclick={() => (viewing = true)}>View</button>
    {#if canLoadFromFile}
        <button onclick={onloadfile}>Load from file…</button>
    {/if}
</div>
{#if viewing && selectedMap}
    <PercussionMapViewer map={selectedMap} {instrument} onclose={() => (viewing = false)} />
{/if}

<style>
    .percussion-picker { display: flex; gap: 6px; }
    select { flex: 1; min-width: 0; }
    button { flex: none; }
</style>
