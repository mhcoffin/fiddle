<script>
    import ExpressionMapPicker from "./ExpressionMapPicker.svelte";

    let {
        strip,
        pluginName = "",
        plugins = [],
        maps = [],
        luaCatalog = [],
        groupBuses = [],
        inspectorOpen = false,
        onClose = () => {},
        onSetPlugin = () => {},
        onShowInstrument = () => {},
        onSetProgram = () => {},
        onSelectMap = () => {},
        onClearMap = () => {},
        onLoadMapFile = () => {},
        onAddLua = () => {},
        onRemoveLua = () => {},
        onInspectMidi = () => {},
        onRefreshLibrary = () => {},
        onRenameLibrary = () => {},
        onDuplicate = () => {},
        onDelete = () => {},
        onSetOutput = () => {},
    } = $props();

    const closeFromBackdrop = (event) => {
        if (event.target === event.currentTarget) onClose();
    };
</script>

<!-- svelte-ignore a11y_click_events_have_key_events -->
<!-- svelte-ignore a11y_no_static_element_interactions -->
<div class="backdrop" onclick={closeFromBackdrop}>
    <dialog class="panel" aria-label={`Layer details for ${strip.layerName || strip.library || "strip"}`} open>
        <header>
            <div>
                <h2>Layer Details</h2>
                <p>{strip.layerName || strip.library || "Instrument strip"}</p>
            </div>
            <button class="close" onclick={onClose}>Close</button>
        </header>

        <div class="content">
            <section>
                <div class="section-heading">
                    <div>
                        <h3>Library source</h3>
                        <p>{strip.chairId ? "This layer inherits its initial setup from a library patch." : "This is a free-standing mixer strip."}</p>
                    </div>
                    {#if strip.chairId}
                        <button
                            class:attention={strip.sourcePatchOutOfDate}
                            disabled={strip.missingPatchReference || !strip.sourcePatchOutOfDate}
                            onclick={onRefreshLibrary}
                        >Refresh Library</button>
                    {/if}
                </div>
                <div class="fields">
                    <label>
                        <span>Patch</span>
                        <input value={strip.layerName || "—"} readonly />
                    </label>
                    <label>
                        <span>Library</span>
                        <input
                            value={strip.library || ""}
                            readonly={Boolean(strip.chairId)}
                            onchange={(event) => onRenameLibrary(event.currentTarget.value)}
                        />
                    </label>
                </div>
                {#if strip.missingPatchReference}
                    <div class="warning">The source library patch is missing.</div>
                {:else if strip.sourcePatchOutOfDate}
                    <div class="notice">The library patch has changed since this layer was created.</div>
                {/if}
            </section>

            <section>
                <div class="section-heading">
                    <div>
                        <h3>Audio output</h3>
                        <p>Send this strip directly to Master or through one group bus.</p>
                    </div>
                </div>
                <select value={strip.directOutputBusId || ""} onchange={(event) => onSetOutput(event.currentTarget.value)}>
                    <option value="">Master</option>
                    {#each groupBuses as bus}
                        <option value={bus.id}>{bus.name}</option>
                    {/each}
                </select>
            </section>

            <section>
                <div class="section-heading">
                    <div>
                        <h3>VST instrument</h3>
                        <p>Assign or replace the player used by this layer.</p>
                    </div>
                    {#if strip.hasPlugin}
                        <button onclick={onShowInstrument}>Edit instrument</button>
                    {/if}
                </div>
                <select value={strip.pluginUid || 0} onchange={(event) => onSetPlugin(Number(event.currentTarget.value))}>
                    <option value="0">No instrument assigned</option>
                    {#each plugins.filter((plugin) => plugin.valid !== false) as plugin}
                        <option value={plugin.uid}>{plugin.name}</option>
                    {/each}
                </select>
                <div class="current-value">Current: {pluginName || "No instrument assigned"}</div>

                {#if strip.hasPlugin && (strip.numPrograms ?? 0) > 1 && (strip.programNames ?? []).some((name) => name && !/^Program\s*\d*$/.test(name))}
                    <label class="program">
                        <span>Program</span>
                        <select value={strip.programIndex ?? 0} onchange={(event) => onSetProgram(Number(event.currentTarget.value))}>
                            {#each (strip.programNames ?? []) as name, index}
                                <option value={index}>{name || `Program ${index}`}</option>
                            {/each}
                        </select>
                    </label>
                {/if}
            </section>

            <section>
                <div class="section-heading">
                    <div>
                        <h3>Expression map</h3>
                        <p>Choose the articulation map supplied to this layer.</p>
                    </div>
                </div>
                <ExpressionMapPicker
                    {maps}
                    selectedId={strip.expressionMapEntityID || ""}
                    selectedName={strip.expressionMapName || ""}
                    onselect={onSelectMap}
                    onclear={onClearMap}
                    onloadfile={onLoadMapFile}
                />
            </section>

            <section>
                <div class="section-heading">
                    <div>
                        <h3>Lua processors</h3>
                        <p>Optional MIDI processors run before the expression map.</p>
                    </div>
                </div>
                {#if strip.luaPlugins?.length}
                    <div class="lua-list">
                        {#each strip.luaPlugins as plugin}
                            <div class="lua-row" class:error={!plugin.loaded}>
                                <div>
                                    <strong>{plugin.name || "Unknown processor"}</strong>
                                    <span>{plugin.filePath || ""}</span>
                                </div>
                                <button onclick={() => onRemoveLua(plugin.index)}>Remove</button>
                            </div>
                        {/each}
                    </div>
                {:else}
                    <div class="empty">No Lua processors assigned.</div>
                {/if}
                {#if luaCatalog.length}
                    <select value="" onchange={(event) => {
                        if (event.currentTarget.value) onAddLua(event.currentTarget.value);
                        event.currentTarget.value = "";
                    }}>
                        <option value="">Add Lua processor…</option>
                        {#each luaCatalog as plugin}
                            <option value={plugin.filePath}>{plugin.name}</option>
                        {/each}
                    </select>
                {/if}
            </section>

            <section>
                <div class="section-heading">
                    <div>
                        <h3>Incoming MIDI</h3>
                        <p>Inspect raw notes, controllers, and expression-map resolution.</p>
                    </div>
                    <button class:attention={inspectorOpen} onclick={onInspectMidi}>{inspectorOpen ? "Hide inspector" : "Inspect MIDI"}</button>
                </div>
            </section>

            <section class="danger-zone">
                <div>
                    <h3>Layer operations</h3>
                    <p>Structural operations are kept away from the mixing controls.</p>
                </div>
                <div class="danger-actions">
                    {#if !strip.chairId}
                        <button onclick={onDuplicate}>Duplicate strip</button>
                    {/if}
                    <button class="danger" onclick={onDelete}>Delete {strip.chairId ? "layer" : "strip"}</button>
                </div>
            </section>
        </div>
    </dialog>
</div>

<style>
    .backdrop { position: absolute; inset: 0; z-index: 52; display: flex; justify-content: flex-end; background: rgba(2, 6, 23, 0.62); backdrop-filter: blur(2px); }
    .panel { position: relative; margin: 0 0 0 auto; padding: 0; width: min(760px, calc(100% - 48px)); height: 100%; display: flex; flex-direction: column; background: #0b1220; border: 0; border-left: 1px solid #334155; box-shadow: -16px 0 40px rgba(0, 0, 0, 0.42); }
    header, .section-heading, .danger-zone, .danger-actions { display: flex; align-items: center; }
    header { justify-content: space-between; gap: 24px; padding: 22px 26px; border-bottom: 1px solid #273449; background: #111c2e; }
    h2, h3, p { margin: 0; }
    h2 { color: #f8fafc; font-size: 1.45rem; }
    h3 { color: #f1f5f9; font-size: 1.02rem; }
    p { margin-top: 4px; color: #94a3b8; font-size: 0.84rem; line-height: 1.4; }
    button, input, select { min-height: 38px; box-sizing: border-box; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; font: inherit; }
    button { padding: 7px 12px; cursor: pointer; font-weight: 600; }
    button:hover:not(:disabled) { border-color: #7dd3fc; background: #1e3048; }
    button:disabled { opacity: 0.38; cursor: default; }
    input, select { width: 100%; padding: 8px 11px; }
    input[readonly] { color: #94a3b8; }
    .close { min-width: 72px; }
    .content { padding: 24px 26px 32px; overflow-y: auto; }
    section { margin-bottom: 18px; padding: 20px; border: 1px solid #26364d; border-radius: 10px; background: #0f1929; }
    .section-heading { justify-content: space-between; gap: 20px; margin-bottom: 16px; }
    .fields { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; }
    label { display: flex; flex-direction: column; gap: 6px; }
    label > span { color: #94a3b8; font-size: 0.72rem; font-weight: 700; text-transform: uppercase; letter-spacing: 0.06em; }
    .program { margin-top: 14px; }
    .current-value { margin-top: 8px; color: #94a3b8; font-size: 0.78rem; }
    .notice, .warning, .empty { margin-top: 12px; padding: 10px 12px; border-radius: 6px; font-size: 0.8rem; }
    .notice { border: 1px solid #854d0e; background: rgba(133, 77, 14, 0.2); color: #fde68a; }
    .warning { border: 1px solid #991b1b; background: rgba(127, 29, 29, 0.22); color: #fecaca; }
    .empty { border: 1px dashed #334155; color: #94a3b8; text-align: center; }
    .attention { border-color: #f59e0b; color: #fbbf24; }
    .lua-list { display: flex; flex-direction: column; gap: 8px; margin-bottom: 12px; }
    .lua-row { display: flex; align-items: center; justify-content: space-between; gap: 16px; padding: 10px 12px; border: 1px solid #334155; border-radius: 7px; background: #111c2e; }
    .lua-row > div { min-width: 0; display: flex; flex-direction: column; gap: 3px; }
    .lua-row strong, .lua-row span { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
    .lua-row strong { color: #e2e8f0; font-size: 0.86rem; }
    .lua-row span { color: #64748b; font-size: 0.72rem; }
    .lua-row.error { border-color: #991b1b; }
    .danger-zone { justify-content: space-between; gap: 20px; }
    .danger-actions { justify-content: flex-end; gap: 10px; flex-wrap: wrap; }
    .danger { border-color: #7f1d1d; color: #fecaca; }
    @media (max-width: 680px) {
        .panel { width: 100%; }
        .fields { grid-template-columns: 1fr; }
        .danger-zone { align-items: flex-start; flex-direction: column; }
    }
</style>
