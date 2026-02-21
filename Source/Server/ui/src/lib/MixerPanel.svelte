<script>
    import { onMount } from "svelte";
    import { FAMILY_ORDER, canonicalFamily } from "./orchestralOrder.js";

    let strips = $state([]);
    let availableInputs = $state([]);
    let scannedPlugins = $state([]);
    let playbackDelay = $state(1000);
    let editingDelay = $state(false);

    /** @type {any} */
    const w = window;

    const getNative = (name) => {
        const win = /** @type {any} */ (window);
        return (
            (win.__JUCE__ &&
                win.__JUCE__.backend &&
                win.__JUCE__.backend[name]) ||
            win[name] ||
            (win.juce && win.juce[name]) ||
            (win.__juce__ && win.__juce__[name])
        );
    };

    const FAMILY_COLORS = {
        woodwinds: { accent: "#38bdf8", header: "#0e4d6e", text: "#bae6fd" },
        brass: { accent: "#f59e0b", header: "#713f12", text: "#fde68a" },
        percussion: { accent: "#a8896b", header: "#44342a", text: "#d4c4b0" },
        keys: { accent: "#c084fc", header: "#581c87", text: "#e9d5ff" },
        strings: { accent: "#34d399", header: "#14532d", text: "#a7f3d0" },
        choir: { accent: "#94a3b8", header: "#334155", text: "#e2e8f0" },
    };
    const defaultColors = FAMILY_COLORS.choir;

    w.setMixerState = (jsonStr) => {
        try {
            strips = JSON.parse(jsonStr);
        } catch (e) {
            console.error("[Mixer] parse error:", e);
        }
    };
    w.setAvailableInputs = (jsonStr) => {
        try {
            availableInputs = JSON.parse(jsonStr);
        } catch (e) {
            console.error("[Mixer] parse error:", e);
        }
    };
    w.setPlaybackDelay = (ms) => {
        playbackDelay = ms;
    };

    onMount(() => {
        const fn = getNative("getPlaybackDelay");
        if (fn) fn();
    });

    const updateDelay = (ms) => {
        const val = Math.max(0, Math.min(5000, parseInt(ms) || 0));
        playbackDelay = val;
        const fn = getNative("setPlaybackDelay");
        if (fn) fn(val);
    };

    const origSetPluginList = w.setPluginList;
    w.setPluginList = (jsonStr) => {
        if (origSetPluginList) origSetPluginList(jsonStr);
        try {
            scannedPlugins = JSON.parse(jsonStr);
        } catch (e) {
            /* ignore */
        }
    };

    onMount(() => {
        const fn = getNative("getAvailableInputs");
        if (fn) fn();
        const fnMixer = getNative("requestMixerState");
        if (fnMixer) fnMixer();
        const fnPlugins = getNative("requestPluginsState");
        if (fnPlugins) fnPlugins();
    });

    const addStrip = () => {
        const fn = getNative("addMixerStrip");
        if (fn) fn();
    };
    const removeStrip = (id) => {
        const fn = getNative("removeMixerStrip");
        if (fn) fn(id);
    };

    const setPlugin = (stripId, pluginUid) => {
        const fn = getNative("setStripPlugin");
        if (fn) fn(stripId, pluginUid);
    };
    const showEditor = (stripId) => {
        const fn = getNative("showStripEditor");
        if (fn) fn(stripId);
    };

    let editingId = $state(null);
    let editValue = $state("");
    const startEditing = (strip) => {
        editingId = strip.id;
        editValue = strip.name;
    };
    const commitEdit = (stripId) => {
        if (editValue.trim()) {
            const fn = getNative("setStripName");
            if (fn) fn(stripId, editValue.trim());
        }
        editingId = null;
    };
    const cancelEdit = () => {
        editingId = null;
    };
    const handleNameKeydown = (e, stripId) => {
        if (e.key === "Enter") {
            e.preventDefault();
            commitEdit(stripId);
        } else if (e.key === "Escape") cancelEdit();
    };

    /** @type {Record<string, boolean>} */
    let collapsedFamilies = $state({});
    const toggleFamily = (family) => {
        collapsedFamilies = {
            ...collapsedFamilies,
            [family]: !collapsedFamilies[family],
        };
    };

    let groupedStrips = $derived.by(() => {
        const groups = [];
        const familyMap = new Map();
        for (const strip of strips) {
            const fam = canonicalFamily(strip.family || "");
            if (!familyMap.has(fam)) {
                const group = {
                    family: fam,
                    displayName: strip.family || "Other",
                    strips: [],
                    colors: FAMILY_COLORS[fam] || defaultColors,
                };
                familyMap.set(fam, group);
                groups.push(group);
            }
            familyMap.get(fam).strips.push(strip);
        }
        groups.sort((a, b) => {
            const ia = FAMILY_ORDER.indexOf(a.family);
            const ib = FAMILY_ORDER.indexOf(b.family);
            return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
        });
        return groups;
    });
</script>

<div class="mixer-container">
    <div class="mixer-toolbar">
        <h2>Mixer</h2>
        <div class="toolbar-right">
            <div class="delay-control">
                <label class="delay-label">Delay</label>
                <input
                    class="delay-slider"
                    type="range"
                    min="0"
                    max="3000"
                    step="50"
                    value={playbackDelay}
                    oninput={(e) => updateDelay(e.target.value)}
                />
                {#if editingDelay}
                    <input
                        class="delay-value-input"
                        type="number"
                        min="0"
                        max="5000"
                        value={playbackDelay}
                        onchange={(e) => {
                            updateDelay(e.target.value);
                            editingDelay = false;
                        }}
                        onblur={() => {
                            editingDelay = false;
                        }}
                        onkeydown={(e) => {
                            if (e.key === "Escape") editingDelay = false;
                        }}
                    />
                {:else}
                    <span
                        class="delay-value"
                        ondblclick={() => {
                            editingDelay = true;
                        }}
                        title="Double-click to type">{playbackDelay}ms</span
                    >
                {/if}
            </div>
        </div>
    </div>

    {#if strips.length === 0}
        <div class="empty-state">
            <p>
                No channel strips. Add instruments in the <strong>Setup</strong>
                tab.
            </p>
        </div>
    {:else}
        <div class="console">
            {#each groupedStrips as group (group.family)}
                {@const collapsed = collapsedFamilies[group.family]}
                <div class="folder" class:folder-collapsed={collapsed}>
                    <!-- Folder tab (always visible, vertical when collapsed) -->
                    <button
                        class="folder-tab"
                        class:folder-tab-collapsed={collapsed}
                        style="background: {group.colors.header}; color: {group
                            .colors.text}; border-bottom: 3px solid {group
                            .colors.accent};"
                        onclick={() => toggleFamily(group.family)}
                        title={collapsed
                            ? `Expand ${group.displayName}`
                            : `Collapse ${group.displayName}`}
                    >
                        <svg
                            class="tab-chevron"
                            class:tab-chevron-collapsed={collapsed}
                            viewBox="0 0 20 20"
                            width="10"
                            height="10"
                            fill="currentColor"
                        >
                            <path d="M6 4l8 6-8 6z" />
                        </svg>
                        <span class="tab-label">{group.displayName}</span>
                        <span
                            class="tab-count"
                            style="background: {group.colors
                                .accent}25; color: {group.colors.accent};"
                            >{group.strips.length}</span
                        >
                    </button>

                    {#if !collapsed}
                        <div class="folder-strips">
                            {#each group.strips as strip (strip.id)}
                                <div
                                    class="channel-strip"
                                    style="border-top: 3px solid {group.colors
                                        .accent};"
                                >
                                    <!-- Strip name -->
                                    <div class="ch-name-area">
                                        {#if editingId === strip.id}
                                            <input
                                                class="ch-name-input"
                                                type="text"
                                                bind:value={editValue}
                                                onblur={() =>
                                                    commitEdit(strip.id)}
                                                onkeydown={(e) =>
                                                    handleNameKeydown(
                                                        e,
                                                        strip.id,
                                                    )}
                                            />
                                        {:else}
                                            <div
                                                class="ch-name"
                                                ondblclick={() =>
                                                    startEditing(strip)}
                                                title="Double-click to rename"
                                            >
                                                {strip.name}
                                            </div>
                                        {/if}
                                    </div>

                                    <!-- Spacer pushes plugin to bottom -->
                                    <div class="ch-spacer"></div>

                                    <!-- Plugin -->
                                    <div class="ch-plugin">
                                        <select
                                            class="ch-select"
                                            value={strip.pluginUid || 0}
                                            onchange={(e) => {
                                                const uid = Number(
                                                    e.target.value,
                                                );
                                                if (uid)
                                                    setPlugin(strip.id, uid);
                                            }}
                                        >
                                            <option value="0">—</option>
                                            {#each scannedPlugins as plugin}
                                                <option value={plugin.uid}
                                                    >{plugin.name}</option
                                                >
                                            {/each}
                                        </select>
                                        {#if strip.hasPlugin}
                                            <button
                                                class="ch-edit-btn"
                                                onclick={() =>
                                                    showEditor(strip.id)}
                                                title="Open editor">⚙</button
                                            >
                                        {/if}
                                    </div>

                                    <!-- Port/Channel label -->
                                    <div class="ch-input-label">
                                        {#if strip.inputPort >= 0}
                                            P{strip.inputPort +
                                                1}.{strip.inputChannel + 1}
                                        {:else}
                                            —
                                        {/if}
                                    </div>
                                </div>
                            {/each}
                        </div>
                    {/if}
                </div>
            {/each}
        </div>
    {/if}
</div>

<style>
    .mixer-container {
        display: flex;
        flex-direction: column;
        height: 100%;
        color: #e2e8f0;
        font-family:
            "Inter",
            -apple-system,
            sans-serif;
        overflow: hidden;
    }

    .mixer-toolbar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 10px 16px;
        flex-shrink: 0;
        border-bottom: 1px solid #1e293b;
    }
    .mixer-toolbar h2 {
        margin: 0;
        font-size: 1rem;
        font-weight: 600;
        color: #f1f5f9;
    }
    .toolbar-right {
        display: flex;
        align-items: center;
        gap: 12px;
    }

    .delay-control {
        display: flex;
        align-items: center;
        gap: 4px;
    }
    .delay-label {
        font-size: 0.7rem;
        color: #94a3b8;
        font-weight: 500;
    }
    .delay-slider {
        width: 80px;
        height: 4px;
        accent-color: #3b82f6;
        cursor: pointer;
    }
    .delay-value {
        font-size: 0.7rem;
        color: #94a3b8;
        min-width: 40px;
        text-align: right;
        cursor: default;
        user-select: none;
    }
    .delay-value-input {
        width: 50px;
        padding: 2px 4px;
        border: 1px solid #3b82f6;
        border-radius: 3px;
        background: #0f172a;
        color: #e2e8f0;
        font-size: 0.7rem;
        text-align: right;
        -moz-appearance: textfield;
    }
    .delay-value-input:focus {
        outline: none;
    }
    .delay-value-input::-webkit-inner-spin-button,
    .delay-value-input::-webkit-outer-spin-button {
        -webkit-appearance: none;
    }

    .empty-state {
        display: flex;
        align-items: center;
        justify-content: center;
        flex: 1;
        color: #64748b;
        font-size: 0.85rem;
    }

    /* ── Console: horizontal strip layout ────────── */
    .console {
        display: flex;
        flex-direction: row;
        flex: 1;
        overflow-x: auto;
        overflow-y: hidden;
        align-items: stretch;
    }

    /* ── Folder ────────────────────────────────── */
    .folder {
        display: flex;
        flex-direction: column;
        border-right: 1px solid #0f172a;
        min-height: 0; /* allow flex shrink */
    }
    .folder-collapsed {
        /* When collapsed, show only the tab as a vertical bar */
    }

    .folder-tab {
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 5px 10px;
        border: none;
        cursor: pointer;
        font-size: 0.7rem;
        font-weight: 700;
        letter-spacing: 0.04em;
        white-space: nowrap;
        transition: filter 0.15s;
        flex-shrink: 0;
    }
    .folder-tab:hover {
        filter: brightness(1.3);
    }

    /* Collapsed: rotate tab to vertical sidebar */
    .folder-tab-collapsed {
        writing-mode: vertical-lr;
        text-orientation: mixed;
        padding: 10px 5px;
        flex: 1;
        justify-content: flex-start;
        min-width: 28px;
    }

    .tab-chevron {
        transition: transform 0.2s ease;
        transform: rotate(90deg);
        flex-shrink: 0;
    }
    .tab-chevron-collapsed {
        transform: rotate(0deg);
    }

    .tab-label {
        flex-shrink: 0;
    }

    .tab-count {
        font-size: 0.6rem;
        font-weight: 700;
        padding: 0px 5px;
        border-radius: 99px;
        min-width: 14px;
        text-align: center;
    }

    /* ── Strips row within folder ────────────── */
    .folder-strips {
        display: flex;
        flex-direction: row;
        flex: 1;
        overflow-x: visible;
        overflow-y: hidden;
        min-height: 0;
    }

    /* ── Channel strip: full-height vertical column ── */
    .channel-strip {
        display: flex;
        flex-direction: column;
        width: 80px;
        min-width: 80px;
        background: #1a2233;
        border-right: 1px solid #0f172a;
        padding: 6px 4px;
        gap: 4px;
    }
    .channel-strip:hover {
        background: #1e293b;
    }

    .ch-name-area {
        min-height: 18px;
    }
    .ch-name {
        font-size: 0.65rem;
        font-weight: 600;
        color: #e2e8f0;
        text-align: center;
        cursor: default;
        user-select: none;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        line-height: 1.3;
    }
    .ch-name-input {
        width: 100%;
        padding: 1px 2px;
        border: 1px solid #3b82f6;
        border-radius: 2px;
        background: #0f172a;
        color: #f1f5f9;
        font-size: 0.65rem;
        font-weight: 600;
        text-align: center;
        box-sizing: border-box;
    }
    .ch-name-input:focus {
        outline: none;
    }

    .ch-spacer {
        flex: 1;
    }

    .ch-plugin {
        display: flex;
        flex-direction: column;
        gap: 2px;
    }
    .ch-select {
        width: 100%;
        padding: 2px 2px;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #94a3b8;
        font-size: 0.55rem;
        cursor: pointer;
        appearance: auto;
    }
    .ch-select:focus {
        outline: none;
        border-color: #3b82f6;
    }

    .ch-edit-btn {
        width: 100%;
        padding: 2px;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #64748b;
        font-size: 0.6rem;
        cursor: pointer;
        text-align: center;
    }
    .ch-edit-btn:hover {
        background: #1e3a5f;
        color: #93c5fd;
        border-color: #3b82f6;
    }

    .ch-input-label {
        font-size: 0.55rem;
        color: #475569;
        text-align: center;
        padding-top: 2px;
        border-top: 1px solid #1e293b;
    }
</style>
