<script>
    import { sortForInstrument } from "./percussionMaps.js";

    // A layer's library percussion map: maps covering the chair's instrument
    // first. Maps are created and imported in the Library Manager.
    let {
        maps = [],
        instrument = "",
        selectedId = "",
        selectedName = "",
        canLoadFromFile = true,
        onselect = () => {},
        onclear = () => {},
        onloadfile = () => {},
    } = $props();

    const canonical = (id) => String(id || "").split(".alias.")[0];
    const covers = (map) => (map.instruments || []).some((id) => canonical(id) === canonical(instrument));
    let ordered = $derived(sortForInstrument(maps, instrument));
    let selectedKnown = $derived(maps.some((map) => map.entityId === selectedId));
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
        <option value="">No percussion map (pitch 36 passes through)</option>
        {#if selectedId && !selectedKnown}
            <option value={selectedId}>{selectedName || selectedId} (not in library)</option>
        {/if}
        {#each ordered as map}
            <option value={map.entityId}>
                {map.name}{covers(map) ? "" : (map.instruments || []).length === 1 ? " (other instrument)" : " (does not cover this instrument)"}
            </option>
        {/each}
    </select>
    {#if canLoadFromFile}
        <button onclick={onloadfile}>Load from file…</button>
    {/if}
</div>

<style>
    .percussion-picker { display: flex; gap: 8px; }
    select { flex: 1; min-width: 0; }
    button { flex: none; }
</style>
