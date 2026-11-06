<script>
    import { onMount } from "svelte";

    let {
        buses = [],
        selectedCount = 0,
        onAdd = (_name, _routeSelection) => {},
        onClose = () => {},
    } = $props();

    let name = $state("");
    let routeSelection = $state(true);
    let input;
    let trimmed = $derived(name.trim());
    let duplicate = $derived(buses.some((bus) => (bus.name || "").trim().toLowerCase() === trimmed.toLowerCase()));

    onMount(() => input?.focus());

    const submit = () => {
        if (!trimmed) return;
        onAdd(trimmed, selectedCount > 0 && routeSelection);
    };
    const closeFromBackdrop = (event) => {
        if (event.target === event.currentTarget) onClose();
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={closeFromBackdrop}>
    <dialog class="panel" aria-label="Add Audio Bus" open>
        <form onsubmit={(event) => { event.preventDefault(); submit(); }}>
            <header>
                <h2>Add Audio Bus</h2>
                <p>A stereo bus for a submix or a shared send effect. It feeds Master.</p>
            </header>
            <label class="name">
                <span>Name</span>
                <input bind:this={input} bind:value={name} placeholder="e.g. Strings" aria-label="New bus name"
                    autocomplete="off" spellcheck="false" />
            </label>
            {#if duplicate}
                <p class="hint">A bus named “{trimmed}” already exists.</p>
            {/if}
            {#if selectedCount > 0}
                <label class="route">
                    <input type="checkbox" bind:checked={routeSelection} />
                    <span>Route the {selectedCount} selected {selectedCount === 1 ? "layer" : "layers"} to this bus</span>
                </label>
            {/if}
            <footer>
                <button type="button" onclick={onClose}>Cancel</button>
                <button type="submit" class="primary" disabled={!trimmed}>Add Bus</button>
            </footer>
        </form>
    </dialog>
</div>

<style>
    .backdrop { position: absolute; inset: 0; z-index: 52; display: flex; align-items: center; justify-content: center; background: rgba(2, 6, 23, 0.62); backdrop-filter: blur(2px); }
    .panel { position: relative; margin: 0; padding: 0; width: min(440px, calc(100% - 48px)); overflow: hidden; background: #0b1220; border: 1px solid #334155; border-radius: 12px; box-shadow: 0 16px 48px rgba(0, 0, 0, 0.5); color: #e2e8f0; }
    form { display: flex; flex-direction: column; gap: 14px; padding: 22px 24px 20px; }
    header h2 { margin: 0; color: #f8fafc; font-size: 1.25rem; }
    header p { margin: 4px 0 0; color: #94a3b8; font-size: 0.82rem; line-height: 1.4; }
    .name { display: flex; flex-direction: column; gap: 6px; }
    .name > span { color: #94a3b8; font-size: 0.72rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; }
    input:not([type="checkbox"]) { min-height: 38px; padding: 8px 11px; box-sizing: border-box; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; outline: none; }
    input:not([type="checkbox"]):focus { border-color: #22d3ee; }
    .hint { margin: -6px 0 0; color: #fbbf24; font-size: 0.78rem; }
    .route { display: flex; align-items: center; gap: 8px; color: #cbd5e1; font-size: 0.82rem; cursor: pointer; }
    .route input { accent-color: #22d3ee; }
    footer { display: flex; justify-content: flex-end; gap: 10px; margin-top: 4px; }
    button { min-height: 36px; min-width: 84px; padding: 7px 14px; box-sizing: border-box; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; font-weight: 600; cursor: pointer; }
    button:hover:not(:disabled) { border-color: #7dd3fc; background: #1e3048; }
    button:disabled { opacity: 0.38; cursor: default; }
    .primary { border-color: #0e7490; background: #155e75; color: #ecfeff; }
    .primary:hover:not(:disabled) { border-color: #22d3ee; background: #0e7490; }
</style>
