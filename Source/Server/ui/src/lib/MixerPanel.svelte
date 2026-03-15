<script>
    import { onMount, onDestroy, untrack } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import BranchSelector from "./BranchSelector.svelte";
    import {
        FAMILY_ORDER,
        canonicalFamily,
        instrumentOrder,
    } from "./orchestralOrder.js";



    let strips = $state([]);
    let availableInputs = $state([]);
    let scannedPlugins = $state([]);
    let availableXmaps = $state([]);
    let branches = $state([]);
    let currentBranch = $state("default");

    let playbackDelay = $state(1000);
    let editingDelay = $state(false);
    let dirty = $state(false);
    let isDetachedHead = $state(false);

    /** @type {any} */
    const w = window;

    /**
     * Returns a callable function that invokes the named native function
     * registered via withNativeFunction() on the C++ side.
     * Uses JUCE's __juce__invoke event system.
     */
    const FAMILY_COLORS = {
        woodwinds: { accent: "#38bdf8", header: "#0e4d6e", text: "#bae6fd" },
        brass: { accent: "#f59e0b", header: "#713f12", text: "#fde68a" },
        percussion: { accent: "#a8896b", header: "#44342a", text: "#d4c4b0" },
        keys: { accent: "#c084fc", header: "#581c87", text: "#e9d5ff" },
        strings: { accent: "#34d399", header: "#14532d", text: "#a7f3d0" },
        choir: { accent: "#94a3b8", header: "#334155", text: "#e2e8f0" },
    };
    const defaultColors = FAMILY_COLORS.choir;

    // ── C++ → JS message handlers ─────────────────────────────────
    // data arrives as parsed objects (no JSON.parse needed)

    onFromCpp("setMixerState", (data) => {
        try {
            strips = data;
        } catch (e) {
            console.error("[Mixer] setMixerState error:", e);
        }
    });
    onFromCpp("setBranches", (data) => {
        try {
            branches = data;
        } catch (e) {
            console.error("[Mixer] setBranches error:", e);
        }
    });
    onFromCpp("setCurrentBranch", (name) => {
        currentBranch = name;
    });
    // setInstrumentMap is broadcast by C++ on startup and after Save Ensemble.
    // Populate availableInputs so strips can look up their instrument name.
    onFromCpp("setInstrumentMap", (data) => {
        try {
            availableInputs = Array.isArray(data) ? data : [];
        } catch (e) {
            console.error("[Mixer] setInstrumentMap error:", e);
        }
    });
    // Legacy request/response path (getAvailableInputs → setAvailableInputs JS eval).
    onFromCpp("setAvailableInputs", (data) => {
        try {
            availableInputs = data;
        } catch (e) {
            console.error("[Mixer] setAvailableInputs error:", e);
        }
    });
    onFromCpp("setPlaybackDelay", (ms) => {
        playbackDelay = ms;
    });
    onFromCpp("setDirtyState", (d) => {
        dirty = d;
    });
    onFromCpp("setPluginList", (data) => {
        try {
            scannedPlugins = data;
        } catch (e) {
            /* ignore */
        }
    });
    onFromCpp("setExpressionMaps", (data) => {
        try {
            availableXmaps = data;
        } catch (e) {
            console.error("[Mixer] xmap error:", e);
        }
    });
    onFromCpp("setDetachedHead", (detached) => {
        isDetachedHead = detached;
    });

    const updateDelay = (ms) => {
        const val = Math.max(0, Math.min(5000, parseInt(ms) || 0));
        playbackDelay = val;
        dispatchCpp("setPlaybackDelay", val);
    };

    const doSaveConfig = () => {
        dispatchCpp("saveConfig");
    };

    // getNative is no longer needed (now using onFromCpp / dispatchCpp instead),
    // but kept as a no-op stub in case any legacy call sites remain.
    const getNative = (_name) => null;

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
        dispatchCpp("getPlaybackDelay");
        dispatchCpp("requestBranches");
        dispatchCpp("requestMixerState");
        dispatchCpp("getAvailableInputs");
        dispatchCpp("requestPluginsState");
        dispatchCpp("requestExpressionMaps");
        dispatchCpp("requestCurrentBranch");

        // Escape key clears selection
        const handleKeyDown = (e) => {
            if (e.key === "Escape") clearSelection();
        };
        window.addEventListener("keydown", handleKeyDown);
        return () => window.removeEventListener("keydown", handleKeyDown);
    });

    const addStrip = () => {
        dispatchCpp("addMixerStrip");
    };
    const removeStrip = (id) => {
        if (isMultiSelected && selectedIds.has(id)) {
            dispatchCpp("removeGroupStrips", selectedIdsJson());
            clearSelection();
            return;
        }
        dispatchCpp("removeMixerStrip", id);
    };

    const duplicateStrip = (id) => {
        dispatchCpp("duplicateStripInput", id);
    };

    const setInput = (stripId, port, channel) => {
        dispatchCpp("setStripInput", stripId, port, channel);
    };

    const setPlugin = (stripId, pluginUid) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            dispatchCpp("setGroupPlugin", selectedIdsJson(), pluginUid);
            return;
        }
        dispatchCpp("setStripPlugin", stripId, pluginUid);
    };
    const showEditor = (stripId) => {
        dispatchCpp("showStripEditor", stripId);
    };
    const loadExpressionMap = (stripId, entityID) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            dispatchCpp("setGroupExpressionMap", selectedIdsJson(), entityID);
            return;
        }
        dispatchCpp("loadExpressionMap", stripId, entityID);
    };
    const loadExpressionMapFromFile = (stripId) => {
        dispatchCpp("loadExpressionMapFromFile", stripId);
    };
    const clearExpressionMap = (stripId) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            dispatchCpp("setGroupExpressionMap", selectedIdsJson(), "");
            return;
        }
        dispatchCpp("clearExpressionMap", stripId);
    };

    // Shadow map: last gain value we sent for each strip (plain object, not reactive).
    // Used so that rapid master-fader events read our own last write, not the async
    // IPC-reflected strip.gainDb (which may still show the old value).
    const gainShadow = {};

    // Unconstrained internal shadow used for all group-master delta math.
    // Allows lock-on redistribution to accumulate values outside [-120,+6] so
    // the theoretical sum stays exact even when displayed faders are clamped.
    const gainShadowRaw = {};

    const setGain = (stripId, db) => {
        gainShadow[stripId] = db;
        gainShadowRaw[stripId] = db; // user-dragged values are already in display range
        dispatchCpp("setStripGain", stripId, db);
    };

    /** Send an unconstrained raw value to a strip: store raw for math, send clamped to audio. */
    const setGainRaw = (stripId, rawDb) => {
        gainShadowRaw[stripId] = rawDb;
        const clamped = Math.max(FADER_MIN, Math.min(FADER_MAX, rawDb));
        gainShadow[stripId] = clamped;
        dispatchCpp("setStripGain", stripId, clamped);
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
                dispatchCpp("setStripMute", id, newVal);
            }
        } else {
            dispatchCpp("setStripMute", stripId, newVal);
        }
    };

    const toggleSolo = (stripId) => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const newVal = !strip.soloed;
        if (isMultiSelected && selectedIds.has(stripId)) {
            for (const id of selectedIds) {
                dispatchCpp("setStripSolo", id, newVal);
            }
        } else {
            dispatchCpp("setStripSolo", stripId, newVal);
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
            dispatchCpp("setGroupGainDelta", selectedIdsJson(), delta);
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
            dispatchCpp("setGroupGainAbsolute", selectedIdsJson(), v);
        } else {
            setGain(strip.id, v);
        }
    };

    // ── Group Master Fader state ──────────────────────────────
    // Only lockSum is stored in state. masterDb is always DERIVED as the sum of
    // member strip gainDb values — it is never stored independently.
    let groupMasters = $state(
        /** @type {Record<string, {lockSum: boolean}>} */ ({}),
    );

    const getGroupMaster = (key) => {
        if (!groupMasters[key]) {
            untrack(() => {
                groupMasters[key] = { lockSum: false };
            });
        }
        return groupMasters[key];
    };

    /** Compute the current theoretical sum of gainDb for all strips in a group.
     *  Uses gainShadowRaw so the value is accurate even if some faders are clamped. */
    const getGroupSumDb = (instrGroup) =>
        instrGroup.strips.reduce(
            (acc, s) => acc + (gainShadowRaw[s.id] ?? s.gainDb ?? 0),
            0,
        );

    /**
     * Master fader drag → distribute delta equally: each strip gets delta/n.
     * delta = newSumDb - currentSum, each strip += delta/n.
     * Uses unconstrained raw values so repeated moves accumulate correctly.
     */
    const handleMasterFaderInput = (instrGroup, pos) => {
        const newSumDb = Math.round(posToDB(pos) * 10) / 10;
        const currentSum = getGroupSumDb(instrGroup);
        const delta = newSumDb - currentSum;
        const n = instrGroup.strips.length;
        for (const strip of instrGroup.strips) {
            const base = gainShadowRaw[strip.id] ?? strip.gainDb ?? 0;
            setGainRaw(strip.id, base + delta / n);
        }
    };

    /** Double-click master fader → set master to 0 dB (average 0 → sum 0) */
    const handleMasterFaderReset = (instrGroup) => {
        // Target: sum = 0, so each strip moves by -currentSum/n
        handleMasterFaderInput(instrGroup, dbToPos(0));
    };

    /**
     * Individual strip fader drag within a group.
     * Lock OFF: only the dragged strip changes; master auto-follows (derived sum updates).
     * Lock ON:  master is "locked" — distribute the inverse delta equally across siblings
     *           so the sum stays constant. Uses gainShadowRaw so accumulated values beyond
     *           the display range don't corrupt the sum invariant.
     */
    const handleGroupFaderInput = (instrGroup, strip, pos) => {
        const gm = getGroupMaster(instrGroup.key);
        const newDb = Math.round(posToDB(pos) * 10) / 10;
        if (gm.lockSum && instrGroup.strips.length > 1) {
            const oldDb = gainShadowRaw[strip.id] ?? strip.gainDb ?? 0;
            const delta = newDb - oldDb;
            const others = instrGroup.strips.filter((s) => s.id !== strip.id);
            const compensate = -delta / others.length;
            for (const other of others) {
                const base = gainShadowRaw[other.id] ?? other.gainDb ?? 0;
                setGainRaw(other.id, base + compensate);
            }
        }
        setGain(strip.id, newDb);
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
                dispatchCpp("setGroupLibrary", selectedIdsJson(), lib);
            } else {
                dispatchCpp("setStripLibrary", stripId, lib);
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

            // Second-level grouping: by inputPort:inputChannel (instrument groups)
            const instrMap = new Map();
            const instrOrder = []; // preserve the sort order above
            for (const strip of g.strips) {
                const key = `${strip.inputPort}:${strip.inputChannel}`;
                if (!instrMap.has(key)) {
                    const input = availableInputs.find(
                        (i) => i.port === strip.inputPort && i.channel === strip.inputChannel,
                    );
                    const portLabel =
                        strip.inputPort >= 0
                            ? `P${strip.inputPort + 1}.${strip.inputChannel + 1}`
                            : "";
                    instrMap.set(key, {
                        key,
                        label: input ? (input.label || input.name) : (portLabel || "—"),
                        portLabel,
                        strips: [],
                    });
                    instrOrder.push(key);
                }
                instrMap.get(key).strips.push(strip);
            }
            g.instrGroups = instrOrder.map((k) => instrMap.get(k));
        }
        return groups;
    });

</script>

<div class="mixer-container">
    <div class="mixer-toolbar">
        <div class="toolbar-left">

            <BranchSelector
                {branches}
                {currentBranch}
                onCheckout={(id) => {
                    dispatchCpp("checkoutBranch", id);
                }}
                onCreateBranch={(name) => {
                    dispatchCpp("createBranch", name);
                }}
            />

            {#if selectedIds.size > 1}
                <span class="selection-badge">{selectedIds.size} selected</span>
            {/if}
            {#if anySoloed}
                <button class="toolbar-ms-btn solo-clear" onclick={clearSolos}>
                    Clear Solos
                </button>
            {/if}
        </div>
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
                class="toolbar-btn save-btn"
                onclick={doSaveConfig}
                disabled={!dirty || isDetachedHead}
                title={isDetachedHead
                    ? "Cannot save while viewing a historical version"
                    : "Save config (creates a new version)"}
            >
                💾 Save
            </button>
        </div>
    </div>

    {#if strips.length === 0}
        <div class="empty-state">
            <p>
                No channel strips. Use <strong>View → Show Setup Window</strong> to set
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
                            {#each group.instrGroups as instrGroup (instrGroup.key)}
                                {@const instInput = availableInputs.find(
                                    (i) => i.port === instrGroup.strips[0]?.inputPort
                                        && i.channel === instrGroup.strips[0]?.inputChannel
                                )}
                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                <!-- svelte-ignore a11y_no_static_element_interactions -->
                                <div
                                    class="inst-group"
                                    style="--accent: {group.colors.accent};"
                                    onclick={(e) => e.stopPropagation()}
                                >
                                    <!-- Bridge header: same height for every group (1 or N strips) -->
                                    <div
                                        class="bridge-header"
                                        class:bridge-multi={instrGroup.strips.length > 1}
                                    >
                                        <span class="bridge-icon">{instInput?.isSolo ? "👤" : "👥"}</span>
                                        <span class="bridge-label">{instrGroup.label}</span>
                                        {#if instrGroup.portLabel}
                                            <span class="bridge-port">{instrGroup.portLabel}</span>
                                        {/if}
                                    </div>
                                    <!-- inst-body: master-strip + strips-row side by side -->
                                    <div class="inst-body">
                                    <!-- Master strip: only for multi-strip groups -->
                                    {#if instrGroup.strips.length > 1}
                                        {@const gm = getGroupMaster(instrGroup.key)}
                                         {@const masterDb = getGroupSumDb(instrGroup)}
                                        <!-- svelte-ignore a11y_click_events_have_key_events -->
                                        <!-- svelte-ignore a11y_no_static_element_interactions -->
                                        <div class="master-strip" onclick={(e) => e.stopPropagation()}>
                                            <!-- Top spacer: matches .select-bar height so fader starts at same Y -->
                                            <div class="master-strip-top-spacer"></div>
                                            <!-- Fader section: flex:1 so it takes same span as .ch-fader -->
                                            <div class="ch-fader" style="flex:1;min-height:0">
                                                <span class="fader-tick fader-top">+6</span>
                                                <div class="fader-meter-row">
                                                    <div class="fader-track master-track">
                                                        <input
                                                            class="fader-slider master-slider"
                                                            type="range"
                                                            min="0"
                                                            max="1000"
                                                            step="1"
                                                            value={Math.round(dbToPos(masterDb) * 1000)}
                                                            oninput={(e) => handleMasterFaderInput(
                                                                instrGroup,
                                                                parseFloat(/** @type {HTMLInputElement} */ (e.target).value) / 1000,
                                                            )}
                                                            ondblclick={() => handleMasterFaderReset(instrGroup)}
                                                        />
                                                    </div>
                                                </div>
                                                <span class="fader-tick fader-bot">-∞</span>
                                                <div class="master-db">{formatDb(getGroupSumDb(instrGroup))}</div>
                                            </div>
                                            <!-- Bottom spacer: matches mute-solo + xmap + plugin rows -->
                                            <div class="master-strip-bot-spacer">
                                                <button
                                                    class="sum-lock-btn"
                                                    class:sum-lock-active={gm.lockSum}
                                                    title={gm.lockSum
                                                        ? "Sum Lock ON — individual faders keep sum constant"
                                                        : "Sum Lock OFF — master tracks sum of all faders"}
                                                    onclick={() => (gm.lockSum = !gm.lockSum)}
                                                >{gm.lockSum ? '🔒' : '🔓'}</button>
                                            </div>
                                        </div>
                                    {/if}
                                    <div class="strips-row">
                                        {#each instrGroup.strips as strip (strip.id)}
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
                                                    style="background: {group.colors.accent};"
                                                    onclick={(e) => {
                                                        e.stopPropagation();
                                                        handleStripClick(strip.id, e);
                                                    }}
                                                ></div>

                                                <!-- Vertical fader -->
                                                <div class="ch-fader">
                                                    <span class="fader-tick fader-top">+6</span>
                                                    <div class="fader-meter-row">
                                                        <div class="fader-track">
                                                            <input
                                                                class="fader-slider"
                                                                type="range"
                                                                min="0"
                                                                max="1000"
                                                                step="1"
                                                                value={Math.round(
                                                                    dbToPos(strip.gainDb ?? 0) * 1000,
                                                                )}
                                                                oninput={(e) => {
                                                                    const pos =
                                                                        parseFloat(
                                                                            /** @type {HTMLInputElement} */ (
                                                                                e.target
                                                                            ).value,
                                                                        ) / 1000;
                                                                    handleGroupFaderInput(instrGroup, strip, pos);
                                                                }}
                                                                ondblclick={() =>
                                                                    handleGainValueInput(strip, "0")}
                                                            />
                                                        </div>
                                                        <div class="meter-track">
                                                            <div
                                                                class="meter-fill"
                                                                class:meter-hot={strip.peakDb > 0}
                                                                style="height: {dbToPos(strip.peakDb ?? -120) * 100}%"
                                                            ></div>
                                                            <div
                                                                class="meter-hold"
                                                                class:meter-hot={strip.peakHoldDb > 0}
                                                                style="bottom: {dbToPos(strip.peakHoldDb ?? -120) * 100}%"
                                                            ></div>
                                                        </div>
                                                    </div>
                                                    <span class="fader-tick fader-bot">-∞</span>
                                                    <input
                                                        class="fader-value"
                                                        type="number"
                                                        min="-120"
                                                        max="6"
                                                        step="0.1"
                                                        value={strip.gainDb != null
                                                            ? Math.round(strip.gainDb * 10) / 10
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
                                                        title={strip.muted ? "Unmute" : "Mute"}
                                                        onclick={() => toggleMute(strip.id)}
                                                        >M</button
                                                    >
                                                    <button
                                                        class="ms-btn solo-btn"
                                                        class:active={strip.soloed}
                                                        title={strip.soloed ? "Unsolo" : "Solo"}
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
                                                                loadExpressionMapFromFile(strip.id);
                                                                /** @type {HTMLSelectElement} */ (
                                                                    e.target
                                                                ).value = strip.expressionMapName
                                                                    ? "__loaded__"
                                                                    : "";
                                                            } else if (val === "") {
                                                                clearExpressionMap(strip.id);
                                                            } else if (val !== "__loaded__") {
                                                                loadExpressionMap(strip.id, val);
                                                            }
                                                        }}
                                                    >
                                                        <option value="">— xmap —</option>
                                                        {#if strip.expressionMapName}
                                                            <option value="__loaded__" selected
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
                                                                if (uid) setPlugin(strip.id, uid);
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
                                                                onclick={() => showEditor(strip.id)}
                                                                title="Open editor">e</button
                                                            >
                                                        {/if}
                                                    </div>
                                                </div>

                                                <!-- Strip name area: only library name now (instrument moved to bridge header) -->
                                                <div class="ch-name-area">
                                                    {#if editingId === strip.id}
                                                        <input
                                                            class="ch-name-input"
                                                            type="text"
                                                            placeholder="Library"
                                                            bind:value={editValue}
                                                            onblur={() => commitEdit(strip.id)}
                                                            onkeydown={(e) =>
                                                                handleNameKeydown(e, strip.id)}
                                                        />
                                                    {:else}
                                                        <!-- svelte-ignore a11y_no_static_element_interactions -->
                                                        <div
                                                            class="ch-lib-name"
                                                            ondblclick={() => startEditing(strip)}
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
                                                        onclick={() => duplicateStrip(strip.id)}
                                                        title="Add parallel strip with same input"
                                                        >+</button
                                                    >
                                                    <button
                                                        class="ch-btn ch-btn-del"
                                                        onclick={() => removeStrip(strip.id)}
                                                        title="Delete strip">✕</button
                                                    >
                                                </div>
                                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                                <!-- svelte-ignore a11y_no_static_element_interactions -->
                                                <div
                                                    class="select-bar select-bar-bottom"
                                                    style="background: {group.colors.accent};"
                                                    onclick={(e) => {
                                                        e.stopPropagation();
                                                        handleStripClick(strip.id, e);
                                                    }}
                                                ></div>
                                            </div>
                                        {/each}
                                    </div> <!-- /strips-row -->
                                    </div> <!-- /inst-body -->
                                </div> <!-- /inst-group -->
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
        color: #ddd;
        cursor: pointer;
        transition: all 0.12s ease;
    }

    .toolbar-ms-btn:hover {
        border-color: #aaa;
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

    .delay-control {
        display: flex;
        align-items: center;
        gap: 4px;
    }
    .delay-label {
        font-size: 0.7rem;
        color: #cbd5e1;
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
        color: #cbd5e1;
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
        color: #cbd5e1;
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
        align-items: stretch;
        gap: 3px;
        background: #070d18;
    }

    /* Instrument group (bridge-header + inst-body) */
    .inst-group {
        display: flex;
        flex-direction: column;
        flex: 1;
        min-height: 0;
        border-left: 2px solid var(--accent, #334155);
        margin-left: -1px; /* center the 2px divider in the 1px+2px gap between groups */
    }
    /* Row below the bridge-header: master-strip (if multi) + strips-row side by side */
    .inst-body {
        display: flex;
        flex-direction: row;
        flex: 1;
        min-height: 0;
        padding-bottom: 12px; /* lift channel-strips above overlay scrollbar so select-bar-bottom is visible */
    }
    /* Row of parallel strips within one instrument group */
    .strips-row {
        display: flex;
        flex-direction: row;
        justify-content: center; /* center strips when header is wider than strip(s) */
        flex: 1;
        min-height: 0;
    }

    /* ── Channel strip: full-height vertical column ── */
    .channel-strip {
        position: relative;
        display: flex;
        flex-direction: column;
        width: 80px;
        min-width: 80px;
        align-self: stretch;
        background: #111827;
        border-right: 1px solid #0f172a;
        padding: 6px 4px 12px;
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

    /* ── Master strip ────────────────────────────────── */
    .master-strip {
        display: flex;
        flex-direction: column;
        align-items: center;
        gap: 4px;
        width: 38px;
        min-width: 38px;
        height: 100%;
        flex-shrink: 0;
        background: #090f1c;
        border-right: 1px solid rgba(51, 65, 85, 0.9); /* softer than other separators */
        /* Same vertical padding as .channel-strip so spacers compute correctly */
        padding: 6px 2px;
        overflow: hidden;
    }
    /* Mirrors .select-bar height so the fader starts at the same Y as individual faders */
    .master-strip-top-spacer {
        flex-shrink: 0;
        height: 6px; /* same as .select-bar */
    }
    /* Mirrors bottom controls (ch-mute-solo + ch-xmap + ch-plugin + measured correction) */
    .master-strip-bot-spacer {
        flex-shrink: 0;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: flex-start;
        height: 128px;
        gap: 4px;
    }

    .master-track {
        width: 10px !important;
    }
    input.fader-slider.master-slider {
        accent-color: #d4a017;
    }
    .master-db {
        font-size: 0.62rem;
        font-weight: 600;
        color: #cbd5e1;
        text-align: center;
        flex-shrink: 0;
        letter-spacing: 0.02em;
    }
    .sum-lock-btn {
        background: none;
        border: 1px solid #334155;
        border-radius: 4px;
        cursor: pointer;
        font-size: 0.65rem;
        padding: 2px 3px;
        color: #94a3b8;
        transition: border-color 0.15s, color 0.15s, background 0.15s;
        flex-shrink: 0;
        line-height: 1;
    }
    .sum-lock-btn:hover {
        border-color: var(--accent, #94a3b8);
        color: var(--accent, #94a3b8);
    }
    .sum-lock-btn.sum-lock-active {
        border-color: var(--accent, #94a3b8);
        color: #fff;
        background: color-mix(in srgb, var(--accent, #94a3b8) 50%, transparent);
    }

    .select-bar {
        flex-shrink: 0;
        height: 6px;
        cursor: pointer;
        transition: filter 0.15s;
    }
    .select-bar-bottom {
        position: absolute;
        bottom: 0;
        left: 4px;  /* match channel-strip horizontal padding so it aligns with select-bar-top */
        right: 4px;
        height: 6px;
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
    /* Instrument group bridge header */
    .inst-group {
        display: flex;
        flex-direction: column;
    }
    .bridge-header {
        display: flex;
        align-items: center;
        gap: 3px;
        padding: 2px 4px;
        border-bottom: 2px solid var(--accent, #3b82f6);
        background: rgba(0,0,0,0.25);
        min-height: 18px;
        overflow: hidden;
    }
    .bridge-header.bridge-multi {
        background: rgba(59,130,246,0.08);
    }
    .bridge-icon {
        font-size: 0.55rem;
        flex-shrink: 0;
        opacity: 0.7;
    }
    .bridge-label {
        font-size: 0.6rem;
        font-weight: 600;
        color: #e2e8f0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        flex: 1;
    }
    .bridge-port {
        font-size: 0.5rem;
        color: #94a3b8;
        flex-shrink: 0;
    }
    .ch-lib-name {
        font-size: 0.55rem;
        color: #cbd5e1;
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
        min-height: 0;
        gap: 2px;
        padding: 2px 0;
    }

    .fader-tick {
        font-size: 0.5rem;
        color: #cbd5e1;
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
        color: #cbd5e1;
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
        color: #aaa;
        font-size: 0.6rem;
        font-weight: 700;
        cursor: pointer;
        padding: 0;
        line-height: 1;
        transition: all 0.12s ease;
    }

    .ms-btn:hover {
        border-color: #aaa;
        color: #ddd;
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
        color: #cbd5e1;
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
