<script>
    import { filterLayerCatalog } from "./layerCatalog.js";

    let {
        chair,
        patches = [],
        onChoose = () => {},
        onClose = () => {},
    } = $props();
    let query = $state("");
    let groups = $derived(filterLayerCatalog(patches, query));

    const closeFromBackdrop = (event) => {
        if (event.target === event.currentTarget) onClose();
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="picker-backdrop" onclick={closeFromBackdrop}>
    <dialog class="picker" aria-label={`Add layer to ${chair?.name || "chair"}`} open>
        <header>
            <div>
                <h2>Add Layer</h2>
                <p>Choose a library patch for <strong>{chair?.name}</strong>. Each assignment creates an independent instrument instance.</p>
            </div>
            <button onclick={onClose}>Cancel</button>
        </header>
        <div class="content">
            <input
                class="search"
                type="search"
                bind:value={query}
                placeholder="Search by patch, library, family, or character"
                aria-label="Search library patches"
            />
            <div class="summary">{groups.reduce((count, group) => count + group.patches.length, 0)} matching patches</div>
            {#if patches.length === 0}
                <div class="empty">
                    <strong>No library patches are available.</strong>
                    <span>Add and configure patches in the Library Manager first.</span>
                </div>
            {:else if groups.length === 0}
                <div class="empty"><strong>No matching patches.</strong><span>Try a shorter or more general search.</span></div>
            {:else}
                <div class="libraries">
                    {#each groups as group (group.id)}
                        <section>
                            <div class="library-heading">
                                <h3>{group.name}</h3>
                                <span>{group.patches.length}</span>
                            </div>
                            <div class="patches">
                                {#each group.patches as patch (patch.id)}
                                    <button class="patch" onclick={() => onChoose(patch.id)}>
                                        <span class="patch-name">{patch.name}</span>
                                        <span class="patch-details">
                                            {[patch.character, patch.family].filter(Boolean).join(" · ") || "Unclassified patch"}
                                        </span>
                                        <span class="add-label">Add layer</span>
                                    </button>
                                {/each}
                            </div>
                        </section>
                    {/each}
                </div>
            {/if}
        </div>
    </dialog>
</div>

<style>
    .picker-backdrop { position:absolute; inset:0; z-index:60; display:flex; align-items:center; justify-content:center; padding:40px; background:rgba(2,6,23,.82); }
    .picker { width:min(860px, 100%); height:min(720px, calc(100% - 24px)); margin:0; padding:0; display:flex; flex-direction:column; overflow:hidden; border:1px solid #3b4b61; border-radius:12px; background:#0b1220; color:#e2e8f0; box-shadow:0 24px 70px rgba(0,0,0,.58); }
    header,.library-heading,.patch { display:flex; align-items:center; }
    header { justify-content:space-between; gap:24px; padding:22px 24px; border-bottom:1px solid #273449; background:#111c2e; }
    h2,h3,p { margin:0; } h2 { font-size:1.35rem; color:#f8fafc; } h3 { font-size:.95rem; color:#e2e8f0; }
    p { margin-top:4px; color:#94a3b8; font-size:.86rem; } p strong { color:#cbd5e1; }
    button,input { box-sizing:border-box; font:inherit; }
    button { min-height:38px; padding:8px 13px; border:1px solid #475569; border-radius:6px; background:#172337; color:#e2e8f0; cursor:pointer; }
    button:hover { border-color:#7dd3fc; background:#1e3048; }
    .content { flex:1; min-height:0; padding:20px 24px 28px; overflow-y:auto; }
    .search { width:100%; height:48px; padding:10px 13px; border:1px solid #475569; border-radius:7px; background:#09121f; color:#f8fafc; font-size:.95rem; }
    .search:focus { outline:2px solid rgba(56,189,248,.4); border-color:#38bdf8; }
    .summary { margin:8px 2px 18px; color:#64748b; font-size:.76rem; }
    .libraries { display:flex; flex-direction:column; gap:18px; }
    section { overflow:hidden; border:1px solid #26364d; border-radius:9px; background:#0f1929; }
    .library-heading { justify-content:space-between; padding:11px 14px; border-bottom:1px solid #26364d; background:#111c2e; }
    .library-heading span { min-width:24px; padding:2px 7px; border-radius:12px; background:#1e293b; color:#94a3b8; text-align:center; font-size:.72rem; }
    .patches { display:grid; grid-template-columns:repeat(auto-fill, minmax(260px, 1fr)); gap:1px; background:#26364d; }
    .patch { position:relative; min-height:74px; flex-direction:column; align-items:flex-start; gap:3px; padding:13px 92px 13px 14px; border:0; border-radius:0; background:#0f1929; text-align:left; }
    .patch-name { max-width:100%; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; color:#f1f5f9; font-weight:650; }
    .patch-details { color:#94a3b8; font-size:.76rem; }
    .add-label { position:absolute; right:14px; color:#7dd3fc; font-size:.76rem; font-weight:650; }
    .empty { min-height:220px; display:flex; flex-direction:column; align-items:center; justify-content:center; gap:7px; color:#cbd5e1; text-align:center; }
    .empty span { color:#94a3b8; }
    @media (max-width:700px) { .picker-backdrop { padding:12px; } header { align-items:flex-start; } .patches { grid-template-columns:1fr; } }
</style>
