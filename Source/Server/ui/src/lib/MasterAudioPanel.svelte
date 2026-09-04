<script>
    import { dispatchCpp } from "./ipc.js";
    import { clampMasterGain, groupEffects } from "./masterAudioUi.js";

    let {
        master = { gainDb: 0, peakDb: -120, latencyMs: 0, inserts: [] },
        plugins = [],
        onClose = () => {},
    } = $props();

    let adding = $state(false);
    let query = $state("");
    let gainEntry = $state(false);
    let effectGroups = $derived(groupEffects(plugins, query));
    let peakPercent = $derived(
        Math.max(0, Math.min(100, ((Number(master.peakDb) + 60) / 60) * 100)),
    );

    const setGain = (value) => {
        const gain = clampMasterGain(value);
        dispatchCpp("setMasterGain", gain);
    };

    const addEffect = (uid) => {
        dispatchCpp("addMasterInsert", uid);
        adding = false;
        query = "";
    };

    const closeFromBackdrop = (event) => {
        if (event.target === event.currentTarget) onClose();
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={closeFromBackdrop}>
    <dialog class="panel" aria-label="Master Audio" open>
        <header>
            <div>
                <h2>Master Audio</h2>
                <p>Final stereo processing and output level</p>
            </div>
            <div class="header-status">
                <span>{Number(master.latencyMs || 0).toFixed(1)} ms latency</span>
                <button class="close" onclick={onClose} aria-label="Close Master Audio">Close</button>
            </div>
        </header>

        <div class="content">
            <section class="gain-section" aria-label="Master output level">
                <div class="section-heading">
                    <div>
                        <h3>Output level</h3>
                        <p>Applied after all inserts</p>
                    </div>
                    {#if gainEntry}
                        <input
                            class="gain-entry"
                            type="number"
                            min="-120"
                            max="6"
                            step="0.1"
                            value={Number(master.gainDb).toFixed(1)}
                            onchange={(event) => {
                                setGain(event.currentTarget.value);
                                gainEntry = false;
                            }}
                            onblur={() => { gainEntry = false; }}
                            onkeydown={(event) => {
                                if (event.key === "Enter") event.currentTarget.blur();
                                if (event.key === "Escape") gainEntry = false;
                            }}
                            aria-label="Master gain in decibels"
                        />
                    {:else}
                        <button class="gain-readout" onclick={() => { gainEntry = true; }}>
                            {Number(master.gainDb).toFixed(1)} dB
                        </button>
                    {/if}
                </div>
                <input
                    class="gain-slider"
                    type="range"
                    min="-60"
                    max="6"
                    step="0.1"
                    value={master.gainDb}
                    oninput={(event) => setGain(event.currentTarget.value)}
                    aria-label="Master output gain"
                />
                <div class="meter" title={`${Number(master.peakDb).toFixed(1)} dB peak`}>
                    <div class="meter-fill" style={`width: ${peakPercent}%`}></div>
                </div>
                <div class="scale"><span>−60</span><span>−30</span><span>−12</span><span>0</span><span>+6 dB</span></div>
            </section>

            <section class="insert-section" aria-label="Master inserts">
                <div class="section-heading">
                    <div>
                        <h3>Insert effects</h3>
                        <p>Audio flows from top to bottom</p>
                    </div>
                    <button class="primary" onclick={() => { adding = !adding; }}>
                        {adding ? "Cancel" : "Add effect"}
                    </button>
                </div>

                {#if adding}
                    <div class="picker">
                        <input
                            class="search"
                            type="search"
                            bind:value={query}
                            placeholder="Search effects by name, maker, or category"
                            aria-label="Search audio effects"
                        />
                        <div class="effect-list">
                            {#if effectGroups.length === 0}
                                <div class="empty-picker">No compatible stereo effects found.</div>
                            {:else}
                                {#each effectGroups as group (group.manufacturer)}
                                    <div class="maker">{group.manufacturer}</div>
                                    {#each group.plugins as plugin (plugin.uid)}
                                        <button class="effect-choice" onclick={() => addEffect(plugin.uid)}>
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
                    {#if !master.inserts?.length}
                        <div class="empty-inserts">
                            No master effects yet. Add a reverb, compressor, limiter, or other stereo effect.
                        </div>
                    {:else}
                        {#each master.inserts as insert, index (insert.id)}
                            <article class:bypassed={insert.bypassed} class="insert-row">
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
                                    <button
                                        onclick={() => dispatchCpp("moveMasterInsert", insert.id, index - 1)}
                                        disabled={index === 0}
                                        aria-label={`Move ${insert.name} earlier`}
                                    >Move up</button>
                                    <button
                                        onclick={() => dispatchCpp("moveMasterInsert", insert.id, index + 1)}
                                        disabled={index === master.inserts.length - 1}
                                        aria-label={`Move ${insert.name} later`}
                                    >Move down</button>
                                    <button onclick={() => dispatchCpp("setMasterInsertBypassed", insert.id, !insert.bypassed)}>
                                        {insert.bypassed ? "Enable" : "Bypass"}
                                    </button>
                                    <button
                                        onclick={() => dispatchCpp("showMasterInsertEditor", insert.id)}
                                        disabled={insert.status !== "loaded"}
                                    >Edit plug-in</button>
                                    <button class="danger" onclick={() => dispatchCpp("removeMasterInsert", insert.id)}>Remove</button>
                                </div>
                            </article>
                        {/each}
                    {/if}
                </div>
            </section>
        </div>
    </dialog>
</div>

<style>
    .backdrop {
        position: absolute;
        inset: 0;
        z-index: 50;
        display: flex;
        justify-content: flex-end;
        background: rgba(2, 6, 23, 0.62);
        backdrop-filter: blur(2px);
    }
    .panel {
        position: relative;
        margin: 0 0 0 auto;
        padding: 0;
        width: min(820px, calc(100% - 48px));
        height: 100%;
        display: flex;
        flex-direction: column;
        background: #0b1220;
        border: 0;
        border-left: 1px solid #334155;
        box-shadow: -16px 0 40px rgba(0, 0, 0, 0.42);
    }
    header, .section-heading, .row-actions, .scale, .header-status {
        display: flex;
        align-items: center;
    }
    header {
        justify-content: space-between;
        gap: 24px;
        padding: 22px 26px;
        border-bottom: 1px solid #273449;
        background: #111c2e;
    }
    h2, h3, p { margin: 0; }
    h2 { font-size: 1.45rem; color: #f8fafc; }
    h3 { font-size: 1.05rem; color: #f1f5f9; }
    p, .header-status, .insert-name span { color: #94a3b8; }
    p { margin-top: 4px; font-size: 0.86rem; }
    .header-status { gap: 16px; font-size: 0.82rem; white-space: nowrap; }
    button, input { font: inherit; }
    button {
        min-height: 36px;
        padding: 7px 12px;
        border: 1px solid #475569;
        border-radius: 6px;
        background: #172337;
        color: #e2e8f0;
        cursor: pointer;
    }
    button:hover:not(:disabled) { border-color: #7dd3fc; background: #1e3048; }
    button:disabled { opacity: 0.38; cursor: default; }
    .close { min-width: 72px; }
    .content { padding: 24px 26px 32px; overflow-y: auto; }
    .gain-section, .insert-section {
        border: 1px solid #26364d;
        border-radius: 10px;
        background: #0f1929;
        padding: 20px;
    }
    .insert-section { margin-top: 20px; }
    .section-heading { justify-content: space-between; gap: 20px; }
    .gain-readout, .gain-entry {
        width: 104px;
        height: 42px;
        box-sizing: border-box;
        text-align: center;
        font-size: 1rem;
        font-variant-numeric: tabular-nums;
    }
    .gain-entry { border: 1px solid #38bdf8; border-radius: 6px; background: #07111f; color: #f8fafc; }
    .gain-slider { width: 100%; margin: 22px 0 10px; accent-color: #38bdf8; }
    .meter { height: 12px; overflow: hidden; border-radius: 5px; background: #020617; border: 1px solid #26364d; }
    .meter-fill { height: 100%; background: linear-gradient(90deg, #22c55e 0%, #eab308 80%, #ef4444 100%); transition: width 80ms linear; }
    .scale { justify-content: space-between; margin-top: 6px; color: #64748b; font-size: 0.7rem; }
    .primary { min-width: 112px; background: #075985; border-color: #38bdf8; }
    .picker { margin-top: 18px; padding: 16px; border: 1px solid #36506f; border-radius: 8px; background: #08111f; }
    .search { width: 100%; height: 44px; box-sizing: border-box; padding: 10px 13px; border: 1px solid #475569; border-radius: 6px; background: #111c2e; color: #f8fafc; font-size: 0.95rem; }
    .search:focus { outline: 2px solid #38bdf8; outline-offset: 1px; }
    .effect-list { max-height: 300px; margin-top: 12px; overflow-y: auto; }
    .maker { position: sticky; top: 0; padding: 9px 8px 6px; background: #08111f; color: #7dd3fc; font-size: 0.72rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.08em; }
    .effect-choice { width: 100%; min-height: 48px; margin-bottom: 5px; display: flex; justify-content: space-between; align-items: center; text-align: left; }
    .effect-choice small { color: #94a3b8; }
    .insert-list { margin-top: 18px; display: flex; flex-direction: column; gap: 10px; }
    .insert-row { display: grid; grid-template-columns: 38px minmax(160px, 1fr) auto; gap: 12px; align-items: center; min-height: 66px; padding: 10px 12px; border: 1px solid #334155; border-radius: 8px; background: #111c2e; }
    .insert-row.bypassed { opacity: 0.62; }
    .order { display: grid; place-items: center; width: 32px; height: 32px; border-radius: 50%; background: #24344c; color: #bae6fd; font-weight: 700; }
    .insert-name { min-width: 0; display: flex; flex-direction: column; gap: 3px; }
    .insert-name strong, .insert-name span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .insert-name strong { color: #f1f5f9; font-size: 0.94rem; }
    .insert-name span { font-size: 0.75rem; }
    .insert-name .error { color: #fca5a5; }
    .row-actions { justify-content: flex-end; gap: 7px; flex-wrap: wrap; }
    .row-actions button { min-height: 34px; font-size: 0.76rem; }
    .danger { color: #fecaca; border-color: #7f1d1d; }
    .empty-inserts, .empty-picker { padding: 28px; border: 1px dashed #334155; border-radius: 8px; color: #94a3b8; text-align: center; line-height: 1.5; }
    .empty-picker { padding: 18px; }
    @media (max-width: 760px) {
        .panel { width: 100%; }
        .insert-row { grid-template-columns: 38px 1fr; }
        .row-actions { grid-column: 1 / -1; justify-content: flex-start; }
        header { align-items: flex-start; }
        .header-status { flex-direction: column; align-items: flex-end; gap: 6px; }
    }
</style>
