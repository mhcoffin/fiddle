<script>
    import { tick } from "svelte";
    import { groupExpressionMaps } from "./expressionMapCatalog.js";

    let {
        maps = [],
        selectedId = "",
        selectedName = "",
        onselect = () => {},
        onclear = () => {},
        onloadfile = () => {},
    } = $props();

    let dialog = $state();
    let searchInput = $state();
    let isOpen = $state(false);
    let query = $state("");
    let groups = $derived(isOpen ? groupExpressionMaps(maps, query) : []);
    let resultCount = $derived(groups.reduce((total, group) => total + group.items.length, 0));

    const openPicker = async () => {
        query = "";
        isOpen = true;
        await tick();
        dialog?.showModal();
        searchInput?.focus();
    };

    const closePicker = () => {
        dialog?.close();
    };

    const choose = (entityID) => {
        onselect(entityID);
        closePicker();
    };

    const clear = () => {
        onclear();
        closePicker();
    };

    const loadFromFile = () => {
        closePicker();
        onloadfile();
    };

    const handleBackdropClick = (event) => {
        if (event.target === dialog) closePicker();
    };

    const optionButtons = () =>
        Array.from(dialog?.querySelectorAll("[data-xmap-option]") ?? []);

    const moveOptionFocus = (event, offset) => {
        const options = optionButtons();
        if (options.length === 0) return;
        event.preventDefault();
        const current = options.indexOf(document.activeElement);
        const next = current < 0
            ? offset > 0 ? 0 : options.length - 1
            : (current + offset + options.length) % options.length;
        options[next].focus();
    };

    const handleDialogKeyDown = (event) => {
        if (event.key === "ArrowDown") moveOptionFocus(event, 1);
        else if (event.key === "ArrowUp") moveOptionFocus(event, -1);
    };
</script>

<div class="xmap-control">
    <button
        type="button"
        class="xmap-trigger"
        title={selectedName || "Choose an expression map"}
        aria-haspopup="dialog"
        onclick={openPicker}
    >
        <span
            class="xmap-value"
            class:xmap-placeholder={!selectedName}
        ><span>{selectedName || "— xmap —"}</span></span>
        <svg
            class="xmap-search-icon"
            aria-hidden="true"
            viewBox="0 0 24 24"
            fill="none"
            stroke="currentColor"
            stroke-width="2"
            stroke-linecap="round"
        >
            <circle cx="11" cy="11" r="6.5"></circle>
            <path d="m16 16 4 4"></path>
        </svg>
    </button>

    {#if isOpen}
        <dialog
            bind:this={dialog}
            class="xmap-dialog"
            aria-labelledby="xmap-dialog-title"
            onclick={handleBackdropClick}
            onkeydown={handleDialogKeyDown}
            onclose={() => (isOpen = false)}
        >
            <div class="xmap-dialog-panel">
                <header class="xmap-dialog-header">
                    <div>
                        <h2 id="xmap-dialog-title">Expression map</h2>
                        <p>{resultCount} {resultCount === 1 ? "map" : "maps"}</p>
                    </div>
                    <button
                        type="button"
                        class="xmap-close"
                        aria-label="Close expression map picker"
                        title="Close"
                        onclick={closePicker}
                    >&times;</button>
                </header>

                <label class="xmap-search-label" for="xmap-search">Find an expression map</label>
                <input
                    bind:this={searchInput}
                    bind:value={query}
                    id="xmap-search"
                    class="xmap-search"
                    type="search"
                    placeholder="Type a library, instrument, or creator…"
                    autocomplete="off"
                />

                <div class="xmap-actions">
                    <button type="button" disabled={!selectedName} onclick={clear}>Clear assignment</button>
                    <button type="button" onclick={loadFromFile}>Load from file…</button>
                </div>

                <div class="xmap-results" aria-label="Expression maps">
                    {#if groups.length === 0}
                        <div class="xmap-empty">No expression maps match “{query}”.</div>
                    {:else}
                        {#each groups as group}
                            <section class="xmap-group">
                                <h3>{group.name}</h3>
                                <div class="xmap-group-items">
                                    {#each group.items as map}
                                        <button
                                            type="button"
                                            data-xmap-option
                                            class="xmap-option"
                                            class:xmap-selected={map.entityID === selectedId}
                                            title={map.name}
                                            aria-current={map.entityID === selectedId ? "true" : undefined}
                                            onclick={() => choose(map.entityID)}
                                        >
                                            <span>{map.label}</span>
                                            {#if map.entityID === selectedId}
                                                <span class="xmap-current">Current</span>
                                            {/if}
                                        </button>
                                    {/each}
                                </div>
                            </section>
                        {/each}
                    {/if}
                </div>
            </div>
        </dialog>
    {/if}
</div>

<style>
    .xmap-control {
        margin-bottom: 2px;
    }

    .xmap-trigger {
        position: relative;
        width: 100%;
        min-height: 30px;
        overflow: hidden;
        padding: 0;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #cbd5e1;
        cursor: pointer;
        text-align: left;
    }
    .xmap-trigger:hover {
        border-color: #475569;
        background: #111c31;
    }
    .xmap-trigger:focus-visible {
        outline: none;
        border-color: #3b82f6;
        box-shadow: 0 0 0 1px #3b82f6;
    }
    .xmap-value {
        position: absolute;
        inset: 0 24px 0 6px;
        display: block;
        overflow: hidden;
        color: #cbd5e1;
        font-size: 0.75rem;
        line-height: 28px;
        white-space: nowrap;
        text-align: left;
        text-overflow: ellipsis;
        direction: rtl;
    }
    .xmap-value > span {
        direction: ltr;
        unicode-bidi: isolate;
    }
    .xmap-placeholder {
        color: #94a3b8;
        direction: ltr;
    }
    .xmap-search-icon {
        position: absolute;
        top: 50%;
        right: 6px;
        width: 16px;
        height: 16px;
        color: #94a3b8;
        transform: translateY(-50%);
        pointer-events: none;
    }
    .xmap-trigger:hover .xmap-search-icon,
    .xmap-trigger:focus-visible .xmap-search-icon {
        color: #bfdbfe;
    }

    .xmap-dialog {
        width: min(620px, calc(100vw - 32px));
        height: min(650px, calc(100vh - 48px));
        max-width: none;
        max-height: none;
        padding: 0;
        overflow: hidden;
        border: 1px solid #475569;
        border-radius: 10px;
        background: #0f172a;
        color: #e2e8f0;
        box-shadow: 0 24px 80px rgba(0, 0, 0, 0.65);
    }
    .xmap-dialog::backdrop {
        background: rgba(2, 6, 23, 0.72);
    }
    .xmap-dialog-panel {
        display: flex;
        flex-direction: column;
        height: 100%;
        box-sizing: border-box;
        padding: 20px;
        gap: 12px;
    }
    .xmap-dialog-header {
        display: flex;
        align-items: flex-start;
        justify-content: space-between;
        gap: 16px;
    }
    .xmap-dialog-header h2 {
        margin: 0;
        color: #f8fafc;
        font-size: 1.15rem;
    }
    .xmap-dialog-header p {
        margin: 4px 0 0;
        color: #94a3b8;
        font-size: 0.8rem;
    }
    .xmap-close {
        width: 36px;
        height: 36px;
        padding: 0;
        border: 1px solid #334155;
        border-radius: 6px;
        background: #111827;
        color: #cbd5e1;
        font-size: 1.5rem;
        line-height: 1;
        cursor: pointer;
    }
    .xmap-close:hover,
    .xmap-close:focus-visible {
        border-color: #60a5fa;
        color: #fff;
        outline: none;
    }
    .xmap-search-label {
        color: #cbd5e1;
        font-size: 0.8rem;
        font-weight: 600;
    }
    .xmap-search {
        width: 100%;
        min-height: 42px;
        box-sizing: border-box;
        padding: 8px 12px;
        border: 1px solid #475569;
        border-radius: 6px;
        background: #020617;
        color: #f8fafc;
        font: inherit;
        font-size: 0.95rem;
    }
    .xmap-search:focus {
        border-color: #60a5fa;
        outline: 2px solid rgba(96, 165, 250, 0.25);
    }
    .xmap-actions {
        display: flex;
        gap: 8px;
    }
    .xmap-actions button {
        min-height: 34px;
        padding: 6px 12px;
        border: 1px solid #475569;
        border-radius: 5px;
        background: #1e293b;
        color: #cbd5e1;
        font-size: 0.8rem;
        cursor: pointer;
    }
    .xmap-actions button:hover:not(:disabled),
    .xmap-actions button:focus-visible {
        border-color: #60a5fa;
        color: #fff;
        outline: none;
    }
    .xmap-actions button:disabled {
        opacity: 0.45;
        cursor: default;
    }
    .xmap-results {
        flex: 1;
        min-height: 0;
        overflow-y: auto;
        border: 1px solid #1e293b;
        border-radius: 6px;
        background: #0b1220;
    }
    .xmap-group + .xmap-group {
        border-top: 1px solid #1e293b;
    }
    .xmap-group h3 {
        position: sticky;
        top: 0;
        z-index: 1;
        margin: 0;
        padding: 8px 12px;
        background: #172033;
        color: #93c5fd;
        font-size: 0.75rem;
        font-weight: 700;
        letter-spacing: 0.02em;
    }
    .xmap-group-items {
        padding: 4px;
    }
    .xmap-option {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 12px;
        width: 100%;
        min-height: 38px;
        padding: 7px 10px;
        border: 1px solid transparent;
        border-radius: 4px;
        background: transparent;
        color: #dbeafe;
        font-size: 0.88rem;
        text-align: left;
        cursor: pointer;
    }
    .xmap-option:hover,
    .xmap-option:focus-visible {
        border-color: #334155;
        background: #1e293b;
        color: #fff;
        outline: none;
    }
    .xmap-selected {
        background: rgba(37, 99, 235, 0.18);
        color: #bfdbfe;
    }
    .xmap-current {
        flex-shrink: 0;
        color: #60a5fa;
        font-size: 0.68rem;
        font-weight: 700;
        text-transform: uppercase;
    }
    .xmap-empty {
        padding: 30px 16px;
        color: #94a3b8;
        text-align: center;
    }
</style>
