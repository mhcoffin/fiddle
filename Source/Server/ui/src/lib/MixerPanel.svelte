<script>
    import { onMount, onDestroy, untrack } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import BranchSelector from "./BranchSelector.svelte";
    import NoteInspector from "./NoteInspector.svelte";
    import {
        addStripRange,
        flattenVisualStripOrder,
    } from "./mixerSelection.js";
    import {
        FAMILY_ORDER,
        canonicalFamily,
        instrumentScoreOrder,
        populateScoreOrder,
    } from "./orchestralOrder.js";
    import {
        STRIP_SIZE_PRESETS,
        readStripSize,
        writeStripSize,
    } from "./uiPreferences.js";

    let {
        uiZoom = 1,
        onZoomIn = () => {},
        onZoomOut = () => {},
        onResetZoom = () => {},
    } = $props();

    let strips = $state([]);
    let availableInputs = $state([]);
    let scannedPlugins = $state([]);
    let availableXmaps = $state([]);
    let luaPluginCatalog = $state([]);
    let branches = $state([]);
    let currentBranch = $state("default");
    let stripSize = $state(readStripSize(window.localStorage));
    let stripWidth = $derived(STRIP_SIZE_PRESETS[stripSize].width);

    const updateStripSize = (value) => {
        stripSize = writeStripSize(window.localStorage, value);
    };

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
    onFromCpp("setLuaPluginCatalog", (data) => {
        try {
            luaPluginCatalog = Array.isArray(data) ? data : [];
            console.log(`[Mixer] Lua plugin catalog: ${luaPluginCatalog.length} plugins`);
        } catch (e) {
            console.error("[Mixer] luaCatalog error:", e);
        }
    });
    // Populate orchestral score order when Dorico instruments arrive
    onFromCpp("setDoricoInstruments", (arr) => {
        populateScoreOrder(arr);
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
    let inspectorStripId = $state(null);
    let inspectorWidth = $state(320);
    let isResizing = $state(false);

    const toggleInspector = (stripId) => {
        inspectorStripId = inspectorStripId === stripId ? null : stripId;
    };

    // ── Resize drag logic ─────────────────────────────────
    let resizeStartX = 0;
    let resizeStartW = 0;

    const onResizeMouseDown = (e) => {
        e.preventDefault();
        isResizing = true;
        resizeStartX = e.clientX;
        resizeStartW = inspectorWidth;
        window.addEventListener('mousemove', onResizeMouseMove);
        window.addEventListener('mouseup', onResizeMouseUp);
    };

    const onResizeMouseMove = (e) => {
        const delta = resizeStartX - e.clientX; // dragging left = wider
        inspectorWidth = Math.max(200, Math.min(800, resizeStartW + delta));
    };

    const onResizeMouseUp = () => {
        isResizing = false;
        window.removeEventListener('mousemove', onResizeMouseMove);
        window.removeEventListener('mouseup', onResizeMouseUp);
    };
    let isMultiSelected = $derived(selectedIds.size > 1);

    const handleStripClick = (stripId, e) => {
        if (e.metaKey && e.shiftKey) {
            // Cmd+Shift+Click: select all
            selectedIds = new Set(flatStripOrder);
        } else if (e.shiftKey && lastClickedId) {
            // Shift+Click: range select
            selectedIds = addStripRange(
                selectedIds,
                flatStripOrder,
                lastClickedId,
                stripId,
            );
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
    const getPluginName = (strip) =>
        scannedPlugins.find(
            (plugin) => Number(plugin.uid) === Number(strip.pluginUid),
        )?.name || "";
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

    // ── Lua Plugin Management ────────────────────────────────
    const addLuaPlugin = (stripId, filePath) => {
        dispatchCpp("addStripLuaPlugin", stripId, filePath);
    };
    const removeLuaPlugin = (stripId, index) => {
        dispatchCpp("removeStripLuaPlugin", stripId, index);
    };

    // Shadow map: last gain value we sent for each strip (plain object, not reactive).
    // Used so that rapid master-fader events read our own last write, not the async
    // IPC-reflected strip.gainDb (which may still show the old value).
    const gainShadow = {};

    // Unconstrained internal shadow used for all group-master delta math.
    // Allows lock-on redistribution to accumulate values outside [-120,+6] so
    // the theoretical sum stays exact even when displayed faders are clamped.
    const gainShadowRaw = {};

    // ── Fader display smoothing ─────────────────────────────
    // Display positions ease toward target values via requestAnimationFrame.
    // Key = stripId or "master:<groupKey>". Value = current display pos (0-1000).
    // When a fader is being user-dragged, smoothing is bypassed.
    const EASE_MS = 120;
    let faderDisplayPos = $state(/** @type {Record<string, number>} */ ({}));
    /** Target positions that the display is easing toward */
    const faderTargetPos = {};
    /** Start time/value for active ease animations */
    const faderEaseState = {};
    /** Set of fader keys currently being user-dragged (instant, no easing) */
    const faderDragging = new Set();

    // ── Shift-drag fine control ──────────────────────────────
    // When Shift is held during a fader drag, movement is reduced by FINE_RATIO.
    // faderAnchor stores the position at drag-start so we can compute deltas.
    const FINE_RATIO = 6;
    /** @type {Record<string, number>} pos (0-1000) at drag-start */
    const faderAnchor = {};

    /** Compute effective fader position, applying fine-mode when shift is held */
    const fineFaderPos = (key, rawPos, shiftKey) => {
        const anchor = faderAnchor[key];
        if (!shiftKey || anchor === undefined) {
            faderAnchor[key] = rawPos; // update anchor when not in fine mode
            return rawPos;
        }
        const delta = (rawPos - anchor) / FINE_RATIO;
        return Math.max(0, Math.min(1000, Math.round(anchor + delta)));
    };
    let animFrameId = 0;

    /** Get the display position for a fader. Returns the smoothed value ONLY
     *  while an ease animation is actively running; otherwise returns computedPos. */
    const getFaderPos = (key, computedPos) => {
        if (faderDragging.has(key)) return computedPos;
        // Only use cached display position during active animation
        if (faderEaseState[key]) {
            const dp = faderDisplayPos[key];
            return dp !== undefined ? dp : computedPos;
        }
        return computedPos;
    };

    /** Request easing a fader to a new target position */
    const easeFaderTo = (key, targetPos) => {
        const current = faderDisplayPos[key] ?? targetPos;
        if (Math.abs(current - targetPos) < 1) {
            // Already at target — snap
            faderDisplayPos[key] = targetPos;
            return;
        }
        faderTargetPos[key] = targetPos;
        faderEaseState[key] = { from: current, start: performance.now() };
        startEaseLoop();
    };

    /** Set display position instantly (for user-dragged faders) */
    const snapFaderTo = (key, pos) => {
        faderDisplayPos[key] = pos;
        delete faderTargetPos[key];
        delete faderEaseState[key];
    };

    function startEaseLoop() {
        if (animFrameId) return;
        animFrameId = requestAnimationFrame(tickEase);
    }

    function tickEase(now) {
        animFrameId = 0;
        let still = true;
        for (const key of Object.keys(faderEaseState)) {
            const es = faderEaseState[key];
            const target = faderTargetPos[key];
            if (target === undefined || !es) { delete faderEaseState[key]; continue; }
            const elapsed = now - es.start;
            const t = Math.min(1, elapsed / EASE_MS);
            // ease-out cubic
            const eased = 1 - Math.pow(1 - t, 3);
            const pos = es.from + (target - es.from) * eased;
            faderDisplayPos[key] = Math.round(pos);
            if (t >= 1) {
                faderDisplayPos[key] = Math.round(target);
                delete faderEaseState[key];
                delete faderTargetPos[key];
            } else {
                still = false;
            }
        }
        if (!still) {
            animFrameId = requestAnimationFrame(tickEase);
        }
    }

    /** Ease all faders in a group (strips + master) to new computed positions.
     *  Call AFTER setting gains/mute so computed positions are correct. */
    const easeGroupFaders = (instrGroup) => {
        for (const s of instrGroup.strips) {
            const db = gainShadow[s.id] ?? gainShadowRaw[s.id] ?? s.gainDb ?? 0;
            easeFaderTo(s.id, Math.round(dbToPos(db) * 1000));
        }
        const masterKey = `master:${instrGroup.key}`;
        const masterDb = getGroupDb(instrGroup);
        easeFaderTo(masterKey, Math.round(dbToPos(masterDb) * 1000));
    };

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

    // ── Library Activation ────────────────────────────────────────
    let uniqueLibraries = $derived.by(() => {
        const libMap = new Map();
        for (const strip of strips) {
            const name = strip.library || "";
            if (!name) continue;
            const cur = libMap.get(name);
            if (!cur) {
                libMap.set(name, { name, active: strip.active !== false });
            } else {
                // Library is active only if ALL its strips are active
                if (strip.active === false) cur.active = false;
            }
        }
        return [...libMap.values()];
    });

    const toggleLibActive = (libraryName) => {
        dispatchCpp("toggleLibraryActive", libraryName);
    };

    // ── Mute / Solo ──────────────────────────────────────────────
    let anySoloed = $derived(strips.some((s) => s.soloed));

    const isAudible = (strip) => strip.active !== false && !strip.muted && (!anySoloed || strip.soloed);

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

    /**
     * Group-aware mute toggle. In locked mode, redistributes power to siblings
     * so the group power stays constant (gain.md Examples 4, lines 66-68).
     * Muting  = treat as moving fader to -∞ (power → 0), redistribute to others.
     * Unmuting = treat as restoring remembered power, others scale down.
     */
    const toggleMuteInGroup = (instrGroup, stripId) => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const willMute = !strip.muted;

        // Multi-select: delegate to simple toggleMute (no per-group compensation)
        if (isMultiSelected && selectedIds.has(stripId)) {
            toggleMute(stripId);
            return;
        }

        const gm = getGroupMaster(instrGroup.key);

        if (gm.lockSum && instrGroup.strips.length > 1) {
            // Compute old group power (before mute change)
            const oldGroupPower = getGroupPower(instrGroup);

            if (willMute) {
                // Strip is being muted: its power will become 0.
                // Target: other strips absorb the difference.
                const stripPower = powerFromDb(gainShadowRaw[strip.id] ?? strip.gainDb ?? 0);
                const otherPowerBefore = oldGroupPower - stripPower;

                if (otherPowerBefore > 0 && oldGroupPower > 0) {
                    // Scale others so their total = oldGroupPower
                    const scale = oldGroupPower / otherPowerBefore;
                    for (const s of instrGroup.strips) {
                        if (s.id !== stripId && !s.muted) {
                            const db = gainShadowRaw[s.id] ?? s.gainDb ?? 0;
                            const p = powerFromDb(db) * scale;
                            setGainRaw(s.id, dbFromGain(Math.sqrt(p)));
                        }
                    }
                }
            } else {
                // Strip is being unmuted: its remembered power is restored.
                // Other strips must scale down to keep group power = oldGroupPower.
                const restoredPower = powerFromDb(gainShadowRaw[strip.id] ?? strip.gainDb ?? 0);
                const otherPowerBefore = oldGroupPower; // all current power is from others
                const newTotal = otherPowerBefore + restoredPower;

                if (newTotal > 0 && otherPowerBefore > 0) {
                    const requiredOtherPower = Math.max(0, oldGroupPower - restoredPower);
                    const scale = requiredOtherPower / otherPowerBefore;
                    for (const s of instrGroup.strips) {
                        if (s.id !== stripId && !s.muted) {
                            const db = gainShadowRaw[s.id] ?? s.gainDb ?? 0;
                            const p = powerFromDb(db) * scale;
                            setGainRaw(s.id, dbFromGain(Math.sqrt(p)));
                        }
                    }
                }
            }
        }

        // Optimistic mute: set locally BEFORE dispatching so the next render
        // (triggered by sibling gain echoes) already sees the right muted state.
        // This prevents the intermediate "bounce" where gains are updated but
        // mute hasn't arrived yet.
        strip.muted = willMute;

        // Ease all faders in this group to their final positions
        easeGroupFaders(instrGroup);

        // Actually mute/unmute the strip
        dispatchCpp("setStripMute", stripId, willMute);
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
        for (const s of strips) {
            if (s.soloed) dispatchCpp("setStripSolo", s.id, false);
        }
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

    // ── Power-model gain helpers ──────────────────────────────
    const MINUS_INF_DB = -120;

    /** dB → linear gain. At or below MINUS_INF_DB → 0. */
    const gainFromDb = (db) => {
        if (db <= MINUS_INF_DB) return 0;
        return Math.pow(10, db / 20);
    };

    /** Linear gain → dB. gain ≤ 0 → MINUS_INF_DB. */
    const dbFromGain = (gain) => {
        if (gain <= 0) return MINUS_INF_DB;
        return 20 * Math.log10(gain);
    };

    /** dB → power (gain²). */
    const powerFromDb = (db) => {
        const g = gainFromDb(db);
        return g * g;
    };

    // ── Group Master Fader state ──────────────────────────────
    // Only lockSum is stored in state. masterDb is always DERIVED from the
    // power model: groupDb = 20·log10(√(Σ power_i)).
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

    /** Compute group power: sum of power of all audible strips.
     *  A strip is audible if it is not muted AND (no solo active OR strip is soloed).
     *  Uses gainShadowRaw so the value is accurate even if some faders are clamped. */
    const getGroupPower = (instrGroup) =>
        instrGroup.strips.reduce((acc, s) => {
            if (!isAudible(s)) return acc;
            const db = gainShadowRaw[s.id] ?? s.gainDb ?? 0;
            return acc + powerFromDb(db);
        }, 0);

    /** Compute the group fader dB using the power model:
     *  groupDb = dbFromGain(√(Σ power_i)) */
    const getGroupDb = (instrGroup) => {
        const gp = getGroupPower(instrGroup);
        return dbFromGain(Math.sqrt(gp));
    };

    /**
     * Master fader drag → scale all unmuted strip powers proportionally.
     * newGroupPower / oldGroupPower gives the scale factor for each strip's power.
     */
    const handleMasterFaderInput = (instrGroup, pos) => {
        const newGroupDb = Math.round(posToDB(pos) * 10) / 10;
        const newGroupGain = gainFromDb(newGroupDb);
        const newGroupPower = newGroupGain * newGroupGain;
        const oldGroupPower = getGroupPower(instrGroup);

        if (oldGroupPower <= 0) {
            // All unmuted strips are silent — can't scale. Set all to equal share.
            const unmuted = instrGroup.strips.filter((s) => !s.muted);
            if (unmuted.length === 0) return;
            const perStripPower = newGroupPower / unmuted.length;
            const perStripDb = dbFromGain(Math.sqrt(perStripPower));
            for (const s of unmuted) {
                setGainRaw(s.id, perStripDb);
            }
            return;
        }

        const scale = newGroupPower / oldGroupPower;
        for (const strip of instrGroup.strips) {
            if (strip.muted) continue;
            const db = gainShadowRaw[strip.id] ?? strip.gainDb ?? 0;
            const p = powerFromDb(db) * scale;
            const g = Math.sqrt(p);
            setGainRaw(strip.id, dbFromGain(g));
        }
    };

    /** Double-click master fader → same effect as sliding to 0 dB */
    const handleMasterFaderReset = (instrGroup) => {
        handleMasterFaderInput(instrGroup, dbToPos(0));
    };

    /**
     * Individual strip fader drag within a group.
     * Lock OFF: only the dragged strip changes; master auto-follows (power model).
     * Lock ON:  master is "locked" — redistribute power proportionally across
     *           other unmuted siblings to keep group power constant.
     */
    const handleGroupFaderInput = (instrGroup, strip, pos) => {
        const gm = getGroupMaster(instrGroup.key);
        const newDb = Math.round(posToDB(pos) * 10) / 10;
        if (gm.lockSum && instrGroup.strips.length > 1) {
            const oldGroupPower = getGroupPower(instrGroup);
            const newStripPower = strip.muted ? 0 : powerFromDb(newDb);
            const requiredOtherPower = Math.max(0, oldGroupPower - newStripPower);

            // Current total power of other unmuted strips
            let currentOtherPower = 0;
            for (const s of instrGroup.strips) {
                if (s.id !== strip.id && !s.muted) {
                    currentOtherPower += powerFromDb(gainShadowRaw[s.id] ?? s.gainDb ?? 0);
                }
            }

            // Redistribute required power across other unmuted strips
            const others = instrGroup.strips.filter((s) => s.id !== strip.id && !s.muted);
            if (others.length > 0) {
                if (currentOtherPower > 0) {
                    // Scale proportionally when others have non-zero power
                    const scale = requiredOtherPower / currentOtherPower;
                    for (const other of others) {
                        const base = gainShadowRaw[other.id] ?? other.gainDb ?? 0;
                        const p = powerFromDb(base) * scale;
                        const g = Math.sqrt(p);
                        setGainRaw(other.id, dbFromGain(g));
                    }
                } else {
                    // Others are all at -∞ — distribute equally
                    const perStripPower = requiredOtherPower / others.length;
                    const perStripDb = dbFromGain(Math.sqrt(perStripPower));
                    for (const other of others) {
                        setGainRaw(other.id, perStripDb);
                    }
                }
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
                // Look up instrument entityID from availableInputs
                const inputA =
                    availableInputs.find(
                        (i) =>
                            i.port === a.inputPort &&
                            i.channel === a.inputChannel,
                    );
                const inputB =
                    availableInputs.find(
                        (i) =>
                            i.port === b.inputPort &&
                            i.channel === b.inputChannel,
                    );
                return instrumentScoreOrder(inputA?.entityID) - instrumentScoreOrder(inputB?.entityID);
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
    let flatStripOrder = $derived(flattenVisualStripOrder(groupedStrips));

</script>

<div
    class="mixer-container"
    style="--strip-width: {stripWidth}px;"
>
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

        </div>
        <div class="toolbar-center">
            {#if selectedIds.size > 1}
                <span class="selection-badge">{selectedIds.size} selected</span>
            {/if}
            <button
                class="toolbar-ms-btn solo-clear"
                style:visibility={anySoloed ? 'visible' : 'hidden'}
                onclick={clearSolos}
            >
                Clear Solos
            </button>
        </div>
        <div class="toolbar-right">
            <div class="display-controls" aria-label="Display settings">
                <label class="display-control-label" for="strip-size-select">Strip size</label>
                <select
                    id="strip-size-select"
                    class="toolbar-select"
                    value={stripSize}
                    onchange={(e) => updateStripSize(e.currentTarget.value)}
                    title="Choose the width of mixer strips"
                >
                    {#each Object.entries(STRIP_SIZE_PRESETS) as [value, preset]}
                        <option {value}>{preset.label}</option>
                    {/each}
                </select>
            </div>
            <div class="zoom-control" aria-label="Interface zoom">
                <button
                    class="zoom-btn"
                    onclick={onZoomOut}
                    aria-label="Zoom out"
                    title="Zoom out (Command-minus)"
                >−</button>
                <button
                    class="zoom-value"
                    onclick={onResetZoom}
                    aria-label="Reset zoom"
                    title="Reset zoom (Command-0)"
                >{Math.round(uiZoom * 100)}%</button>
                <button
                    class="zoom-btn"
                    onclick={onZoomIn}
                    aria-label="Zoom in"
                    title="Zoom in (Command-plus)"
                >+</button>
            </div>
            <div class="delay-control">
                <label class="delay-label" for="delay-slider">Delay</label>
                <input
                    id="delay-slider"
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
                        role="button"
                        tabindex="0"
                        ondblclick={() => {
                            editingDelay = true;
                        }}
                        onkeydown={(e) => {
                            if (e.key === "Enter" || e.key === " ") {
                                e.preventDefault();
                                editingDelay = true;
                            }
                        }}
                        title="Double-click or press Enter to type">{playbackDelay}ms</span
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

    {#if uniqueLibraries.length > 0}
        <div class="library-chip-bar">
            {#each uniqueLibraries as lib (lib.name)}
                <button
                    class="library-chip"
                    class:library-chip-active={lib.active}
                    class:library-chip-inactive={!lib.active}
                    onclick={() => toggleLibActive(lib.name)}
                    title={lib.active ? `Deactivate ${lib.name}` : `Activate ${lib.name}`}
                >
                    <span class="chip-icon">{lib.active ? "✓" : "○"}</span>
                    <span class="chip-label">{lib.name}</span>
                </button>
            {/each}
        </div>
    {/if}

    <div class="mixer-body" class:resizing={isResizing}>
    {#if strips.length === 0}
        <div class="empty-state">
            <p>
                No channel strips. Use <strong>View → Show Library Manager</strong> to set
                up your ensemble.
            </p>
        </div>
    {:else}
        <div class="mixer-strips-area">
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
                                         {@const masterDb = getGroupDb(instrGroup)}
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
                                                            value={getFaderPos(`master:${instrGroup.key}`, Math.round(dbToPos(masterDb) * 1000))}
                                                            onpointerdown={() => {
                                                                const mk = `master:${instrGroup.key}`;
                                                                const cur = getFaderPos(mk, Math.round(dbToPos(masterDb) * 1000));
                                                                faderAnchor[mk] = cur;
                                                            }}
                                                            oninput={(e) => {
                                                                const mk = `master:${instrGroup.key}`;
                                                                faderDragging.add(mk);
                                                                const rawPos = parseFloat(/** @type {HTMLInputElement} */ (e.target).value);
                                                                const effectivePos = fineFaderPos(mk, rawPos, e.shiftKey);
                                                                snapFaderTo(mk, effectivePos);
                                                                handleMasterFaderInput(instrGroup, effectivePos / 1000);
                                                            }}
                                                            onpointerup={() => { faderDragging.delete(`master:${instrGroup.key}`); delete faderAnchor[`master:${instrGroup.key}`]; }}
                                                            onpointerleave={() => { faderDragging.delete(`master:${instrGroup.key}`); delete faderAnchor[`master:${instrGroup.key}`]; }}
                                                            ondblclick={() => handleMasterFaderReset(instrGroup)}
                                                        />
                                                    </div>
                                                </div>
                                                <span class="fader-tick fader-bot">-∞</span>
                                                <div class="master-db">{formatDb(getGroupDb(instrGroup))}</div>
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
                                                class:inactive={strip.active === false}
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

                                                <div class="ch-inspect-midi-slot">
                                                    <button
                                                        class="ch-inspect-midi-btn"
                                                        class:active={inspectorStripId === strip.id}
                                                        title="Open the incoming MIDI note inspector"
                                                        onclick={() => toggleInspector(strip.id)}
                                                    >Inspect MIDI</button>
                                                </div>

                                                <!-- Lua Plugins (top of strip = first in signal chain) -->
                                                <div class="ch-lua">
                                                    {#if strip.luaPlugins && strip.luaPlugins.length > 0}
                                                        <div class="ch-lua-chips">
                                                            {#each strip.luaPlugins as lp}
                                                                <span class="ch-lua-chip" class:error={!lp.loaded} title={lp.filePath}>
                                                                    <span class="ch-lua-chip-name">{lp.name || 'Unknown'}</span>
                                                                    <button
                                                                        class="ch-lua-chip-x"
                                                                        title="Remove"
                                                                        onclick={() => removeLuaPlugin(strip.id, lp.index)}
                                                                    >×</button>
                                                                </span>
                                                            {/each}
                                                        </div>
                                                    {/if}
                                                    {#if luaPluginCatalog.length > 0}
                                                        <select
                                                            class="ch-select ch-lua-select"
                                                            value=""
                                                            onchange={(e) => {
                                                                const val = /** @type {HTMLSelectElement} */ (e.target).value;
                                                                if (val) {
                                                                    addLuaPlugin(strip.id, val);
                                                                    /** @type {HTMLSelectElement} */ (e.target).value = "";
                                                                }
                                                            }}
                                                        >
                                                            <option value="">+ lua plugin</option>
                                                            {#each luaPluginCatalog as lp}
                                                                <option value={lp.filePath}>{lp.name}</option>
                                                            {/each}
                                                        </select>
                                                    {/if}
                                                </div>

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
                                                                value={getFaderPos(strip.id, Math.round(
                                                                    dbToPos(strip.gainDb ?? 0) * 1000,
                                                                ))}
                                                                onpointerdown={() => {
                                                                    const cur = getFaderPos(strip.id, Math.round(dbToPos(strip.gainDb ?? 0) * 1000));
                                                                    faderAnchor[strip.id] = cur;
                                                                }}
                                                                oninput={(e) => {
                                                                    faderDragging.add(strip.id);
                                                                    const rawPos = parseFloat(/** @type {HTMLInputElement} */ (e.target).value);
                                                                    const effectivePos = fineFaderPos(strip.id, rawPos, e.shiftKey);
                                                                    snapFaderTo(strip.id, effectivePos);
                                                                    handleGroupFaderInput(instrGroup, strip, effectivePos / 1000);
                                                                }}
                                                                onpointerup={() => { faderDragging.delete(strip.id); delete faderAnchor[strip.id]; }}
                                                                onpointerleave={() => { faderDragging.delete(strip.id); delete faderAnchor[strip.id]; }}
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
                                                        onclick={() => toggleMuteInGroup(instrGroup, strip.id)}
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
                                                    <div
                                                        class="ch-xmap-picker"
                                                        title={strip.expressionMapName ||
                                                            "Choose an expression map"}
                                                    >
                                                        <span
                                                            class="ch-xmap-value"
                                                            class:ch-xmap-placeholder={!strip.expressionMapName}
                                                            aria-hidden="true"
                                                        ><span>{strip.expressionMapName || "— xmap —"}</span></span>
                                                        <span class="ch-xmap-arrow" aria-hidden="true"></span>
                                                        <select
                                                            class="ch-select ch-xmap-select"
                                                            aria-label="Expression map"
                                                            title={strip.expressionMapName || "Choose an expression map"}
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
                                                </div>


                                                <!-- Plugin -->
                                                <div class="ch-plugin">
                                                    <div class="ch-control-heading">
                                                        <span>VSTi</span>
                                                        {#if strip.hasPlugin}
                                                            <button
                                                                class="ch-action-btn"
                                                                onclick={() => showEditor(strip.id)}
                                                                title="Open the VST instrument editor"
                                                            >Edit</button>
                                                        {/if}
                                                    </div>
                                                    <div class="ch-plugin-row">
                                                        <select
                                                            class="ch-select"
                                                            title={getPluginName(strip) || "Choose a VST instrument"}
                                                            value={strip.pluginUid || 0}
                                                            onchange={(e) => {
                                                                const uid = Number(
                                                                    /** @type {HTMLSelectElement} */ (
                                                                        e.target
                                                                    ).value,
                                                                );
                                                                setPlugin(strip.id, uid);
                                                            }}
                                                        >
                                                            <option value="0">—</option>
                                                            {#each scannedPlugins.filter((p) => p.valid !== false) as plugin}
                                                                <option value={plugin.uid}
                                                                    >{plugin.name}</option
                                                                >
                                                            {/each}
                                                        </select>
                                                    </div>
                                                </div>

                                                <!-- Program preset (only if VST exposes meaningful program names) -->
                                                {#if strip.hasPlugin && (strip.numPrograms ?? 0) > 1 && (strip.programNames ?? []).some((n) => n && !/^Program\s*\d*$/.test(n))}
                                                    <div class="ch-program">
                                                        <select
                                                            class="ch-select ch-program-select"
                                                            value={strip.programIndex ?? 0}
                                                            onchange={(e) => {
                                                                const idx = Number(
                                                                    /** @type {HTMLSelectElement} */ (
                                                                        e.target
                                                                    ).value,
                                                                );
                                                                dispatchCpp("setStripProgram", strip.id, idx);
                                                            }}
                                                        >
                                                            {#each (strip.programNames ?? []) as name, i}
                                                                <option value={i}>{name || `Program ${i}`}</option>
                                                            {/each}
                                                        </select>
                                                    </div>
                                                {/if}


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
        </div> <!-- /mixer-strips-area -->
    {/if}

    {#if inspectorStripId}
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <div
            class="inspector-resize-handle"
            onmousedown={onResizeMouseDown}
        ></div>
        <div class="inspector-panel" style="width: {inspectorWidth}px;">
            <div class="inspector-panel-header">
                <span class="inspector-panel-title">Note Inspector</span>
                <button class="inspector-close-btn" onclick={() => { inspectorStripId = null; }}>✕</button>
            </div>
            <NoteInspector stripId={inspectorStripId} />
        </div>
    {/if}
    </div> <!-- /mixer-body -->
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

    .mixer-body {
        display: flex;
        flex-direction: row;
        flex: 1;
        overflow: hidden;
        min-height: 0;
    }

    .mixer-body.resizing {
        user-select: none;
        cursor: col-resize;
    }

    .mixer-strips-area {
        flex: 1;
        display: flex;
        flex-direction: column;
        overflow: hidden;
        min-width: 0;
    }

    .inspector-resize-handle {
        width: 5px;
        cursor: col-resize;
        background: #1e293b;
        flex-shrink: 0;
        transition: background 0.15s;
    }

    .inspector-resize-handle:hover {
        background: #38bdf8;
    }

    .inspector-panel {
        display: flex;
        flex-direction: column;
        background: #0f172a;
        border-left: 1px solid #1e293b;
        overflow: hidden;
        flex-shrink: 0;
    }

    .inspector-panel-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 8px 12px;
        background: #1e293b;
        border-bottom: 1px solid #334155;
        flex-shrink: 0;
    }

    .inspector-panel-title {
        font-weight: 600;
        font-size: 0.85rem;
        color: #e2e8f0;
    }

    .inspector-close-btn {
        background: transparent;
        border: none;
        color: #64748b;
        cursor: pointer;
        font-size: 0.9rem;
        padding: 2px 6px;
        border-radius: 3px;
        transition: all 0.15s;
    }

    .inspector-close-btn:hover {
        color: #ef4444;
        background: rgba(239, 68, 68, 0.1);
    }

    .mixer-toolbar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 12px;
        padding: 6px 16px;
        flex-shrink: 0;
        border-bottom: 1px solid #1e293b;
        overflow-x: auto;
    }
    .toolbar-center {
        display: flex;
        align-items: center;
        gap: 8px;
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
        flex-shrink: 0;
    }

    .display-controls,
    .zoom-control {
        display: flex;
        align-items: center;
        gap: 6px;
    }
    .display-control-label {
        color: #cbd5e1;
        font-size: 0.75rem;
        font-weight: 500;
        white-space: nowrap;
    }
    .toolbar-select {
        height: 30px;
        padding: 3px 8px;
        border: 1px solid #475569;
        border-radius: 4px;
        background: #0f172a;
        color: #e2e8f0;
        font-size: 0.75rem;
        cursor: pointer;
    }
    .zoom-control {
        gap: 0;
    }
    .zoom-btn,
    .zoom-value {
        height: 30px;
        border: 1px solid #475569;
        background: #0f172a;
        color: #e2e8f0;
        cursor: pointer;
        font-size: 0.8rem;
        font-weight: 600;
    }
    .zoom-btn {
        width: 30px;
        padding: 0;
        font-size: 1rem;
    }
    .zoom-btn:first-child {
        border-radius: 4px 0 0 4px;
    }
    .zoom-btn:last-child {
        border-radius: 0 4px 4px 0;
    }
    .zoom-value {
        min-width: 52px;
        padding: 0 6px;
        border-left: 0;
        border-right: 0;
    }
    .zoom-btn:hover,
    .zoom-value:hover {
        background: #1e293b;
        border-color: #64748b;
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
        width: var(--strip-width, 120px);
        min-width: var(--strip-width, 120px);
        align-self: stretch;
        background: #111827;
        border-right: 1px solid #0f172a;
        padding: 6px 8px 12px;
        gap: 6px;
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
        gap: 6px;
        width: 46px;
        min-width: 46px;
        height: 100%;
        flex-shrink: 0;
        background: #090f1c;
        border-right: 1px solid rgba(51, 65, 85, 0.9); /* softer than other separators */
        /* Same vertical padding as .channel-strip so spacers compute correctly */
        padding: 6px 4px;
        overflow: hidden;
    }
    /* Mirrors .select-bar height so the fader starts at the same Y as individual faders */
    .master-strip-top-spacer {
        flex-shrink: 0;
        /* selection bar + MIDI inspector slot + Lua area and gaps */
        height: 134px;
    }
    /* Mirrors the enlarged controls below each channel fader. */
    .master-strip-bot-spacer {
        flex-shrink: 0;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: flex-start;
        height: 264px;
        gap: 6px;
    }

    .master-track {
        width: 10px !important;
    }
    input.fader-slider.master-slider {
        accent-color: #d4a017;
    }
    .master-db {
        font-size: 0.75rem;
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
        min-width: 30px;
        min-height: 28px;
        font-size: 0.8rem;
        padding: 3px;
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
        height: 8px;
        cursor: pointer;
        transition: filter 0.15s;
    }
    .select-bar-bottom {
        position: absolute;
        bottom: 0;
        left: 8px;  /* match channel-strip horizontal padding so it aligns with select-bar-top */
        right: 8px;
        height: 8px;
    }
    .select-bar:hover {
        filter: brightness(1.4);
    }
    .selected .select-bar {
        filter: brightness(1.6);
    }

    .ch-inspect-midi-slot {
        height: 24px;
        flex-shrink: 0;
    }
    .ch-inspect-midi-btn {
        width: 100%;
        height: 24px;
        padding: 1px 6px;
        border: 1px solid #334155;
        border-radius: 4px;
        background: #0f172a;
        color: #cbd5e1;
        cursor: pointer;
        font-size: 0.7rem;
        font-weight: 600;
        line-height: 1;
        transition: all 0.15s;
    }
    .ch-inspect-midi-btn:hover {
        background: #1e3a5f;
        color: #bfdbfe;
        border-color: #3b82f6;
    }
    .ch-inspect-midi-btn.active {
        background: #1e3a5f;
        color: #38bdf8;
        border-color: #38bdf8;
    }

    .ch-name-area {
        min-height: 22px;
    }
    /* Instrument group bridge header */
    .inst-group {
        display: flex;
        flex-direction: column;
    }
    .bridge-header {
        display: flex;
        align-items: center;
        gap: 5px;
        padding: 4px 6px;
        border-bottom: 2px solid var(--accent, #3b82f6);
        background: rgba(0,0,0,0.25);
        min-height: 22px;
        overflow: hidden;
    }
    .bridge-header.bridge-multi {
        background: rgba(59,130,246,0.08);
    }
    .bridge-icon {
        font-size: 0.75rem;
        flex-shrink: 0;
        opacity: 0.7;
    }
    .bridge-label {
        font-size: 0.75rem;
        font-weight: 600;
        color: #e2e8f0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
        flex: 1;
    }
    .bridge-port {
        font-size: 0.65rem;
        color: #94a3b8;
        flex-shrink: 0;
    }
    .ch-lib-name {
        font-size: 0.75rem;
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
        padding: 3px 5px;
        border: 1px solid #3b82f6;
        border-radius: 2px;
        background: #0f172a;
        color: #f1f5f9;
        font-size: 0.75rem;
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
        font-size: 0.65rem;
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
        width: 6px;
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
        width: 24px;
        height: 100%;
        cursor: pointer;
        accent-color: #3b82f6;
        -webkit-appearance: slider-vertical;
        appearance: slider-vertical;
        margin: 0;
    }

    .fader-value {
        width: 100%;
        min-height: 28px;
        padding: 3px 5px;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #cbd5e1;
        font-size: 0.75rem;
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
        gap: 8px;
        justify-content: center;
        padding: 4px 0;
    }

    .ms-btn {
        width: 32px;
        height: 28px;
        border: 1px solid #555;
        border-radius: 3px;
        background: #2a2a2a;
        color: #aaa;
        font-size: 0.8rem;
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

    /* Inactive strip (library deactivated) — stronger dimming */
    .channel-strip.inactive {
        opacity: 0.35;
        filter: saturate(0.3);
    }
    .channel-strip.inactive .ms-btn {
        opacity: 1;
    }

    /* ── Library chip bar ─────────────────────────────────────── */
    .library-chip-bar {
        display: flex;
        flex-wrap: wrap;
        gap: 6px;
        padding: 6px 12px;
        background: #0d1117;
        border-bottom: 1px solid #1e293b;
    }

    .library-chip {
        display: inline-flex;
        align-items: center;
        gap: 4px;
        padding: 3px 10px 3px 7px;
        border-radius: 14px;
        border: 1px solid #334155;
        background: #1e293b;
        color: #94a3b8;
        font-size: 0.7rem;
        font-weight: 500;
        cursor: pointer;
        transition: all 0.15s ease;
        line-height: 1.3;
    }

    .library-chip:hover {
        border-color: #475569;
        color: #cbd5e1;
    }

    .library-chip-active {
        background: rgba(59, 130, 246, 0.2);
        border-color: #3b82f6;
        color: #93c5fd;
    }
    .library-chip-active:hover {
        background: rgba(59, 130, 246, 0.3);
        border-color: #60a5fa;
        color: #bfdbfe;
    }

    .library-chip-inactive {
        background: #1a1a2e;
        border-color: #2a2a40;
        color: #4a5568;
    }
    .library-chip-inactive:hover {
        border-color: #475569;
        color: #718096;
    }

    .chip-icon {
        font-size: 0.65rem;
    }
    .chip-label {
        white-space: nowrap;
    }

    .ch-xmap {
        margin-bottom: 2px;
    }
    .ch-xmap-picker {
        position: relative;
        min-height: 30px;
        overflow: hidden;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #cbd5e1;
        cursor: pointer;
    }
    .ch-xmap-picker:focus-within {
        border-color: #3b82f6;
    }
    .ch-xmap-value {
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
    .ch-xmap-value > span {
        direction: ltr;
        unicode-bidi: isolate;
    }
    .ch-xmap-placeholder {
        color: #94a3b8;
        direction: ltr;
    }
    .ch-xmap-arrow {
        position: absolute;
        top: 50%;
        right: 8px;
        width: 0;
        height: 0;
        border-right: 4px solid transparent;
        border-left: 4px solid transparent;
        border-top: 5px solid #94a3b8;
        transform: translateY(-25%);
        pointer-events: none;
    }
    .ch-xmap-select {
        position: absolute;
        inset: 0;
        width: 100%;
        height: 100%;
        opacity: 0;
    }
    .ch-control-heading {
        display: flex;
        align-items: center;
        justify-content: space-between;
        min-height: 28px;
        gap: 6px;
        color: #94a3b8;
        font-size: 0.7rem;
        font-weight: 600;
        line-height: 1;
    }
    .ch-action-btn {
        min-width: 52px;
        min-height: 28px;
        padding: 3px 8px;
        background: #0f172a;
        border: 1px solid #334155;
        border-radius: 4px;
        cursor: pointer;
        font-size: 0.72rem;
        font-weight: 600;
        color: #cbd5e1;
        transition: all 0.15s;
        flex-shrink: 0;
    }
    .ch-action-btn:hover {
        background: #1e3a5f;
        color: #bfdbfe;
        border-color: #3b82f6;
    }
    .ch-plugin {
        display: flex;
        flex-direction: column;
        gap: 4px;
    }
    .ch-program {
        display: flex;
        gap: 2px;
    }

    /* ── Lua Plugin Section ──────────────────────────────── */
    .ch-lua {
        display: flex;
        flex-direction: column;
        gap: 2px;
        height: 90px;
        flex-shrink: 0;
        overflow-y: auto;
        overflow-x: hidden;
        justify-content: flex-end;
    }
    .ch-lua-chips {
        display: flex;
        flex-wrap: wrap;
        gap: 2px;
    }
    .ch-lua-chip {
        display: inline-flex;
        align-items: center;
        gap: 2px;
        padding: 1px 4px;
        border-radius: 3px;
        background: #1e293b;
        border: 1px solid #475569;
        font-size: 0.7rem;
        color: #e2e8f0;
        line-height: 1.2;
        transition: border-color 0.15s;
    }
    .ch-lua-chip:hover {
        border-color: #cbd5e1;
    }
    .ch-lua-chip.error {
        color: #fca5a5;
        border-color: #991b1b;
    }
    .ch-lua-chip-name {
        white-space: nowrap;
        max-width: calc(var(--strip-width, 120px) - 48px);
        overflow: hidden;
        text-overflow: ellipsis;
    }
    .ch-lua-chip-x {
        background: none;
        border: none;
        color: #94a3b8;
        cursor: pointer;
        min-width: 22px;
        min-height: 22px;
        font-size: 0.8rem;
        padding: 0 3px;
        line-height: 1;
    }
    .ch-lua-chip-x:hover {
        color: #f87171;
    }
    .ch-lua-select {
        min-height: 26px;
        font-size: 0.7rem;
        color: #94a3b8;
        background: transparent;
        border: none;
        outline: none;
    }
    .ch-lua-select option {
        color: initial;
    }
    .ch-program-select {
        font-size: 0.7rem;
        color: #94a3b8;
    }
    .ch-plugin-row {
        display: flex;
        gap: 2px;
        align-items: center;
    }
    .ch-select {
        flex: 1;
        min-width: 0;
        min-height: 30px;
        padding: 4px 6px;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #cbd5e1;
        font-size: 0.75rem;
        cursor: pointer;
        appearance: auto;
    }
    .ch-select:focus {
        outline: none;
        border-color: #3b82f6;
    }

    .ch-buttons {
        display: flex;
        gap: 6px;
        margin-top: 4px;
    }
    .ch-btn {
        flex: 1;
        min-height: 28px;
        padding: 3px 0;
        border: 1px solid #334155;
        border-radius: 3px;
        background: #0f172a;
        color: #cbd5e1;
        font-size: 0.8rem;
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
