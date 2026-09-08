<script>
    import { dispatchCpp } from "./ipc.js";
    import { groupEffects } from "./masterAudioUi.js";

    let {
        bus,
        plugins = [],
        onClose = () => {},
    } = $props();

    let addingPosition = $state("");
    let query = $state("");
    let effectGroups = $derived(groupEffects(plugins, query));
    let sections = $derived([
        {
            key: "preFader",
            title: "Pre-fader inserts",
            description: "Shared processing before the bus level control",
            items: bus.audio?.preFaderInserts || [],
        },
        {
            key: "postFader",
            title: "Post-fader inserts",
            description: "Processing after the bus level control, before Master",
            items: bus.audio?.postFaderInserts || [],
        },
    ]);

    const addEffect = (uid, position) => {
        dispatchCpp("addGroupBusInsert", bus.id, uid, position);
        addingPosition = "";
        query = "";
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={(event) => event.target === event.currentTarget && onClose()}>
    <dialog class="panel" aria-label={`Audio effects for ${bus.name}`} open>
        <header>
            <div>
                <h2>Bus Audio</h2>
                <p>{bus.name} · feeds Master</p>
            </div>
            <div class="header-status">
                <span>{Number(bus.audio?.latencyMs || 0).toFixed(1)} ms latency</span>
                <button class="close" onclick={onClose}>Close</button>
            </div>
        </header>

        <div class="content">
            {#each sections as section (section.key)}
                <section aria-label={section.title}>
                    <div class="section-heading">
                        <div>
                            <h3>{section.title}</h3>
                            <p>{section.description}</p>
                        </div>
                        <button
                            class="primary"
                            onclick={() => {
                                addingPosition = addingPosition === section.key ? "" : section.key;
                                query = "";
                            }}
                        >{addingPosition === section.key ? "Cancel" : "Add effect"}</button>
                    </div>

                    {#if addingPosition === section.key}
                        <div class="picker">
                            <input
                                class="search"
                                type="search"
                                bind:value={query}
                                placeholder="Search effects by name, maker, or category"
                                aria-label={`Search effects for ${section.title}`}
                            />
                            <div class="effect-list">
                                {#if effectGroups.length === 0}
                                    <div class="empty-picker">No compatible stereo effects found.</div>
                                {:else}
                                    {#each effectGroups as group (group.manufacturer)}
                                        <div class="maker">{group.manufacturer}</div>
                                        {#each group.plugins as plugin (plugin.uid)}
                                            <button class="effect-choice" onclick={() => addEffect(plugin.uid, section.key)}>
                                                <span>{plugin.name}</span>
                                                <small>{plugin.category || "Audio effect"}</small>
                                            </button>
                                        {/each}
                                    {/each}
                                {/if}
                            </div>
                        </div>
                    {/if}

                    <div class="insert-list">
                        {#if section.items.length === 0}
                            <div class="empty-inserts">No {section.key === "preFader" ? "pre-fader" : "post-fader"} effects.</div>
                        {:else}
                            {#each section.items as insert, index (insert.id)}
                                <article class="insert-row" class:bypassed={insert.bypassed}>
                                    <div class="order">{index + 1}</div>
                                    <div class="insert-name">
                                        <strong>{insert.name || "Unavailable plug-in"}</strong>
                                        <span>{insert.manufacturer || insert.format || "Audio effect"}</span>
                                        {#if insert.status !== "loaded"}
                                            <span class:error={insert.status === "failed" || insert.status === "missing"}>
                                                {insert.status}{insert.error ? ` — ${insert.error}` : ""}
                                            </span>
                                        {/if}
                                    </div>
                                    <div class="row-actions">
                                        <button onclick={() => dispatchCpp("moveGroupBusInsert", bus.id, insert.id, section.key, index - 1)} disabled={index === 0}>Move up</button>
                                        <button onclick={() => dispatchCpp("moveGroupBusInsert", bus.id, insert.id, section.key, index + 1)} disabled={index === section.items.length - 1}>Move down</button>
                                        <button
                                            onclick={() => dispatchCpp(
                                                "moveGroupBusInsert",
                                                bus.id,
                                                insert.id,
                                                section.key === "preFader" ? "postFader" : "preFader",
                                                section.key === "preFader"
                                                    ? (bus.audio?.postFaderInserts || []).length
                                                    : (bus.audio?.preFaderInserts || []).length,
                                            )}
                                        >Move {section.key === "preFader" ? "after" : "before"} fader</button>
                                        <button onclick={() => dispatchCpp("setGroupBusInsertBypassed", bus.id, insert.id, !insert.bypassed)}>{insert.bypassed ? "Enable" : "Bypass"}</button>
                                        <button
                                            onclick={() => dispatchCpp("toggleGroupBusInsertEditor", bus.id, insert.id)}
                                            disabled={insert.status !== "loaded"}
                                        >{insert.editorOpen ? "Hide" : "Edit"} plug-in</button>
                                        <button class="danger" onclick={() => dispatchCpp("removeGroupBusInsert", bus.id, insert.id)}>Remove</button>
                                    </div>
                                </article>
                            {/each}
                        {/if}
                    </div>
                </section>
            {/each}
        </div>
    </dialog>
</div>

<style>
    .backdrop { position: absolute; inset: 0; z-index: 56; display: flex; justify-content: flex-end; background: rgba(2, 6, 23, .62); backdrop-filter: blur(2px); }
    .panel { position: relative; margin: 0 0 0 auto; padding: 0; width: min(900px, calc(100% - 48px)); height: 100%; display: flex; flex-direction: column; background: #0b1220; color: #e2e8f0; border: 0; border-left: 1px solid #334155; box-shadow: -16px 0 40px rgba(0,0,0,.42); }
    header, .section-heading, .row-actions, .header-status { display: flex; align-items: center; }
    header { justify-content: space-between; gap: 24px; padding: 22px 26px; border-bottom: 1px solid #273449; background: #111c2e; }
    h2, h3, p { margin: 0; }
    h2 { font-size: 1.45rem; color: #f8fafc; }
    h3 { font-size: 1.05rem; color: #f1f5f9; }
    p, .header-status, .insert-name span { color: #94a3b8; }
    p { margin-top: 4px; font-size: .86rem; }
    .header-status { gap: 16px; font-size: .82rem; white-space: nowrap; }
    button, input { font: inherit; }
    button { min-height: 36px; padding: 7px 12px; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; cursor: pointer; }
    button:hover:not(:disabled) { border-color: #7dd3fc; background: #1e3048; }
    button:disabled { opacity: .38; cursor: default; }
    .close { min-width: 72px; }
    .content { padding: 24px 26px 32px; overflow-y: auto; }
    section { margin-bottom: 20px; padding: 20px; border: 1px solid #26364d; border-radius: 10px; background: #0f1929; }
    .section-heading { justify-content: space-between; gap: 20px; }
    .primary { min-width: 112px; border-color: #38bdf8; background: #075985; }
    .picker { margin-top: 18px; padding: 16px; border: 1px solid #36506f; border-radius: 8px; background: #08111f; }
    .search { width: 100%; height: 44px; box-sizing: border-box; padding: 10px 13px; border: 1px solid #475569; border-radius: 6px; background: #111c2e; color: #f8fafc; }
    .search:focus { outline: 2px solid #38bdf8; outline-offset: 1px; }
    .effect-list { max-height: 260px; margin-top: 12px; overflow-y: auto; }
    .maker { position: sticky; top: 0; padding: 9px 8px 6px; background: #08111f; color: #7dd3fc; font-size: .72rem; font-weight: 700; text-transform: uppercase; letter-spacing: .08em; }
    .effect-choice { width: 100%; min-height: 48px; margin-bottom: 5px; display: flex; justify-content: space-between; align-items: center; text-align: left; }
    .effect-choice small { color: #94a3b8; }
    .insert-list { margin-top: 18px; display: flex; flex-direction: column; gap: 10px; }
    .insert-row { display: grid; grid-template-columns: 38px minmax(160px, 1fr) auto; gap: 12px; align-items: center; min-height: 66px; padding: 10px 12px; border: 1px solid #334155; border-radius: 8px; background: #111c2e; }
    .insert-row.bypassed { opacity: .62; }
    .order { display: grid; place-items: center; width: 32px; height: 32px; border-radius: 50%; background: #24344c; color: #bae6fd; font-weight: 700; }
    .insert-name { min-width: 0; display: flex; flex-direction: column; gap: 3px; }
    .insert-name strong, .insert-name span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .insert-name strong { color: #f1f5f9; font-size: .94rem; }
    .insert-name span { font-size: .75rem; }
    .insert-name .error { color: #fca5a5; }
    .row-actions { justify-content: flex-end; gap: 7px; flex-wrap: wrap; }
    .row-actions button { min-height: 34px; font-size: .76rem; }
    .danger { color: #fecaca; border-color: #7f1d1d; }
    .empty-inserts, .empty-picker { padding: 22px; border: 1px dashed #334155; border-radius: 8px; color: #94a3b8; text-align: center; line-height: 1.5; }
    @media (max-width: 780px) { .panel { width: 100%; } .insert-row { grid-template-columns: 38px 1fr; } .row-actions { grid-column: 1 / -1; justify-content: flex-start; } }
</style>
