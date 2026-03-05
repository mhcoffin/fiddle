<script>
    import { onMount } from "svelte";
    import {
        FAMILY_ORDER,
        canonicalFamily,
        instrumentOrder,
    } from "./orchestralOrder.js";

    /** @type {{ onEditSetup?: () => void }} */
    let { onEditSetup = () => {} } = $props();

    let strips = $state([]);
    let availableInputs = $state([]);
    let scannedPlugins = $state([]);
    let availableXmaps = $state([]);
    let playbackDelay = $state(1000);
    let editingDelay = $state(false);
    let dirty = $state(false);

    /** @type {any} */
    const w = window;

    /**
     * Returns a callable function that invokes the named native function
     * registered via withNativeFunction() on the C++ side.
     * Uses JUCE's __juce__invoke event system.
     */
    const getNative = (name) => {
        const win = /** @type {any} */ (window);
        if (
            win.__JUCE__ &&
            win.__JUCE__.backend &&
            typeof win.__JUCE__.backend.emitEvent === "function"
        ) {
            let nextId = 0;
            return function () {
                win.__JUCE__.backend.emitEvent("__juce__invoke", {
                    name: name,
                    params: Array.prototype.slice.call(arguments),
                    resultId: nextId++,
                });
            };
        }
        return null;
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
    w.setDirtyState = (d) => {
        dirty = d;
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

    const doSaveConfig = () => {
        const fn = getNative("saveConfig");
        if (fn) fn();
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

    w.setExpressionMaps = (jsonStr) => {
        try {
            availableXmaps = JSON.parse(jsonStr);
        } catch (e) {
            console.error("[Mixer] xmap parse error:", e);
        }
    };

    // ── Selection state ───────────────────────────────────
    let selectedIds = $state(new Set());
    let lastClickedId = $state(null);
    let isMultiSelected = $derived(selectedIds.size > 1);
    let flatStripOrder = $derived(strips.map((s) => s.id));

    const handleStripClick = (stripId, e) => {
        if (e.metaKey && e.shiftKey) {
            // Cmd+Shift+Click: select all
            selectedIds = new Set(flatStripOrder);
        } else if (e.shiftKey && lastClickedId) {
            // Shift+Click: range select
            const a = flatStripOrder.indexOf(lastClickedId);
            const b = flatStripOrder.indexOf(stripId);
            if (a >= 0 && b >= 0) {
                const lo = Math.min(a, b);
                const hi = Math.max(a, b);
                const rangeIds = flatStripOrder.slice(lo, hi + 1);
                selectedIds = new Set([...selectedIds, ...rangeIds]);
            }
        } else if (e.metaKey) {
            // Cmd+Click: toggle
            const next = new Set(selectedIds);
            if (next.has(stripId)) next.delete(stripId);
            else next.add(stripId);
            selectedIds = next;
        } else {
            // Plain click: single select
            selectedIds = new Set([stripId]);
        }
        lastClickedId = stripId;
    };

    const clearSelection = () => {
        selectedIds = new Set();
        lastClickedId = null;
    };

    /** JSON-encode the selected strip IDs for group native calls */
    const selectedIdsJson = () => JSON.stringify([...selectedIds]);

    onMount(() => {
        const fn = getNative("getAvailableInputs");
        if (fn) fn();
        const fnMixer = getNative("requestMixerState");
        if (fnMixer) fnMixer();
        const fnPlugins = getNative("requestPluginsState");
        if (fnPlugins) fnPlugins();
        const fnXmaps = getNative("requestExpressionMaps");
        if (fnXmaps) fnXmaps();

        // Escape key clears selection
        const handleKeyDown = (e) => {
            if (e.key === "Escape") clearSelection();
        };
        window.addEventListener("keydown", handleKeyDown);
        return () => window.removeEventListener("keydown", handleKeyDown);
    });

    const addStrip = () => {
        const fn = getNative("addMixerStrip");
        if (fn) fn();
    };
    const removeStrip = (id) => {
        if (isMultiSelected && selectedIds.has(id)) {
            const fn = getNative("removeGroupStrips");
            if (fn) fn(selectedIdsJson());
            clearSelection();
            return;
        }
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
    };

    const setPlugin = (stripId, pluginUid) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            const fn = getNative("setGroupPlugin");
            if (fn) fn(selectedIdsJson(), pluginUid);
            return;
        }
        const fn = getNative("setStripPlugin");
        if (fn) fn(stripId, pluginUid);
    };
    const showEditor = (stripId) => {
        const fn = getNative("showStripEditor");
        if (fn) fn(stripId);
    };
    const loadExpressionMap = (stripId, entityID) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            const fn = getNative("setGroupExpressionMap");
            if (fn) fn(selectedIdsJson(), entityID);
            return;
        }
        const fn = getNative("loadExpressionMap");
        if (fn) fn(stripId, entityID);
    };
    const loadExpressionMapFromFile = (stripId) => {
        const fn = getNative("loadExpressionMapFromFile");
        if (fn) fn(stripId);
    };
    const clearExpressionMap = (stripId) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            const fn = getNative("setGroupExpressionMap");
            if (fn) fn(selectedIdsJson(), "");
            return;
        }
        const fn = getNative("clearExpressionMap");
        if (fn) fn(stripId);
    };

    const setGain = (stripId, db) => {
        const fn = getNative("setStripGain");
        if (fn) fn(stripId, db);
    };

    // ── Mute / Solo ──────────────────────────────────────────────
    let anySoloed = $derived(strips.some((s) => s.soloed));

    const isAudible = (strip) => !strip.muted && (!anySoloed || strip.soloed);

    const toggleMute = (stripId) => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const newVal = !strip.muted;
        if (isMultiSelected && selectedIds.has(stripId)) {
            for (const id of selectedIds) {
                const fn = getNative("setStripMute");
                if (fn) fn(id, newVal);
            }
        } else {
            const fn = getNative("setStripMute");
            if (fn) fn(stripId, newVal);
        }
    };

    const toggleSolo = (stripId) => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const newVal = !strip.soloed;
        if (isMultiSelected && selectedIds.has(stripId)) {
            for (const id of selectedIds) {
                const fn = getNative("setStripSolo");
                if (fn) fn(id, newVal);
            }
        } else {
            const fn = getNative("setStripSolo");
            if (fn) fn(stripId, newVal);
        }
    };

    const clearSolos = () => {
        const fn = getNative("setStripSolo");
        if (!fn) return;
        for (const s of strips) fn(s.id, false);
    };

    /** Group-aware gain change: fader drag sends delta */
    const handleFaderInput = (strip, pos) => {
        const newDb = Math.round(posToDB(pos) * 10) / 10;
        if (isMultiSelected && selectedIds.has(strip.id)) {
            const oldDb = strip.gainDb ?? 0;
            const delta = newDb - oldDb;
            const fn = getNative("setGroupGainDelta");
            if (fn) fn(selectedIdsJson(), delta);
        } else {
            setGain(strip.id, newDb);
        }
    };

    /** Group-aware gain change: typed value sets absolute */
    const handleGainValueInput = (strip, value) => {
        let v = parseFloat(value);
        if (isNaN(v)) v = 0;
        v = Math.max(-120, Math.min(6, v));
        if (isMultiSelected && selectedIds.has(strip.id)) {
            const fn = getNative("setGroupGainAbsolute");
            if (fn) fn(selectedIdsJson(), v);
        } else {
            setGain(strip.id, v);
        }
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

    /** Get the input instrument name for a strip */
    const getInputName = (strip) => {
        if (strip.inputPort < 0) return "";
        const input = availableInputs.find(
            (i) =>
                i.port === strip.inputPort && i.channel === strip.inputChannel,
        );
        return input ? input.label || input.name : "";
    };

    let editingId = $state(null);
    let editValue = $state("");
    const startEditing = (strip) => {
        editingId = strip.id;
        editValue = strip.library || "";
    };
    const commitEdit = (stripId) => {
        if (editValue.trim()) {
            const lib = editValue.trim();
            if (isMultiSelected && selectedIds.has(stripId)) {
                const fn = getNative("setGroupLibrary");
                if (fn) fn(selectedIdsJson(), lib);
            } else {
                const fn = getNative("setStripLibrary");
                if (fn) fn(stripId, lib);
            }
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
        // Within each group, solos first then sections, then by score order
        for (const g of groups) {
            g.strips.sort((a, b) => {
                if (a.isSolo !== b.isSolo) return a.isSolo ? -1 : 1;
                // Look up instrument name from availableInputs
                const nameA =
                    availableInputs.find(
                        (i) =>
                            i.port === a.inputPort &&
                            i.channel === a.inputChannel,
                    )?.name || "";
                const nameB =
                    availableInputs.find(
                        (i) =>
                            i.port === b.inputPort &&
                            i.channel === b.inputChannel,
                    )?.name || "";
                return instrumentOrder(nameA) - instrumentOrder(nameB);
            });
        }
        return groups;
    });
</script>

<div class="mixer-container">
    <div class="mixer-toolbar">
        <h2>Mixer</h2>
        {#if selectedIds.size > 1}
            <span class="selection-badge">{selectedIds.size} selected</span>
        {/if}
        {#if anySoloed}
            <button class="toolbar-ms-btn solo-clear" onclick={clearSolos}>
                Clear Solos
            </button>
        {/if}
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
            <button
                class="toolbar-btn edit-setup-btn"
                title="Edit Setup"
                onclick={onEditSetup}
            >
                <svg
                    viewBox="0 0 20 20"
                    width="14"
                    height="14"
                    fill="currentColor"
                >
                    <path
                        d="M13.586 3.586a2 2 0 112.828 2.828l-.793.793-2.828-2.828.793-.793zM11.379 5.793L3 14.172V17h2.828l8.38-8.379-2.83-2.828z"
                    />
                </svg>
            </button>
            <button
                class="toolbar-btn save-btn"
                onclick={doSaveConfig}
                disabled={!dirty}
                title="Save config (creates a new version)"
            >
                💾 Save
            </button>
        </div>
    </div>

    {#if strips.length === 0}
        <div class="empty-state">
            <p>
                No channel strips. Click the <strong>✏️ Edit</strong> button to set
                up your ensemble.
            </p>
        </div>
    {:else}
        <!-- svelte-ignore a11y_click_events_have_key_events -->
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <div class="console" onclick={clearSelection}>
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
                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                <!-- svelte-ignore a11y_no_static_element_interactions -->
                                <div
                                    class="channel-strip"
                                    class:selected={selectedIds.has(strip.id)}
                                    class:suppressed={!isAudible(strip)}
                                    onclick={(e) => e.stopPropagation()}
                                >
                                    <!-- svelte-ignore a11y_click_events_have_key_events -->
                                    <!-- svelte-ignore a11y_no_static_element_interactions -->
                                    <div
                                        class="select-bar select-bar-top"
                                        style="background: {group.colors
                                            .accent};"
                                        onclick={(e) => {
                                            e.stopPropagation();
                                            handleStripClick(strip.id, e);
                                        }}
                                    ></div>
                                    <!-- Input selector at top -->
                                    <div class="ch-input">
                                        <select
                                            class="ch-select"
                                            disabled={isMultiSelected &&
                                                selectedIds.has(strip.id)}
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
                                        <div class="fader-meter-row">
                                            <div class="fader-track">
                                                <input
                                                    class="fader-slider"
                                                    type="range"
                                                    min="0"
                                                    max="1000"
                                                    step="1"
                                                    value={Math.round(
                                                        dbToPos(
                                                            strip.gainDb ?? 0,
                                                        ) * 1000,
                                                    )}
                                                    oninput={(e) => {
                                                        const pos =
                                                            parseFloat(
                                                                /** @type {HTMLInputElement} */ (
                                                                    e.target
                                                                ).value,
                                                            ) / 1000;
                                                        handleFaderInput(
                                                            strip,
                                                            pos,
                                                        );
                                                    }}
                                                    ondblclick={() =>
                                                        handleGainValueInput(
                                                            strip,
                                                            "0",
                                                        )}
                                                />
                                            </div>
                                            <div class="meter-track">
                                                <div
                                                    class="meter-fill"
                                                    class:meter-hot={strip.peakDb >
                                                        0}
                                                    style="height: {dbToPos(
                                                        strip.peakDb ?? -120,
                                                    ) * 100}%"
                                                ></div>
                                                <div
                                                    class="meter-hold"
                                                    class:meter-hot={strip.peakHoldDb >
                                                        0}
                                                    style="bottom: {dbToPos(
                                                        strip.peakHoldDb ??
                                                            -120,
                                                    ) * 100}%"
                                                ></div>
                                            </div>
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
                                                handleGainValueInput(
                                                    strip,
                                                    /** @type {HTMLInputElement} */ (
                                                        e.target
                                                    ).value,
                                                );
                                            }}
                                        />
                                    </div>

                                    <!-- Mute / Solo -->
                                    <div class="ch-mute-solo">
                                        <button
                                            class="ms-btn mute-btn"
                                            class:active={strip.muted}
                                            title={strip.muted
                                                ? "Unmute"
                                                : "Mute"}
                                            onclick={() => toggleMute(strip.id)}
                                            >M</button
                                        >
                                        <button
                                            class="ms-btn solo-btn"
                                            class:active={strip.soloed}
                                            title={strip.soloed
                                                ? "Unsolo"
                                                : "Solo"}
                                            onclick={() => toggleSolo(strip.id)}
                                            >S</button
                                        >
                                    </div>

                                    <!-- Expression Map -->
                                    <div class="ch-xmap">
                                        <select
                                            class="ch-select ch-xmap-select"
                                            value={strip.expressionMapName
                                                ? "__loaded__"
                                                : ""}
                                            onchange={(e) => {
                                                const val =
                                                    /** @type {HTMLSelectElement} */ (
                                                        e.target
                                                    ).value;
                                                if (val === "__file__") {
                                                    loadExpressionMapFromFile(
                                                        strip.id,
                                                    );
                                                    // reset select after
                                                    /** @type {HTMLSelectElement} */ (
                                                        e.target
                                                    ).value =
                                                        strip.expressionMapName
                                                            ? "__loaded__"
                                                            : "";
                                                } else if (val === "") {
                                                    clearExpressionMap(
                                                        strip.id,
                                                    );
                                                } else if (
                                                    val !== "__loaded__"
                                                ) {
                                                    loadExpressionMap(
                                                        strip.id,
                                                        val,
                                                    );
                                                }
                                            }}
                                        >
                                            <option value="">— xmap —</option>
                                            {#if strip.expressionMapName}
                                                <option
                                                    value="__loaded__"
                                                    selected
                                                    >{strip.expressionMapName}</option
                                                >
                                            {/if}
                                            {#each availableXmaps as xmap}
                                                <option value={xmap.entityID}
                                                    >{xmap.name}</option
                                                >
                                            {/each}
                                            <option value="__file__"
                                                >Load from file…</option
                                            >
                                        </select>
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
                                                {#each scannedPlugins.filter((p) => p.valid !== false) as plugin}
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

                                    <!-- Strip name area -->
                                    <div class="ch-name-area">
                                        <div
                                            class="ch-input-name"
                                            title={getInputName(strip)}
                                        >
                                            {getInputName(strip) || "—"}
                                        </div>
                                        {#if editingId === strip.id}
                                            <input
                                                class="ch-name-input"
                                                type="text"
                                                placeholder="Library"
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
                                                class="ch-lib-name"
                                                ondblclick={() =>
                                                    startEditing(strip)}
                                                title="Double-click to set library name"
                                            >
                                                {strip.library || "—"}
                                            </div>
                                        {/if}
                                    </div>

                                    <!-- Strip action buttons -->
                                    <div class="ch-buttons">
                                        <button
                                            class="ch-btn ch-btn-dup"
                                            disabled={isMultiSelected &&
                                                selectedIds.has(strip.id)}
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
                                    <!-- svelte-ignore a11y_click_events_have_key_events -->
                                    <!-- svelte-ignore a11y_no_static_element_interactions -->
                                    <div
                                        class="select-bar select-bar-bottom"
                                        style="background: {group.colors
                                            .accent};"
                                        onclick={(e) => {
                                            e.stopPropagation();
                                            handleStripClick(strip.id, e);
                                        }}
                                    ></div>
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
    .selection-badge {
        font-size: 0.7rem;
        font-weight: 600;
        color: #38bdf8;
        background: rgba(56, 189, 248, 0.1);
        border: 1px solid rgba(56, 189, 248, 0.3);
        border-radius: 4px;
        padding: 2px 8px;
        margin-left: 8px;
    }

    .toolbar-ms-btn {
        font-size: 0.7rem;
        font-weight: 600;
        padding: 2px 10px;
        border: 1px solid #555;
        border-radius: 4px;
        background: #2a2a2a;
        color: #aaa;
        cursor: pointer;
        transition: all 0.12s ease;
    }

    .toolbar-ms-btn:hover {
        border-color: #888;
        color: #e0e0e0;
        background: #383838;
    }

    .toolbar-ms-btn.solo-clear {
        border-color: rgba(34, 197, 94, 0.4);
        color: #4ade80;
    }
    .toolbar-right {
        display: flex;
        align-items: center;
        gap: 12px;
    }

    .toolbar-btn {
        padding: 4px 10px;
        font-size: 0.7rem;
        font-weight: 600;
        border-radius: 4px;
        cursor: pointer;
        transition:
            background 0.15s,
            transform 0.1s;
        white-space: nowrap;
        display: flex;
        align-items: center;
        justify-content: center;
        height: 28px;
        box-sizing: border-box;
    }
    .toolbar-btn:active {
        transform: scale(0.97);
    }
    .save-btn {
        color: #e2e8f0;
        background: #1e40af;
        border: 1px solid #3b82f6;
    }
    .save-btn:hover {
        background: #2563eb;
    }
    .save-btn:disabled {
        opacity: 0.4;
        cursor: default;
        transform: none;
    }
    .edit-setup-btn {
        color: #94a3b8;
        background: rgba(30, 41, 59, 0.85);
        border: 1px solid #334155;
    }
    .edit-setup-btn:hover {
        background: rgba(56, 189, 248, 0.15);
        border-color: #38bdf8;
        color: #38bdf8;
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
        color: #94a3b8;
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
        background: #111827;
        border-right: 1px solid #0f172a;
        padding: 6px 4px;
        gap: 4px;
        overflow: hidden;
    }
    .channel-strip:hover {
        background: #151d2e;
    }
    .channel-strip.selected {
        background: #1e293b;
        outline: 1px solid rgba(56, 189, 248, 0.3);
        outline-offset: -1px;
    }
    .channel-strip.selected:hover {
        background: #243044;
    }

    .select-bar {
        flex-shrink: 0;
        height: 6px;
        cursor: pointer;
        transition: filter 0.15s;
    }
    .select-bar:hover {
        filter: brightness(1.4);
    }
    .selected .select-bar {
        filter: brightness(1.6);
    }

    .ch-name-area {
        min-height: 18px;
    }
    .ch-input-name {
        font-size: 0.6rem;
        font-weight: 600;
        color: #e2e8f0;
        text-align: center;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        line-height: 1.3;
    }
    .ch-lib-name {
        font-size: 0.55rem;
        color: #94a3b8;
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
        color: #94a3b8;
        line-height: 1;
        flex-shrink: 0;
    }

    .fader-meter-row {
        display: flex;
        flex: 1;
        gap: 2px;
        align-items: stretch;
        width: 100%;
        min-height: 40px;
    }

    .fader-track {
        flex: 1;
        display: flex;
        align-items: center;
        justify-content: center;
        position: relative;
    }

    .meter-track {
        width: 4px;
        flex-shrink: 0;
        background: #0f172a;
        border-radius: 2px;
        position: relative;
        overflow: hidden;
    }
    .meter-fill {
        position: absolute;
        bottom: 0;
        left: 0;
        right: 0;
        background: #22c55e;
        border-radius: 2px;
        will-change: height;
    }
    .meter-hot {
        background: #ef4444;
    }
    .meter-hold {
        position: absolute;
        left: 0;
        right: 0;
        height: 2px;
        background: #4ade80;
        will-change: bottom;
    }
    .meter-hold.meter-hot {
        background: #f87171;
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

    /* Mute / Solo buttons */
    .ch-mute-solo {
        display: flex;
        gap: 4px;
        justify-content: center;
        padding: 4px 0;
    }

    .ms-btn {
        width: 22px;
        height: 18px;
        border: 1px solid #555;
        border-radius: 3px;
        background: #2a2a2a;
        color: #888;
        font-size: 0.6rem;
        font-weight: 700;
        cursor: pointer;
        padding: 0;
        line-height: 1;
        transition: all 0.12s ease;
    }

    .ms-btn:hover {
        border-color: #888;
        color: #ccc;
    }

    .mute-btn.active {
        background: rgba(245, 158, 11, 0.25);
        border-color: #f59e0b;
        color: #fbbf24;
        box-shadow: 0 0 4px rgba(245, 158, 11, 0.3);
    }

    .solo-btn.active {
        background: rgba(34, 197, 94, 0.2);
        border-color: #22c55e;
        color: #4ade80;
        box-shadow: 0 0 4px rgba(34, 197, 94, 0.3);
    }

    /* Dim non-audible strips */
    .channel-strip.suppressed {
        opacity: 0.6;
    }
    .channel-strip.suppressed .ms-btn {
        opacity: 1;
    }

    .ch-xmap {
        display: flex;
        flex-direction: column;
        margin-bottom: 2px;
    }
    .ch-xmap-select {
        font-size: 0.6rem;
        max-width: 100%;
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
        color: #cbd5e1;
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
        color: #94a3b8;
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
        color: #cbd5e1;
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
