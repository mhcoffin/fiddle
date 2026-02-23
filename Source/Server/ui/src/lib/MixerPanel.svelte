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

    const duplicateStrip = (id) => {
        const fn = getNative("duplicateStripInput");
        if (fn) fn(id);
    };

    const setInput = (stripId, port, channel) => {
        const fn = getNative("setStripInput");
        if (fn) fn(stripId, port, channel);

        // Auto-name: use input label, with (n) disambiguation for duplicates
        const input = availableInputs.find(
            (i) => i.port === port && i.channel === channel,
        );
        if (input) {
            const baseName = input.label || input.name;
            // Count how many other strips share this same input
            const sameInput = strips.filter(
                (s) =>
                    s.id !== stripId &&
                    s.inputPort === port &&
                    s.inputChannel === channel,
            );
            const name =
                sameInput.length > 0
                    ? `${baseName} (${sameInput.length + 1})`
                    : baseName;
            const renameFn = getNative("setStripName");
            if (renameFn) renameFn(stripId, name);
        }
    };

    const setPlugin = (stripId, pluginUid) => {
        const fn = getNative("setStripPlugin");
        if (fn) fn(stripId, pluginUid);
    };
    const showEditor = (stripId) => {
        const fn = getNative("showStripEditor");
        if (fn) fn(stripId);
    };

    const setGain = (stripId, db) => {
        const fn = getNative("setStripGain");
        if (fn) fn(stripId, db);
    };

    // ── Fader skew (JUCE-style NormalisableRange) ────────────
    // Maps slider position (0..1) ↔ dB (-120..+6) with a power curve.
    // Skew < 1 gives more resolution at the top (useful range).
    const FADER_MIN = -120;
    const FADER_MAX = 6;
    const FADER_SKEW = 0.25;

    /** Normalized position (0..1) → dB */
    const posToDB = (pos) => {
        if (pos <= 0) return FADER_MIN;
        if (pos >= 1) return FADER_MAX;
        return FADER_MIN + (FADER_MAX - FADER_MIN) * Math.pow(pos, FADER_SKEW);
    };

    /** dB → normalized position (0..1) */
    const dbToPos = (db) => {
        if (db <= FADER_MIN) return 0;
        if (db >= FADER_MAX) return 1;
        return Math.pow(
            (db - FADER_MIN) / (FADER_MAX - FADER_MIN),
            1 / FADER_SKEW,
        );
    };

    /** Format dB for display */
    const formatDb = (db) => {
        if (db <= -120) return "-∞";
        if (db >= 0) return "+" + db.toFixed(1);
        return db.toFixed(1);
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
                                    <!-- Input selector at top -->
                                    <div class="ch-input">
                                        <select
                                            class="ch-select"
                                            value={`${strip.inputPort}:${strip.inputChannel}`}
                                            onchange={(e) => {
                                                const [p, c] =
                                                    /** @type {HTMLSelectElement} */ (
                                                        e.target
                                                    ).value
                                                        .split(":")
                                                        .map(Number);
                                                setInput(strip.id, p, c);
                                            }}
                                        >
                                            <option value="-1:-1"
                                                >— None —</option
                                            >
                                            {#each availableInputs as input}
                                                {@const icon = input.isSolo
                                                    ? "👤"
                                                    : "👥"}
                                                <option
                                                    value={`${input.port}:${input.channel}`}
                                                    >{icon}
                                                    {input.label ||
                                                        input.name}</option
                                                >
                                            {/each}
                                        </select>
                                    </div>
                                    {#if strip.inputPort >= 0}
                                        <div class="ch-input-label">
                                            P{strip.inputPort +
                                                1}.{strip.inputChannel + 1}
                                        </div>
                                    {/if}

                                    <!-- Vertical fader -->
                                    <div class="ch-fader">
                                        <span class="fader-tick fader-top"
                                            >+6</span
                                        >
                                        <div class="fader-track">
                                            <input
                                                class="fader-slider"
                                                type="range"
                                                min="0"
                                                max="1000"
                                                step="1"
                                                value={Math.round(
                                                    dbToPos(strip.gainDb ?? 0) *
                                                        1000,
                                                )}
                                                oninput={(e) => {
                                                    const pos =
                                                        parseFloat(
                                                            /** @type {HTMLInputElement} */ (
                                                                e.target
                                                            ).value,
                                                        ) / 1000;
                                                    setGain(
                                                        strip.id,
                                                        Math.round(
                                                            posToDB(pos) * 10,
                                                        ) / 10,
                                                    );
                                                }}
                                                ondblclick={() =>
                                                    setGain(strip.id, 0)}
                                            />
                                        </div>
                                        <span class="fader-tick fader-bot"
                                            >-∞</span
                                        >
                                        <input
                                            class="fader-value"
                                            type="number"
                                            min="-120"
                                            max="6"
                                            step="0.1"
                                            value={strip.gainDb != null
                                                ? Math.round(
                                                      strip.gainDb * 10,
                                                  ) / 10
                                                : 0}
                                            onchange={(e) => {
                                                let v = parseFloat(
                                                    /** @type {HTMLInputElement} */ (
                                                        e.target
                                                    ).value,
                                                );
                                                if (isNaN(v)) v = 0;
                                                v = Math.max(
                                                    -120,
                                                    Math.min(6, v),
                                                );
                                                setGain(strip.id, v);
                                            }}
                                        />
                                    </div>

                                    <!-- Plugin -->
                                    <div class="ch-plugin">
                                        <div class="ch-plugin-row">
                                            <select
                                                class="ch-select"
                                                value={strip.pluginUid || 0}
                                                onchange={(e) => {
                                                    const uid = Number(
                                                        /** @type {HTMLSelectElement} */ (
                                                            e.target
                                                        ).value,
                                                    );
                                                    if (uid)
                                                        setPlugin(
                                                            strip.id,
                                                            uid,
                                                        );
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
                                                    title="Open editor"
                                                    >e</button
                                                >
                                            {/if}
                                        </div>
                                    </div>

                                    <!-- Editable strip name at bottom -->
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

                                    <!-- Strip action buttons -->
                                    <div class="ch-buttons">
                                        <button
                                            class="ch-btn ch-btn-dup"
                                            onclick={() =>
                                                duplicateStrip(strip.id)}
                                            title="Add parallel strip with same input"
                                            >+</button
                                        >
                                        <button
                                            class="ch-btn ch-btn-del"
                                            onclick={() =>
                                                removeStrip(strip.id)}
                                            title="Delete strip">✕</button
                                        >
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
        flex-shrink: 0;
        border-right: 1px solid #0f172a;
        min-height: 0;
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

    /* ── Fader ────────────────────────────── */
    .ch-fader {
        display: flex;
        flex-direction: column;
        align-items: center;
        flex: 1;
        min-height: 60px;
        gap: 2px;
        padding: 2px 0;
    }

    .fader-tick {
        font-size: 0.5rem;
        color: #475569;
        line-height: 1;
        flex-shrink: 0;
    }

    .fader-track {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: center;
        width: 100%;
        position: relative;
        min-height: 40px;
    }

    .fader-slider {
        writing-mode: vertical-lr;
        direction: rtl; /* top = max (+6), bottom = min (-120) */
        width: 18px;
        height: 100%;
        cursor: pointer;
        accent-color: #3b82f6;
        -webkit-appearance: slider-vertical;
        appearance: slider-vertical;
        margin: 0;
    }

    .fader-value {
        width: 100%;
        padding: 1px 2px;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #94a3b8;
        font-size: 0.55rem;
        text-align: center;
        flex-shrink: 0;
        -moz-appearance: textfield;
        box-sizing: border-box;
    }
    .fader-value:focus {
        outline: none;
        border-color: #3b82f6;
    }
    .fader-value::-webkit-inner-spin-button,
    .fader-value::-webkit-outer-spin-button {
        -webkit-appearance: none;
    }

    .ch-plugin {
        display: flex;
        flex-direction: column;
        gap: 2px;
    }
    .ch-plugin-row {
        display: flex;
        gap: 2px;
        align-items: center;
    }
    .ch-select {
        flex: 1;
        min-width: 0;
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
        padding: 1px 4px;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #64748b;
        font-size: 0.6rem;
        font-style: italic;
        cursor: pointer;
        line-height: 1;
        flex-shrink: 0;
    }
    .ch-edit-btn:hover {
        background: #1e3a5f;
        color: #93c5fd;
        border-color: #3b82f6;
    }

    .ch-input-label {
        font-size: 0.6rem;
        color: #64748b;
        text-align: center;
        padding: 1px 0;
    }

    .ch-buttons {
        display: flex;
        gap: 2px;
        margin-top: 2px;
    }
    .ch-btn {
        flex: 1;
        padding: 2px 0;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #475569;
        font-size: 0.65rem;
        cursor: pointer;
        text-align: center;
        transition: all 0.12s;
    }
    .ch-btn-dup:hover {
        background: #1e3a5f;
        color: #93c5fd;
        border-color: #3b82f6;
    }
    .ch-btn-del:hover {
        background: #451a1a;
        color: #f87171;
        border-color: #dc2626;
    }
</style>
