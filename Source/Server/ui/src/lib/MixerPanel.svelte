<script>
    import StereoMeter from "./StereoMeter.svelte";
    import BalanceControl from "./BalanceControl.svelte";
    import { onMount, onDestroy } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import BranchSelector from "./BranchSelector.svelte";
    import AudioDiagnostics from "./AudioDiagnostics.svelte";
    import ToolbarMenu from "./ToolbarMenu.svelte";
    import { parseMixerMeters } from "./mixerMeters.js";
    import MasterAudioPanel from "./MasterAudioPanel.svelte";
    import ChannelAudioPanel from "./ChannelAudioPanel.svelte";
    import PromotedSends from "./PromotedSends.svelte";
    import BusAudioPanel from "./BusAudioPanel.svelte";
    import AudioBusBank from "./AudioBusBank.svelte";
    import LayerDetailsPanel from "./LayerDetailsPanel.svelte";
    import AddBusDialog from "./AddBusDialog.svelte";
    import BusDetailsPanel from "./BusDetailsPanel.svelte";
    import LayerPicker from "./LayerPicker.svelte";
    import NoteInspector from "./NoteInspector.svelte";
    import {
        addStripRange,
        flattenVisualStripOrder,
        selectedChairBusIds,
        chairHasSettings,
    } from "./mixerSelection.js";
    import {
        FAMILY_ORDER,
        canonicalFamily,
        compareChairsInScoreOrder,
        instrumentScoreOrder,
        populateScoreOrder,
    } from "./orchestralOrder.js";
    import {
        STRIP_SIZE_PRESETS,
        readStripSize,
        writeStripSize,
        readLibraryBarVisible,
        writeLibraryBarVisible,
    } from "./uiPreferences.js";
    import { captureChairLevel, planLockedLayerChange } from "./lockedGroupGain.js";
    import { kitColumns, linkedKitLayers, orderChairsWithKits } from "./kitModel.js";
    import { projectSaveButton } from "./projectSaveUi.js";

    let meters = $state(parseMixerMeters(null));

    let {
        uiZoom = 1,
        onZoomIn = () => {},
        onZoomOut = () => {},
        onResetZoom = () => {},
    } = $props();

    let strips = $state([]);
    let undoState = $state({ canUndo: false, canRedo: false, undoDescription: "", redoDescription: "" });
    let availableInputs = $state([]);
    let scannedPlugins = $state([]);
    // Every library's maps; Layer Details shows the strip's library's.
    let libraryMaps = $state([]);
    let luaPluginCatalog = $state([]);
    let masterAudio = $state({
        id: "master",
        name: "Master",
        gainDb: 0,
        peakDb: -120,
        latencyMs: 0,
        inserts: [],
    });
    let masterAudioOpen = $state(false);
    let groupBuses = $state([]);
    // Every chair's bus (docs/chair-buses.md): the chair strip's fader,
    // inserts, sends and output. Keyed by chair bus ID.
    let chairBuses = $state([]);
    let chairBusById = $derived(new Map(chairBuses.map((bus) => [bus.id, bus])));
    let chairAudioBusId = $state("");
    let busAudioBusId = $state("");
    let addBusOpen = $state(false);
    let busDetailsId = $state("");
    let channelAudioStripId = $state("");
    let channelAudio = $state(null);
    let layerDetailsStripId = $state("");
    let audioBankReveal = $state(0);
    let chairs = $state([]);
    let layerCatalog = $state([]);
    let layerPickerChair = $state(null);
    let layerRefreshResult = $state("");
    let branches = $state([]);
    let currentBranch = $state("default");
    let stripSize = $state(readStripSize(window.localStorage));
    let showLibraryBar = $state(readLibraryBarVisible(window.localStorage));
    let stripWidth = $derived(STRIP_SIZE_PRESETS[stripSize].width);

    const updateStripSize = (value) => {
        stripSize = writeStripSize(window.localStorage, value);
    };

    let dirty = $state(false);
    let isDetachedHead = $state(false);
    let saveButton = $derived(projectSaveButton(dirty, isDetachedHead));

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
            if (faderDragging.size === 0) {
                for (const id of Object.keys(gainShadow)) delete gainShadow[id];
                for (const id of Object.keys(gainShadowRaw)) delete gainShadowRaw[id];
            }
        } catch (e) {
            console.error("[Mixer] setMixerState error:", e);
        }
    });
    const unsubscribeMeters = onFromCpp("setMixerMeters", (data) => {
        meters = parseMixerMeters(data);
    });
    onDestroy(unsubscribeMeters);
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
    onFromCpp("setUndoState", (state) => { undoState = state; });
    onFromCpp("setProjectSettings", (state) => {
groupMasters = Object.fromEntries((state.lockedChairIds || []).map(id => [id, { lockSum: true, ...state.chairLevels?.[id] }]));
        if (faderDragging.size === 0) chairLevelShadow = {};
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
    onFromCpp("setMasterAudioState", (data) => {
        if (data && typeof data === "object") masterAudio = data;
    });
    onFromCpp("setGroupBusState", (data) => {
        groupBuses = Array.isArray(data) ? data : [];
        if (busAudioBusId && !groupBuses.some((bus) => bus.id === busAudioBusId))
            busAudioBusId = "";
        if (busDetailsId && !groupBuses.some((bus) => bus.id === busDetailsId))
            busDetailsId = "";
    });
    onFromCpp("setChairBusState", (data) => {
        chairBuses = Array.isArray(data) ? data : [];
        if (faderDragging.size === 0)
            for (const id of Object.keys(chairGainShadow)) delete chairGainShadow[id];
        if (chairAudioBusId && !chairBuses.some((bus) => bus.id === chairAudioBusId))
            chairAudioBusId = "";
    });
    onFromCpp("setStripAudioState", (data) => {
        if (!data || typeof data !== "object" || !data.stripId) return;
        strips = strips.map((strip) =>
            strip.id === data.stripId
                ? { ...strip, audio: { ...(strip.audio || {}), ...data } }
                : strip,
        );
        if (data.stripId === channelAudioStripId) channelAudio = data;
    });
    onFromCpp("setInstrumentEditorState", (data) => {
        if (!data?.stripId) return;
        strips = strips.map((strip) => strip.id === data.stripId
            ? { ...strip, instrumentEditorOpen: Boolean(data.editorOpen) } : strip);
    });
    onFromCpp("setLibraryMaps", (data) => {
        libraryMaps = Array.isArray(data) ? data : [];
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
    onFromCpp("setChairState", (data) => {
        chairs = Array.isArray(data) ? data : [];
    });
    onFromCpp("setLayerCatalog", (data) => {
        layerCatalog = Array.isArray(data) ? data : [];
    });
    onFromCpp("layerOperationResult", (result) => {
        const message = typeof result === "string"
            ? result
            : result?.message || "Layer operation failed";
        if (result?.operation === "refresh") {
            layerRefreshResult = message;
            setTimeout(() => { layerRefreshResult = ""; }, 4000);
        } else if (message !== "Layer added") {
            layerRefreshResult = message;
            setTimeout(() => { layerRefreshResult = ""; }, 5000);
            console.warn("[Mixer]", message);
        }
    });
    onFromCpp("setDetachedHead", (detached) => {
        isDetachedHead = detached;
    });

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
    let inspectorOpen = $state(false);
    let inspectorWidth = $state(320);
    let isResizing = $state(false);

    const toggleInspector = (stripId) => {
        if (inspectorOpen && inspectorStripId === stripId) {
            inspectorOpen = false;
            return;
        }
        inspectorStripId = stripId;
        inspectorOpen = true;
    };

    const showInspector = () => {
        inspectorStripId ??= [...selectedIds][0] ?? lastClickedId ?? strips[0]?.id ?? null;
        inspectorOpen = true;
    };

    onFromCpp("showMixerView", (view) => {
        if (view === "inspector") showInspector();
        else if (view === "audio-bank") audioBankReveal += 1;
    });

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
    // The selection holds layer IDs and chair bus IDs (chair strips).
    /** Selected IDs that are still layers in the mixer. */
    const selectedLayerIds = () => [...selectedIds].filter((id) => strips.some((strip) => strip.id === id));
    /** The chairs the selection stands for; a selected layer counts as its chair. */
    const selectedChairs = () => selectedChairBusIds(selectedIds, strips, new Set(chairBusById.keys()));

    /** JSON-encode the selected layer IDs for group native calls */
    const selectedIdsJson = () => JSON.stringify(selectedLayerIds());

    onMount(() => {
        dispatchCpp("requestBranches");
        dispatchCpp("requestMixerState");
        dispatchCpp("getAvailableInputs");
        dispatchCpp("requestPluginsState");
        dispatchCpp("requestMasterAudioState");
        dispatchCpp("requestGroupBusState");
        dispatchCpp("requestLibraryMaps");
        dispatchCpp("requestCurrentBranch");
        dispatchCpp("requestChairs");
        dispatchCpp("requestLayerCatalog");

        // Escape closes the focused panel before clearing mixer selection.
        const handleKeyDown = (e) => {
            if (e.key !== "Escape") return;
            if (layerPickerChair) layerPickerChair = null;
            else if (addBusOpen) addBusOpen = false;
            else if (busDetailsId) busDetailsId = "";
            else if (layerDetailsStripId) layerDetailsStripId = "";
            else if (channelAudioStripId) channelAudioStripId = "";
            else if (chairAudioBusId) chairAudioBusId = "";
            else if (busAudioBusId) busAudioBusId = "";
            else if (masterAudioOpen) masterAudioOpen = false;
            else clearSelection();
        };
        window.addEventListener("keydown", handleKeyDown);
        return () => window.removeEventListener("keydown", handleKeyDown);
    });

    // A kit's linked layers (kitModel.js) share maps, updates and removal.
    const linkedIds = (stripId) =>
        linkedKitLayers(strips.find((strip) => strip.id === stripId), strips, chairs).map((strip) => strip.id);
    const removeStrip = (id, thisChairOnly = false) => {
        const target = strips.find((strip) => strip.id === id);
        if (target?.chairId) {
            if (!thisChairOnly && linkedIds(id).length > 1) dispatchCpp("removeChairLayer", id, true);
            else dispatchCpp("removeChairLayer", id);
            selectedIds.delete(id);
            selectedIds = new Set(selectedIds);
            return;
        }
        if (isMultiSelected && selectedIds.has(id)) {
            dispatchCpp("removeGroupStrips", selectedIdsJson());
            clearSelection();
            return;
        }
        dispatchCpp("removeMixerStrip", id);
    };

    const refreshLayerFromLibrary = (strip) => {
        if (!strip?.chairId || strip.missingPatchReference ||
            !strip.sourcePatchOutOfDate) return;
        dispatchCpp("refreshChairLayerFromLibrary", strip.id);
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
    const openChannelAudio = (strip) => {
        channelAudioStripId = strip.id;
        channelAudio = { stripId: strip.id, ...(strip.audio || {}) };
        dispatchCpp("requestStripAudioState", strip.id);
    };
    const visibleStripInserts = (strip) => [
        ...(strip.audio?.preFaderInserts || []).map((insert) => ({ ...insert, location: "Pre" })),
        ...(strip.audio?.postFaderInserts || []).map((insert) => ({ ...insert, location: "Post" })),
    ];
    const stripNeedsAttention = (strip) =>
        Boolean(
            strip.missingPatchReference ||
            strip.sourcePatchOutOfDate ||
            strip.pluginStatus === "missing" ||
            strip.pluginStatus === "failed" ||
            strip.luaPlugins?.some((plugin) => !plugin.loaded) ||
            visibleStripInserts(strip).some((insert) =>
                insert.status === "missing" || insert.status === "failed"),
        );
    const openLayerDetails = (strip) => {
        channelAudioStripId = "";
        channelAudio = null;
        layerDetailsStripId = strip.id;
    };
    /** Route a chair, or every selected chair when this one is selected. */
    const setChairOutput = (chairBusId, busId) => {
        const chosen = selectedChairs();
        const chairsToRoute = chosen.includes(chairBusId) ? chosen : [];
        if (chairsToRoute.length > 1) {
            dispatchCpp("setChairsOutput", JSON.stringify(chairsToRoute), busId);
            return;
        }
        dispatchCpp("setChairOutput", chairBusId, busId);
    };
    const loadExpressionMap = (stripId, entityID) => {
        if (isMultiSelected && selectedIds.has(stripId)) {
            dispatchCpp("setGroupExpressionMap", selectedIdsJson(), entityID);
            return;
        }
        if (linkedIds(stripId).length > 1) {
            dispatchCpp("setGroupExpressionMap", JSON.stringify(linkedIds(stripId)), entityID);
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
        if (linkedIds(stripId).length > 1) {
            dispatchCpp("setGroupExpressionMap", JSON.stringify(linkedIds(stripId)), "");
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

    // Last applied (clamped) gain for group calculations. The combined marker
    // describes the gains sent to audio, not a hypothetical unclamped sum.
    const gainShadowRaw = $state({});

    // ── Fader display smoothing ─────────────────────────────
    // Display positions ease toward target values via requestAnimationFrame.
    // Key = stripId or "chair:<chair bus ID>". Value = current display pos (0-1000).
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

    /** Ease a chair's layer faders to new computed positions.
     *  Call AFTER setting gains/mute so computed positions are correct. */
    const easeGroupFaders = (instrGroup) => {
        for (const s of instrGroup.strips) {
            const db = gainShadow[s.id] ?? gainShadowRaw[s.id] ?? s.gainDb ?? 0;
            easeFaderTo(s.id, Math.round(dbToPos(db) * 1000));
        }
    };

    const setGain = (stripId, db) => {
        gainShadow[stripId] = db;
        gainShadowRaw[stripId] = db; // user-dragged values are already in display range
        queueMixerControl(stripId, { gainDb: db });
    };

    /** Clamp compensation to the supported audio gain range. */
    const setGainRaw = (stripId, rawDb) => {
        const clamped = Math.max(FADER_MIN, Math.min(FADER_MAX, rawDb));
        gainShadowRaw[stripId] = clamped;
        gainShadow[stripId] = clamped;
        queueMixerControl(stripId, { gainDb: clamped });
    };

    let pendingControlBatch = null;
    let pendingChairLevels = null;
    const batchMixerEdit = (label, edit) => {
        if (pendingControlBatch) { edit(); return; }
        pendingControlBatch = new Map();
        pendingChairLevels = new Map();
        try { edit(); }
        finally {
            const changes = [...pendingControlBatch.values()];
            const levels = [...pendingChairLevels.values()];
            pendingControlBatch = null;
            pendingChairLevels = null;
            if (levels.length) dispatchCpp("setMixerControls", changes, label, levels);
            else if (changes.length) dispatchCpp("setMixerControls", changes, label);
        }
    };
    const queueMixerControl = (id, values) => {
        if (pendingControlBatch)
            pendingControlBatch.set(id, { ...pendingControlBatch.get(id), id, ...values });
        else
            dispatchCpp("setMixerControls", [{ id, ...values }], "Adjust layer gains");
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

    const toggleLibActive = (libraryName) => batchMixerEdit("Toggle library activation", () => {
        const libraryStrips = strips.filter((strip) => strip.library === libraryName);
        if (libraryStrips.length === 0) return;

        // Native library activation changes every matching strip to the same
        // state: deactivate when all are active, otherwise activate them all.
        const nextActive = !libraryStrips.every((strip) => strip.active !== false);
        const affectedIds = new Set(libraryStrips.map((strip) => strip.id));
        const adjustedGroups = [];
        const adjustments = [];
        for (const familyGroup of groupedStrips) {
            for (const instrGroup of familyGroup.instrGroups || []) {
                const affected = instrGroup.strips.filter(s => affectedIds.has(s.id));
                if (!affected.length || !getGroupMaster(instrGroup.key).lockSum) continue;
                const plan = planLockedLayerChange(groupGainState(instrGroup),
                    affected.map(s => ({ id: s.id, active: nextActive })),
                    getChairLevel(instrGroup), anySoloed);
                rememberChairLevel(instrGroup, plan.state);
                adjustments.push(...plan.changes.filter(change => change.gainDb !== undefined));
                adjustedGroups.push(instrGroup);
            }
        }

        // Update the local audibility state before gain echoes arrive from
        // native code, avoiding a transient jump in the derived master fader.
        for (const strip of libraryStrips)
            strip.active = nextActive;
        for (const adjustment of adjustments)
            setGainRaw(adjustment.id, adjustment.gainDb);
        for (const instrGroup of adjustedGroups)
            easeGroupFaders(instrGroup);

        for (const strip of libraryStrips) queueMixerControl(strip.id, { active: nextActive });
    });

    // ── Mute / Solo ──────────────────────────────────────────────
    let anySoloed = $derived(strips.some((s) => s.soloed));
    let anyBusSoloed = $derived(groupBuses.some((bus) => bus.soloed));

    const isAudible = (strip) => strip.active !== false && !strip.muted && (!anySoloed || strip.soloed);

    const toggleMute = (stripId) => batchMixerEdit("Mute layers", () => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const newVal = !strip.muted;
        if (isMultiSelected && selectedIds.has(stripId)) {
            for (const id of selectedLayerIds()) {
                queueMixerControl(id, { muted: newVal });
            }
        } else {
            queueMixerControl(stripId, { muted: newVal });
        }
    });

    /**
     * Group-aware mute toggle. In locked mode, redistributes power to siblings
     * toward the stored chair target (see docs/gain.md).
     * Muting  = treat as moving fader to -∞ (power → 0), redistribute to others.
     * Unmuting = treat as restoring remembered power, others scale down.
     */
    const toggleMuteInGroup = (instrGroup, stripId) => batchMixerEdit("Mute layer", () => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const willMute = !strip.muted;

        // Multi-select: delegate to simple toggleMute (no per-group compensation)
        if (isMultiSelected && selectedIds.has(stripId)) {
            toggleMute(stripId);
            return;
        }

        if (getGroupMaster(instrGroup.key).lockSum) {
            const plan = planLockedLayerChange(groupGainState(instrGroup),
                [{ id: stripId, muted: willMute }], getChairLevel(instrGroup), anySoloed);
            rememberChairLevel(instrGroup, plan.state);
            for (const change of plan.changes)
                if (change.gainDb !== undefined) setGainRaw(change.id, change.gainDb);
        }

        // Optimistic mute: set locally BEFORE dispatching so the next render
        // (triggered by sibling gain echoes) already sees the right muted state.
        // This prevents the intermediate "bounce" where gains are updated but
        // mute hasn't arrived yet.
        strip.muted = willMute;

        // Ease all faders in this group to their final positions
        easeGroupFaders(instrGroup);

        // Actually mute/unmute the strip
        queueMixerControl(stripId, { muted: willMute });
    });

    const toggleSolo = (stripId) => batchMixerEdit("Solo layers", () => {
        const strip = strips.find((s) => s.id === stripId);
        if (!strip) return;
        const newVal = !strip.soloed;
        if (isMultiSelected && selectedIds.has(stripId)) {
            for (const id of selectedLayerIds()) {
                queueMixerControl(id, { soloed: newVal });
            }
        } else {
            queueMixerControl(stripId, { soloed: newVal });
        }
    });

    const clearSolos = () => {
        dispatchCpp("clearSolos");
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
    const handleGainValueInput = (strip, value) => batchMixerEdit("Adjust layer gains", () => {
        let v = parseFloat(value);
        if (isNaN(v)) v = 0;
        v = Math.max(-120, Math.min(6, v));
        if (isMultiSelected && selectedIds.has(strip.id)) {
            for (const id of selectedLayerIds()) setGain(id, v);
        } else {
            const group = groupedStrips.flatMap(g => g.instrGroups).find(g => g.strips.some(s => s.id === strip.id));
            if (group) handleGroupFaderInput(group, strip, dbToPos(v));
            else setGain(strip.id, v);
        }
    });

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
    // Target and blend memory are project state. Shadows bridge async echoes
    // during a drag; native state replaces them on completed edits/Undo.
    let groupMasters = $state({});
    let chairLevelShadow = $state({});
    const getGroupMaster = (key) => chairLevelShadow[key] ?? groupMasters[key] ?? { lockSum: false };
    const groupGainState = (group) => group.strips.map(s => ({
        ...s, gainDb: gainShadow[s.id] ?? s.gainDb ?? 0,
    }));
    const getChairLevel = (group) => {
        const gm = getGroupMaster(group.key);
        return Number.isFinite(gm.targetDb) ? gm : captureChairLevel(groupGainState(group), anySoloed);
    };
    const rememberChairLevel = (group, state) => {
        chairLevelShadow[group.key] = { lockSum: true, ...state };
        pendingChairLevels.set(group.key, { id: group.key, targetDb: state.targetDb, weights: state.weights });
    };

    /** Compute group power: sum of power of all audible strips.
     *  A strip is audible if it is not muted AND (no solo active OR strip is soloed).
     *  Uses the clamped gains actually sent to audio. */
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

    // ── Chair bus fader ───────────────────────────────────────
    // The chair fader is a plain gain after the layers are summed; it never
    // changes layer gains. The shadow holds the dragged value until native
    // state catches up.
    const chairGainShadow = $state({});
    const chairGainDb = (bus) => chairGainShadow[bus.id] ?? bus.gainDb ?? 0;
    const setChairGain = (bus, db) => {
        const clamped = Math.max(FADER_MIN, Math.min(FADER_MAX, db));
        chairGainShadow[bus.id] = clamped;
        dispatchCpp("setGroupBusGain", bus.id, clamped);
    };
    let anyChairSoloed = $derived(chairBuses.some((bus) => bus.soloed));
    /** A muted chair, or one silenced by another chair's solo. */
    const chairSilenced = (bus) => Boolean(bus && (bus.muted || (anyChairSoloed && !bus.soloed)));

    /**
     * Individual strip fader drag within a group.
     * Lock OFF: only the dragged strip changes; master auto-follows (power model).
     * Lock ON:  master is "locked" — redistribute power proportionally across
     *           other unmuted siblings to keep group power constant.
     */
    const handleGroupFaderInput = (instrGroup, strip, pos) => batchMixerEdit("Adjust layer gains", () => {
        const gm = getGroupMaster(instrGroup.key);
        const newDb = Math.round(posToDB(pos) * 10) / 10;
        if (isMultiSelected && selectedIds.has(strip.id)) {
            const delta = newDb - (gainShadow[strip.id] ?? strip.gainDb ?? 0);
            for (const id of selectedLayerIds()) {
                const selected = strips.find(s => s.id === id);
                if (selected) setGainRaw(id, (gainShadow[id] ?? selected.gainDb ?? 0) + delta);
            }
            return;
        }
        if (gm.lockSum) {
            const plan = planLockedLayerChange(groupGainState(instrGroup),
                [{ id: strip.id, gainDb: newDb }], getChairLevel(instrGroup), anySoloed);
            rememberChairLevel(instrGroup, plan.state);
            for (const change of plan.changes) setGainRaw(change.id, change.gainDb);
            return;
        }
        setGain(strip.id, newDb);
    });

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

        // Explicit chairs are the mixer hierarchy. This keeps empty chairs
        // visible and makes their Add Layer affordance available before any
        // plug-in instance exists.
        if (chairs.length > 0) {
            // A percussion kit's chairs stay together (docs/percussion-kits.md).
            for (const chair of orderChairsWithKits(chairs, compareChairsInScoreOrder)) {
                const fam = canonicalFamily(chair.family || "");
                if (!familyMap.has(fam)) {
                    const group = {
                        family: fam,
                        displayName: chair.family || "Other",
                        strips: [],
                        instrGroups: [],
                        colors: FAMILY_COLORS[fam] || defaultColors,
                    };
                    familyMap.set(fam, group);
                    groups.push(group);
                }
                const chairStrips = strips.filter(
                    (strip) => strip.chairId === chair.id ||
                        (strip.inputPort === chair.port && strip.inputChannel === chair.channel),
                );
                const group = familyMap.get(fam);
                group.strips.push(...chairStrips);
                group.instrGroups.push({
                    key: chair.id,
                    chairId: chair.id,
                    chairBusId: `chair:${chair.id}`,
                    chair,
                    label: chair.name,
                    portLabel: `P${chair.port + 1}.${chair.channel + 1}`,
                    strips: chairStrips,
                });
            }
            groups.sort((a, b) => {
                const ia = FAMILY_ORDER.indexOf(a.family);
                const ib = FAMILY_ORDER.indexOf(b.family);
                return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
            });
            for (const group of groups) group.columns = kitColumns(group.instrGroups);
            return groups;
        }

        // Transitional fallback for installations that have not created any
        // explicit chairs yet.
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

            // Second-level grouping: one group per chair bus (layers without
            // a chair each have their own).
            const instrMap = new Map();
            const instrOrder = []; // preserve the sort order above
            for (const strip of g.strips) {
                const key = strip.chairBusId || `${strip.inputPort}:${strip.inputChannel}`;
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
                        chairBusId: strip.chairBusId || "",
                        label: input ? (input.label || input.name) : (portLabel || "—"),
                        portLabel,
                        strips: [],
                    });
                    instrOrder.push(key);
                }
                instrMap.get(key).strips.push(strip);
            }
            g.instrGroups = instrOrder.map((k) => instrMap.get(k));
            g.columns = kitColumns(g.instrGroups);
        }
        return groups;
    });
    // A chair with one layer shows no chair strip unless its chair bus has
    // settings you can hear (level, mute, solo, balance, inserts); its layer
    // strip shows the chair's sends instead. The output is in the header.
    // A strip being dragged stays until the drag ends.
    let draggingChairId = $state("");
    const showsChairStrip = (instrGroup) => {
        const bus = chairBusById.get(instrGroup.chairBusId);
        if (!bus || instrGroup.strips.length === 0) return false;
        return instrGroup.strips.length > 1 || chairHasSettings(bus) || draggingChairId === bus.id;
    };
    let flatStripOrder = $derived(flattenVisualStripOrder(groupedStrips, showsChairStrip));
    // Every rendered strip shares the same send band; cap its height, not the
    // number of promoted sends. Longer lists scroll without consuming faders.
    // Sends belong to chairs: on the chair strip, or on the layer strip of a
    // one-layer chair without one.
    const sendsAt = (bus, location) => (bus?.sends || []).filter((send) => !!send.preFader === (location === "Pre"));
    let consoleHasKits = $derived(chairs.some((chair) => chair.kitId));
    let renderedChairBuses = $derived(groupedStrips.flatMap((group) => group.instrGroups)
        .filter((instrGroup) => instrGroup.strips.length > 0)
        .map((instrGroup) => chairBusById.get(instrGroup.chairBusId)).filter(Boolean));
    let sendRows = $derived(Object.fromEntries(["Pre", "Post"].map((location) => [location,
        Math.min(2, Math.max(0, ...renderedChairBuses
            .map((bus) => sendsAt(bus, location).filter((send) => send.showOnStrip).length)))])));
    const chairInserts = (bus) => [
        ...(bus?.audio?.preFaderInserts || []).map((insert) => ({ ...insert, location: "Pre" })),
        ...(bus?.audio?.postFaderInserts || []).map((insert) => ({ ...insert, location: "Post" })),
    ];
    const insertSummaryOf = (inserts) => {
        const active = inserts.filter((insert) => !insert.bypassed && insert.status === "loaded").length;
        return `${inserts.length} insert${inserts.length === 1 ? "" : "s"}${inserts.length ? active ? `, ${active} active`
            : inserts.every((insert) => insert.bypassed) ? ", all bypassed" : ", none active" : ""}`;
    };
    const chairNameOf = (busId) => groupedStrips.flatMap((group) => group.instrGroups)
        .find((instrGroup) => instrGroup.chairBusId === busId)?.label || "";
    let sendRowHeight = $derived(stripSize === "large" ? 36 : 52);
    const sendSpan = (rows) => rows ? rows * sendRowHeight + 5 : 0;

</script>

{#snippet chairColumn(instrGroup, group)}
                                {@const instInput = availableInputs.find(
                                    (i) => i.port === instrGroup.strips[0]?.inputPort
                                        && i.channel === instrGroup.strips[0]?.inputChannel
                                )}
                                {@const groupChairBus = instrGroup.strips.length > 0 ? chairBusById.get(instrGroup.chairBusId) : undefined}
                                {@const showChairStrip = showsChairStrip(instrGroup)}
                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                <!-- svelte-ignore a11y_no_static_element_interactions -->
                                <div
                                    class="inst-group"
                                    style="--accent: {group.colors.accent};"
                                    onclick={(e) => e.stopPropagation()}
                                >
                                    <!-- Bridge header: the chair's name, port, output and Add Layer.
                                         Same height for every group (1 or N strips). -->
                                    <div
                                        class="bridge-header"
                                        class:bridge-multi={instrGroup.strips.length > 1}
                                    >
                                        <div class="bridge-identity" title={instrGroup.portLabel ? `${instrGroup.label} · ${instrGroup.portLabel}` : instrGroup.label}>
                                            <span class="bridge-icon">{(instrGroup.chair?.role === "solo" || instInput?.isSolo) ? "👤" : "👥"}</span>
                                            <span class="bridge-label">{instrGroup.label}</span>
                                            {#if instrGroup.portLabel}
                                                <span class="bridge-port">{instrGroup.portLabel}</span>
                                            {/if}
                                        </div>
                                        {#if groupChairBus}
                                            {@const chosenChairs = selectedChairs()}
                                            {@const routesSelection = chosenChairs.length > 1 && chosenChairs.includes(groupChairBus.id)}
                                            <select
                                                class="bridge-output"
                                                aria-label={`Output for ${instrGroup.label} chair`}
                                                title={`Output: ${groupBuses.find((bus) => bus.id === groupChairBus.directOutputBusId)?.name || "Master"}${routesSelection ? ` · routes all ${chosenChairs.length} selected chairs` : ""}`}
                                                value={groupChairBus.directOutputBusId || ""}
                                                onchange={(event) => setChairOutput(groupChairBus.id, event.currentTarget.value)}
                                            >
                                                <option value="">Master</option>
                                                {#each groupBuses as bus (bus.id)}
                                                    <option value={bus.id}>{bus.name}</option>
                                                {/each}
                                            </select>
                                        {/if}
                                        {#if instrGroup.chairId}
                                            <button
                                                class="bridge-add-layer"
                                                title={`Add a layer to ${instrGroup.label}`}
                                                aria-label={`Add a layer to ${instrGroup.label}`}
                                                onclick={() => { layerPickerChair = instrGroup.chair; }}
                                            >+</button>
                                        {/if}
                                    </div>
                                    <!-- inst-body: the layers, then the chair strip -->
                                    <div class="inst-body">
                                    <div class="strips-row">
                                        {#if instrGroup.chairId && instrGroup.strips.length === 0}
                                            <div class="empty-chair">
                                                <span>No sound layers assigned</span>
                                                <button onclick={() => { layerPickerChair = instrGroup.chair; }}>Add the first layer</button>
                                            </div>
                                        {/if}
                                        {#each instrGroup.strips as strip (strip.id)}
                                            {@const layerShowsChairSends = Boolean(groupChairBus) && !showChairStrip}
                                            <!-- svelte-ignore a11y_click_events_have_key_events -->
                                            <!-- svelte-ignore a11y_no_static_element_interactions -->
                                            <div
                                                class="channel-strip"
                                                class:selected={selectedIds.has(strip.id)}
                                                class:suppressed={!isAudible(strip) || chairSilenced(chairBusById.get(instrGroup.chairBusId))}
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

                                                <div class="ch-strip-identity" title={`${strip.layerName || "Layer"} — ${strip.library || "Unknown library"}`}>
                                                    <strong>{strip.library || "Unknown library"}</strong>
                                                </div>

                                                <section class="ch-strip-section ch-instrument-section" aria-label="Instrument">
                                                <div class="ch-instrument-summary">
                                                    {#if strip.hasPlugin}
                                                        <button
                                                            onclick={() => dispatchCpp("toggleStripEditor", strip.id)}
                                                            aria-pressed={Boolean(strip.instrumentEditorOpen)}
                                                            title={`${strip.instrumentEditorOpen ? "Hide" : "Edit"} ${getPluginName(strip) || "VST instrument"}; use Edit details to change the player`}
                                                        >{strip.instrumentEditorOpen ? "Hide VSTi" : "Edit VSTi"}</button>
                                                    {:else if strip.pluginUid && (strip.pluginStatus === "missing" || strip.pluginStatus === "failed")}
                                                        <button
                                                            class="attention"
                                                            onclick={() => setPlugin(strip.id, strip.pluginUid)}
                                                            title={strip.pluginError || "The VST instrument did not load; retry with its saved state"}
                                                        >Retry VSTi</button>
                                                    {:else}
                                                        <select
                                                            aria-label="Choose VSTi"
                                                            title="Choose a VST instrument"
                                                            value={strip.pluginUid || 0}
                                                            onchange={(event) => setPlugin(strip.id, Number(event.currentTarget.value))}
                                                        >
                                                            <option value={0}>{stripSize === "compact" ? "+ VSTi" : "Choose VSTi…"}</option>
                                                            {#if strip.pluginUid && !scannedPlugins.some((plugin) => Number(plugin.uid) === Number(strip.pluginUid) && plugin.valid !== false)}
                                                                <option value={strip.pluginUid} disabled>Unavailable VSTi</option>
                                                            {/if}
                                                            {#each scannedPlugins.filter((plugin) => plugin.valid !== false) as plugin}
                                                                <option value={plugin.uid}>{plugin.name}</option>
                                                            {/each}
                                                        </select>
                                                    {/if}
                                                </div>
                                                </section>

                                                <div class="ch-fx-zone">
                                                    {#each ["Pre", "Post"] as location}
                                                        {@const inserts = visibleStripInserts(strip).filter((insert) => insert.location === location)}
                                                        {@const layerSendCount = layerShowsChairSends ? sendsAt(groupChairBus, location).length : 0}
                                                        {@const activeInserts = inserts.filter((insert) => !insert.bypassed && insert.status === "loaded").length}
                                                        {@const insertSummary = insertSummaryOf(inserts)}
                                                        <section class="ch-strip-section ch-fx-section" class:post-fader={location === "Post"}
                                                            aria-label={`${location}-fader effects`}>
                                                        <div class="ch-section-heading">
                                                        <span class="ch-section-title">{location}-fader</span>
                                                        <button
                                                            class="ch-audio-fx"
                                                            class:has-effects={inserts.length > 0 || layerSendCount > 0}
                                                            onclick={() => openChannelAudio(strip)}
                                                            aria-label={`Manage ${location.toLowerCase()}-fader effects — ${insertSummary}`}
                                                            title={layerShowsChairSends
                                                                ? `${location}-fader: ${insertSummary}, ${layerSendCount} chair send${layerSendCount === 1 ? "" : "s"} — manage`
                                                                : `${location}-fader: ${insertSummary} — manage`}
                                                        >{#if inserts.length > 0}<span class="ch-insert-count" class:active={activeInserts > 0} aria-hidden="true">{inserts.length}{stripSize === "compact" ? "" : " FX"}</span>{/if}<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M3 6h8m4 0h6M3 12h2m4 0h12M3 18h12m4 0h2M11 3v6M7 9v6M17 15v6" /></svg></button>
                                                        </div>
                                                        {#if stripSize === "large"}
                                                        <div class="ch-fx-summary" aria-label={`${location}-fader audio effect inserts`}>
                                                            {#if inserts.length > 0}
                                                                {#each inserts as insert (insert.id)}
                                                                    <div class="ch-fx-summary-row" class:bypassed={insert.bypassed}>
                                                                        <button class="ch-fx-summary-name"
                                                                            onclick={() => dispatchCpp("toggleStripInsertEditor", strip.id, insert.id)}
                                                                            disabled={insert.status !== "loaded"}
                                                                            aria-label={`${insert.editorOpen ? "Hide" : "Edit"} ${insert.name || "audio effect"}`}
                                                                            aria-pressed={Boolean(insert.editorOpen)}
                                                                            title={`${insert.editorOpen ? "Hide" : "Edit"} ${insert.name || "Unavailable plug-in"}`}>
                                                                            <strong>{insert.name || "Unavailable plug-in"}</strong>
                                                                        </button>
                                                                        <div class="ch-fx-summary-actions">
                                                                            <button
                                                                                class:active={insert.bypassed}
                                                                                onclick={() => dispatchCpp("setStripInsertBypassed", strip.id, insert.id, !insert.bypassed)}
                                                                                title={insert.bypassed ? `Enable ${insert.name}` : `Bypass ${insert.name}`}
                                                                            >{insert.bypassed ? "Enable" : "Bypass"}</button>
                                                                        </div>
                                                                    </div>
                                                                {/each}
                                                            {:else}
                                                                <div class="ch-fx-empty">No inserts</div>
                                                            {/if}
                                                        </div>
                                                        {/if}
                                                        {#if layerShowsChairSends && sendRows[location] > 0}
                                                            <PromotedSends chair={groupChairBus} chairName={instrGroup.label} buses={groupBuses}
                                                                horizontal={stripSize === "large"}
                                                                preFader={location === "Pre"} rows={sendRows[location]} />
                                                        {/if}
                                                        </section>
                                                    {/each}
                                                </div>

                                                <BalanceControl value={strip.audio?.balance ?? 0}
                                                    label={`${strip.layerName || strip.library || "Layer"} stereo balance`}
                                                    command="setStripBalance" id={strip.id} />

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
                                                                onpointercancel={() => { faderDragging.delete(strip.id); }}
                                                                onkeyup={() => { faderDragging.delete(strip.id); }}
                                                                onblur={() => { faderDragging.delete(strip.id); }}
                                                                onpointerleave={() => { faderDragging.delete(strip.id); delete faderAnchor[strip.id]; }}
                                                                ondblclick={() =>
                                                                    handleGainValueInput(strip, "0")}
                                                            />
                                                        </div>
                                                        <StereoMeter levels={meters.stereoStrips[strip.id]}
                                                            label={strip.layerName || strip.library || "Layer"} kind="strip" id={strip.id} />
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

                                                <button
                                                    class="ch-details-btn"
                                                    class:attention={stripNeedsAttention(strip)}
                                                    onclick={() => openLayerDetails(strip)}
                                                    title={stripNeedsAttention(strip) ? "Layer details need attention" : "Edit layer assignments and setup"}
                                                >{stripNeedsAttention(strip) ? "Details · !" : "Edit details…"}</button>
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
                                        {#if showChairStrip}
                                            {@const chairBus = groupChairBus}
                                            {@const chairLabel = instrGroup.label}
                                            {@const multiLayer = instrGroup.strips.length > 1}
                                            {@const locked = multiLayer && getGroupMaster(instrGroup.key).lockSum}
                                            {@const blendDb = getGroupDb(instrGroup)}
                                            {@const blendTargetDb = locked ? getChairLevel(instrGroup).targetDb : blendDb}
                                            {@const offTarget = locked && Math.abs(blendDb - blendTargetDb) > 0.05}
                                            {@const faderKey = `chair:${chairBus.id}`}
                                            {@const chairFaderPos = Math.round(dbToPos(chairGainDb(chairBus)) * 1000)}
                                            <!-- The chair strip: all the chair's layers mix into its bus. -->
                                            <!-- svelte-ignore a11y_click_events_have_key_events -->
                                            <!-- svelte-ignore a11y_no_noninteractive_element_interactions -->
                                            <div
                                                class="channel-strip chair-strip"
                                                class:selected={selectedIds.has(chairBus.id)}
                                                class:suppressed={chairSilenced(chairBus)}
                                                role="group"
                                                aria-label={`${chairLabel} chair`}
                                                onclick={(e) => e.stopPropagation()}
                                            >
                                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                                <!-- svelte-ignore a11y_no_static_element_interactions -->
                                                <div
                                                    class="select-bar select-bar-top"
                                                    style="background: {group.colors.accent};"
                                                    onclick={(e) => {
                                                        e.stopPropagation();
                                                        handleStripClick(chairBus.id, e);
                                                    }}
                                                ></div>

                                                <div class="ch-strip-identity" title={`${chairLabel}: the chair's layers mix here`}>
                                                    <strong>{chairLabel}</strong>
                                                    <span class="ch-chair-kind">Chair</span>
                                                </div>

                                                <!-- Layers have their instrument here; a chair has none. -->
                                                <div class="ch-instrument-section ch-chair-blank" aria-hidden="true"></div>

                                                <div class="ch-fx-zone">
                                                    {#each ["Pre", "Post"] as location}
                                                        {@const inserts = chairInserts(chairBus).filter((insert) => insert.location === location)}
                                                        {@const activeInserts = inserts.filter((insert) => !insert.bypassed && insert.status === "loaded").length}
                                                        {@const insertSummary = insertSummaryOf(inserts)}
                                                        {@const sendCount = sendsAt(chairBus, location).length}
                                                        <section class="ch-strip-section ch-fx-section" class:post-fader={location === "Post"}
                                                            aria-label={`${chairLabel} chair ${location.toLowerCase()}-fader effects`}>
                                                        <div class="ch-section-heading">
                                                        <span class="ch-section-title">{location}-fader</span>
                                                        <button
                                                            class="ch-audio-fx"
                                                            class:has-effects={inserts.length > 0 || sendCount > 0}
                                                            onclick={() => { chairAudioBusId = chairBus.id; }}
                                                            aria-label={`Manage ${chairLabel} chair ${location.toLowerCase()}-fader effects and sends — ${insertSummary}`}
                                                            title={`${location}-fader: ${insertSummary}, ${sendCount} send${sendCount === 1 ? "" : "s"} — manage`}
                                                        >{#if inserts.length > 0}<span class="ch-insert-count" class:active={activeInserts > 0} aria-hidden="true">{inserts.length}{stripSize === "compact" ? "" : " FX"}</span>{/if}<svg viewBox="0 0 24 24" width="16" height="16" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" aria-hidden="true"><path d="M3 6h8m4 0h6M3 12h2m4 0h12M3 18h12m4 0h2M11 3v6M7 9v6M17 15v6" /></svg></button>
                                                        </div>
                                                        {#if stripSize === "large"}
                                                        <div class="ch-fx-summary" aria-label={`${chairLabel} chair ${location.toLowerCase()}-fader audio effect inserts`}>
                                                            {#if inserts.length > 0}
                                                                {#each inserts as insert (insert.id)}
                                                                    <div class="ch-fx-summary-row" class:bypassed={insert.bypassed}>
                                                                        <button class="ch-fx-summary-name"
                                                                            onclick={() => dispatchCpp("toggleGroupBusInsertEditor", chairBus.id, insert.id)}
                                                                            disabled={insert.status !== "loaded"}
                                                                            aria-label={`${insert.editorOpen ? "Hide" : "Edit"} ${insert.name || "audio effect"}`}
                                                                            aria-pressed={Boolean(insert.editorOpen)}
                                                                            title={`${insert.editorOpen ? "Hide" : "Edit"} ${insert.name || "Unavailable plug-in"}`}>
                                                                            <strong>{insert.name || "Unavailable plug-in"}</strong>
                                                                        </button>
                                                                        <div class="ch-fx-summary-actions">
                                                                            <button
                                                                                class:active={insert.bypassed}
                                                                                onclick={() => dispatchCpp("setGroupBusInsertBypassed", chairBus.id, insert.id, !insert.bypassed)}
                                                                                title={insert.bypassed ? `Enable ${insert.name}` : `Bypass ${insert.name}`}
                                                                            >{insert.bypassed ? "Enable" : "Bypass"}</button>
                                                                        </div>
                                                                    </div>
                                                                {/each}
                                                            {:else}
                                                                <div class="ch-fx-empty">No inserts</div>
                                                            {/if}
                                                        </div>
                                                        {/if}
                                                        {#if sendRows[location] > 0}
                                                            <PromotedSends chair={chairBus} chairName={chairLabel} buses={groupBuses}
                                                                horizontal={stripSize === "large"}
                                                                preFader={location === "Pre"} rows={sendRows[location]} />
                                                        {/if}
                                                        </section>
                                                    {/each}
                                                </div>

                                                <BalanceControl value={chairBus.audio?.balance ?? 0}
                                                    label={`${chairLabel} chair stereo balance`}
                                                    command="setGroupBusBalance" id={chairBus.id} />

                                                <div class="ch-fader">
                                                    <span class="fader-tick fader-top">+6</span>
                                                    <div class="fader-meter-row">
                                                        <div class="fader-track">
                                                            <input
                                                                class="fader-slider chair-slider"
                                                                type="range"
                                                                min="0"
                                                                max="1000"
                                                                step="1"
                                                                aria-label={`${chairLabel} chair level`}
                                                                value={getFaderPos(faderKey, chairFaderPos)}
                                                                onpointerdown={() => { faderAnchor[faderKey] = getFaderPos(faderKey, chairFaderPos); draggingChairId = chairBus.id; }}
                                                                oninput={(e) => {
                                                                    faderDragging.add(faderKey);
                                                                    const rawPos = parseFloat(/** @type {HTMLInputElement} */ (e.target).value);
                                                                    const effectivePos = fineFaderPos(faderKey, rawPos, e.shiftKey);
                                                                    snapFaderTo(faderKey, effectivePos);
                                                                    setChairGain(chairBus, Math.round(posToDB(effectivePos / 1000) * 10) / 10);
                                                                }}
                                                                onpointerup={() => { faderDragging.delete(faderKey); delete faderAnchor[faderKey]; draggingChairId = ""; }}
                                                                onpointercancel={() => { faderDragging.delete(faderKey); draggingChairId = ""; }}
                                                                onkeyup={() => { faderDragging.delete(faderKey); }}
                                                                onblur={() => { faderDragging.delete(faderKey); draggingChairId = ""; }}
                                                                onpointerleave={() => { faderDragging.delete(faderKey); delete faderAnchor[faderKey]; }}
                                                                ondblclick={() => setChairGain(chairBus, 0)}
                                                            />
                                                        </div>
                                                        <StereoMeter levels={meters.stereoChairs[chairBus.id]}
                                                            label={`${chairLabel} chair`} kind="bus" id={chairBus.id} />
                                                    </div>
                                                    {#if multiLayer}
                                                        <!-- Σ: the layers' combined (blend) level, before the chair fader. -->
                                                        <span class="fader-tick fader-bot blend-level" class:off-target={offTarget}
                                                            title={offTarget
                                                                ? `Blend level ${formatDb(blendDb)} dB can't reach its locked level ${formatDb(blendTargetDb)} dB (a gain estimate, not measured loudness)`
                                                                : locked ? `Blend level, kept at ${formatDb(blendTargetDb)} dB` : "Blend level: the layers' combined gain, before the chair fader"}
                                                        >Σ {formatDb(blendDb)}</span>
                                                    {:else}
                                                        <span class="fader-tick fader-bot">-∞</span>
                                                    {/if}
                                                    <div class="ch-chair-gain-row">
                                                        <input
                                                            class="fader-value"
                                                            type="number"
                                                            min="-120"
                                                            max="6"
                                                            step="0.1"
                                                            aria-label={`${chairLabel} chair level in decibels`}
                                                            value={Math.round(chairGainDb(chairBus) * 10) / 10}
                                                            onchange={(e) => {
                                                                const value = parseFloat(/** @type {HTMLInputElement} */ (e.target).value);
                                                                setChairGain(chairBus, Number.isFinite(value) ? value : 0);
                                                            }}
                                                        />
                                                        {#if multiLayer}
                                                            <button
                                                                class="sum-lock-btn"
                                                                class:sum-lock-active={locked}
                                                                aria-pressed={locked}
                                                                aria-label={`Keep blend level for ${chairLabel}`}
                                                                title={locked
                                                                    ? "Keep blend level is on: changing one layer adjusts the others, so the layers' combined level stays put. Click to turn off."
                                                                    : "Keep blend level: hold the layers' combined level, so changing one layer adjusts the others"}
                                                                onclick={() => dispatchCpp("setChairLevelLock", instrGroup.key, !locked)}
                                                            >{locked ? '🔒' : '🔓'}</button>
                                                        {/if}
                                                    </div>
                                                </div>

                                                <div class="ch-mute-solo">
                                                    <button
                                                        class="ms-btn mute-btn"
                                                        class:active={chairBus.muted}
                                                        aria-label={`${chairBus.muted ? "Unmute" : "Mute"} ${chairLabel} chair`}
                                                        title={chairBus.muted ? "Unmute the chair" : "Mute the chair"}
                                                        onclick={() => dispatchCpp("setGroupBusMute", chairBus.id, !chairBus.muted)}
                                                        >M</button
                                                    >
                                                    <button
                                                        class="ms-btn solo-btn"
                                                        class:active={chairBus.soloed}
                                                        aria-label={`${chairBus.soloed ? "Unsolo" : "Solo"} ${chairLabel} chair`}
                                                        title={chairBus.soloed ? "Unsolo the chair" : "Solo the chair"}
                                                        onclick={() => dispatchCpp("setGroupBusSolo", chairBus.id, !chairBus.soloed)}
                                                        >S</button
                                                    >
                                                </div>

                                                <!-- Layers have Edit details here; the chair's output is in the header. -->
                                                <div class="ch-chair-bottom-blank" aria-hidden="true"></div>
                                                <!-- svelte-ignore a11y_click_events_have_key_events -->
                                                <!-- svelte-ignore a11y_no_static_element_interactions -->
                                                <div
                                                    class="select-bar select-bar-bottom"
                                                    style="background: {group.colors.accent};"
                                                    onclick={(e) => {
                                                        e.stopPropagation();
                                                        handleStripClick(chairBus.id, e);
                                                    }}
                                                ></div>
                                            </div>
                                        {/if}
                                    </div> <!-- /strips-row -->
                                    </div> <!-- /inst-body -->
                                </div> <!-- /inst-group -->
{/snippet}


<div
    class="mixer-container"
    class:compact-strips={stripSize === "compact"}
    style="--strip-width: {stripWidth}px; --strip-control-height: 32px; --strip-pre-fader-height: {stripSize === "large" ? 306 : 236}px; --strip-fx-height: {stripSize === "large" ? 150 : 80}px; --strip-send-row-height: {sendRowHeight}px; --strip-pre-send-span: {sendSpan(sendRows.Pre)}px; --strip-post-send-span: {sendSpan(sendRows.Post)}px; --strip-send-span: {sendSpan(sendRows.Pre) + sendSpan(sendRows.Post)}px;"
>
    {#if layerRefreshResult}
        <div class="layer-refresh-notice" role="status">{layerRefreshResult}</div>
    {/if}
    <div class="mixer-toolbar">
        <div class="toolbar-left">
            <BranchSelector
                {branches}
                {currentBranch}
                onCheckout={(id) => dispatchCpp("checkoutBranch", id)}
                onCreateBranch={(name) => dispatchCpp("createBranch", name)}
            />
            <button class="toolbar-btn" disabled={!undoState.canUndo}
                title={undoState.undoDescription ? `Undo: ${undoState.undoDescription}` : "Nothing to undo"}
                onclick={() => dispatchCpp("undo")}>Undo</button>
            <button class="toolbar-btn" disabled={!undoState.canRedo}
                title={undoState.redoDescription ? `Redo: ${undoState.redoDescription}` : "Nothing to redo"}
                onclick={() => dispatchCpp("redo")}>Redo</button>
            <button class="toolbar-btn save-btn" onclick={doSaveConfig}
                disabled={saveButton.disabled} title={saveButton.title}>Save</button>
            {#if selectedIds.size > 1}
                <span class="selection-badge">{selectedIds.size} selected</span>
            {/if}
            {#if anySoloed || anyBusSoloed || anyChairSoloed}
                <button class="toolbar-btn solo-clear" onclick={clearSolos}>Clear Solos</button>
            {/if}
        </div>
        <div class="toolbar-right">
            <AudioDiagnostics />
            <ToolbarMenu label="View">
                {#snippet children(close)}
                    <div class="view-group">
                        <span class="view-group-title">Windows</span>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "print"); }}>Print Audio…</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "chairs"); }}>Chairs…</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "settings"); }}>Settings…</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "library-manager"); }}>Library Manager</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "score-tests"); }}>Score Tests…</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "project-history"); }}>Project History</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "audio-performance"); }}>Audio Performance</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "plugins"); }}>Plug-ins…</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "timeline"); }}>Timeline</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "event-log"); }}>Event Log</button>
                        <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "midi-capture"); }}>MIDI Capture</button>
                    </div>
                    <span class="view-group-title">Mixer layout</span>
                    <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "inspector"); }}>Layer Inspector</button>
                    <button class="view-item" onclick={() => { close(); dispatchCpp("showView", "audio-bank"); }}>Audio Buses &amp; Master</button>
                    <div class="display-controls">
                        <label class="display-control-label" for="strip-size-select">Strip size</label>
                        <select id="strip-size-select" class="toolbar-select" value={stripSize}
                            onchange={(event) => updateStripSize(event.currentTarget.value)}>
                            {#each Object.entries(STRIP_SIZE_PRESETS) as [value, preset]}
                                <option {value}>{preset.label}</option>
                            {/each}
                        </select>
                    </div>
                    <div class="display-controls">
                        <span class="display-control-label">Zoom</span>
                        <div class="zoom-control" aria-label="Interface zoom">
                            <button class="zoom-btn" onclick={onZoomOut} aria-label="Zoom out" title="Zoom out (Command-minus)">−</button>
                            <button class="zoom-value" onclick={onResetZoom} aria-label="Reset zoom" title="Reset zoom (Command-0)">{Math.round(uiZoom * 100)}%</button>
                            <button class="zoom-btn" onclick={onZoomIn} aria-label="Zoom in" title="Zoom in (Command-plus)">+</button>
                        </div>
                    </div>
                    <label class="library-bar-option">
                        <input type="checkbox" checked={showLibraryBar}
                            onchange={(event) => { showLibraryBar = writeLibraryBarVisible(window.localStorage, event.currentTarget.checked); }} />
                        Show library bar
                    </label>
                {/snippet}
            </ToolbarMenu>
        </div>
    </div>

    {#if showLibraryBar && uniqueLibraries.length > 0}
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
    {#if strips.length === 0 && chairs.length === 0}
        <div class="empty-state">
            <p>
                No chairs yet. Use <strong>Manage Chairs</strong> to define the
                instruments Dorico should address.
            </p>
        </div>
    {:else}
        <div class="mixer-strips-area">
        <!-- svelte-ignore a11y_click_events_have_key_events -->
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <div class="console" class:has-kits={consoleHasKits} onclick={clearSelection}>
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
                            {#each group.columns as column (column.kitId || column.instrGroup.key)}
                                {#if column.kitId}
                                    <div class="kit-block" style="--accent: {group.colors.accent};" role="group" aria-label={`Kit ${column.kitName}`}>
                                        <div class="kit-header" title={`${column.kitName}: a percussion kit, each instrument on its own chair`}>
                                            <span class="kit-header-label">{column.kitName}</span>
                                            <span class="kit-header-count">Kit · {column.instrGroups.length}</span>
                                        </div>
                                        <div class="kit-chairs">
                                            {#each column.instrGroups as instrGroup (instrGroup.key)}
                                                {@render chairColumn(instrGroup, group)}
                                            {/each}
                                        </div>
                                    </div>
                                {:else}
                                    {@render chairColumn(column.instrGroup, group)}
                                {/if}
                            {/each}
                        </div>
                    {/if}
                </div>
            {/each}
            <AudioBusBank
                buses={groupBuses}
                {chairBuses}
                master={masterAudio}
                busMeters={meters.stereoBuses}
                masterMeters={meters.master}
                masterLoudness={meters.loudness}
                onAddBus={() => { addBusOpen = true; }}
                onEditBus={(busId) => { busDetailsId = busId; }}
                onOpenBusAudio={(busId) => { busAudioBusId = busId; }}
                onOpenMasterAudio={() => { masterAudioOpen = true; }}
                revealRequest={audioBankReveal}
            />
        </div>
        </div> <!-- /mixer-strips-area -->
    {/if}

    {#if inspectorOpen}
        <!-- svelte-ignore a11y_no_static_element_interactions -->
        <div
            class="inspector-resize-handle"
            onmousedown={onResizeMouseDown}
        ></div>
        <div class="inspector-panel" style="width: {inspectorWidth}px;">
            <div class="inspector-panel-header">
                <span class="inspector-panel-title">Note Inspector</span>
                <button class="inspector-close-btn" aria-label="Close inspector" onclick={() => { inspectorOpen = false; }}>✕</button>
            </div>
            {#if inspectorStripId}
                <NoteInspector stripId={inspectorStripId} />
            {:else}
                <div class="inspector-empty">Select a layer to inspect its incoming MIDI.</div>
            {/if}
        </div>
    {/if}
    </div> <!-- /mixer-body -->

    {#if masterAudioOpen}
        <MasterAudioPanel
            loudness={meters.loudness}
            master={masterAudio}
            peakDb={meters.masterPeakDb}
            plugins={scannedPlugins}
            onClose={() => { masterAudioOpen = false; }}
        />
    {/if}
    {#if channelAudioStripId}
        {@const channelStrip = strips.find((strip) => strip.id === channelAudioStripId)}
        {#if channelStrip}
            <ChannelAudioPanel
                strip={channelStrip}
                audio={channelAudio || channelStrip.audio}
                instrumentName={getPluginName(channelStrip)}
                plugins={scannedPlugins}
                onOpenChairAudio={chairBusById.has(channelStrip.chairBusId)
                    ? () => { chairAudioBusId = channelStrip.chairBusId; channelAudioStripId = ""; channelAudio = null; }
                    : null}
                onClose={() => { channelAudioStripId = ""; channelAudio = null; }}
            />
        {/if}
    {/if}
    {#if busAudioBusId}
        {@const audioBus = groupBuses.find((bus) => bus.id === busAudioBusId)}
        {#if audioBus}
            <BusAudioPanel
                bus={audioBus}
                plugins={scannedPlugins}
                onClose={() => { busAudioBusId = ""; }}
            />
        {/if}
    {/if}
    {#if chairAudioBusId}
        {@const chairAudioBus = chairBusById.get(chairAudioBusId)}
        {#if chairAudioBus}
            <BusAudioPanel
                bus={chairAudioBus}
                buses={groupBuses}
                chairName={chairNameOf(chairAudioBus.id)}
                plugins={scannedPlugins}
                onClose={() => { chairAudioBusId = ""; }}
            />
        {/if}
    {/if}
    {#if addBusOpen}
        <AddBusDialog
            buses={groupBuses}
            selectedCount={selectedChairs().length}
            onAdd={(name, routeSelection) => {
                dispatchCpp("addGroupBus", name, JSON.stringify(routeSelection ? selectedChairs() : []));
                addBusOpen = false;
            }}
            onClose={() => { addBusOpen = false; }}
        />
    {/if}
    {#if busDetailsId}
        {@const detailsBus = groupBuses.find((bus) => bus.id === busDetailsId)}
        {#if detailsBus}
            <BusDetailsPanel
                bus={detailsBus}
                routedCount={chairBuses.filter((chair) => chair.directOutputBusId === detailsBus.id).length}
                sendCount={chairBuses.reduce((count, chair) => count + (chair.sends || []).filter((send) => send.busId === detailsBus.id).length, 0)}
                onRename={(name) => dispatchCpp("renameGroupBus", detailsBus.id, name)}
                onDelete={() => {
                    dispatchCpp("removeGroupBus", detailsBus.id);
                    busDetailsId = "";
                }}
                onClose={() => { busDetailsId = ""; }}
            />
        {/if}
    {/if}
    {#if layerDetailsStripId}
        {@const detailsStrip = strips.find((strip) => strip.id === layerDetailsStripId)}
        {#if detailsStrip}
            <LayerDetailsPanel
                strip={detailsStrip}
                pluginName={getPluginName(detailsStrip)}
                plugins={scannedPlugins}
                {libraryMaps}
                luaCatalog={luaPluginCatalog}
                inspectorOpen={inspectorOpen && inspectorStripId === detailsStrip.id}
                onClose={() => { layerDetailsStripId = ""; }}
                onSetPlugin={(uid) => setPlugin(detailsStrip.id, uid)}
                onShowInstrument={() => showEditor(detailsStrip.id)}
                onSetProgram={(index) => dispatchCpp("setStripProgram", detailsStrip.id, index)}
                onSelectMap={(entityID) => loadExpressionMap(detailsStrip.id, entityID)}
                onClearMap={() => clearExpressionMap(detailsStrip.id)}
                onLoadMapFile={() => loadExpressionMapFromFile(detailsStrip.id)}
                onSelectPercussionMap={(entityId) => dispatchCpp("loadPercussionMap", detailsStrip.id, entityId)}
                onClearPercussionMap={() => dispatchCpp("clearPercussionMap", detailsStrip.id)}
                onLoadPercussionMapFile={() => dispatchCpp("loadPercussionMapFromFile", detailsStrip.id)}
                onAddLua={(filePath) => addLuaPlugin(detailsStrip.id, filePath)}
                onRemoveLua={(index) => removeLuaPlugin(detailsStrip.id, index)}
                onInspectMidi={() => {
                    toggleInspector(detailsStrip.id);
                    layerDetailsStripId = "";
                }}
                onRefreshLibrary={() => refreshLayerFromLibrary(detailsStrip)}
                onDelete={() => {
                    removeStrip(detailsStrip.id);
                    layerDetailsStripId = "";
                }}
                linkedChairCount={linkedIds(detailsStrip.id).length}
                kitName={chairs.find((chair) => chair.id === detailsStrip.chairId)?.kitName || ""}
                onDeleteThisChair={() => {
                    removeStrip(detailsStrip.id, true);
                    layerDetailsStripId = "";
                }}
            />
        {/if}
    {/if}
    {#if layerPickerChair}
        <LayerPicker
            chair={layerPickerChair}
            kitChairCount={layerPickerChair.kitId ? chairs.filter((chair) => chair.kitId === layerPickerChair.kitId).length : 0}
            patches={layerCatalog}
            onChoose={(patchId, kit) => {
                dispatchCpp("assignPatchToChair", kit ? { chairId: layerPickerChair.id, patchId, kit: true }
                    : { chairId: layerPickerChair.id, patchId });
                layerPickerChair = null;
            }}
            onClose={() => { layerPickerChair = null; }}
        />
    {/if}
</div>



<style>
    .mixer-container {
        position: relative;
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

    .layer-refresh-notice {
        position: absolute;
        z-index: 30;
        top: 54px;
        right: 18px;
        max-width: min(360px, calc(100% - 36px));
        padding: 9px 13px;
        border: 1px solid #34d399;
        border-radius: 6px;
        background: #0f2f2b;
        color: #d1fae5;
        box-shadow: 0 8px 24px rgba(0, 0, 0, 0.35);
        font-size: 0.75rem;
        font-weight: 600;
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

    .inspector-empty {
        padding: 24px 16px;
        color: #94a3b8;
        font-size: 0.8rem;
        line-height: 1.5;
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
        flex-wrap: nowrap;
        gap: 12px;
        padding: 6px 12px;
        flex-shrink: 0;
        border-bottom: 1px solid #1e293b;
        /* Menus such as the branch selector extend below this row. Keeping
           toolbar overflow visible prevents those popovers being clipped. */
        overflow: visible;
        position: relative;
        z-index: 20;
    }
    .toolbar-left {
        display: flex;
        align-items: center;
        gap: 8px;
        min-width: 0;
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

    .toolbar-right {
        display: flex;
        align-items: center;
        gap: 8px;
        flex-shrink: 0;
    }

    .display-controls,
    .zoom-control {
        display: flex;
        align-items: center;
        gap: 6px;
    }
    .display-controls { justify-content: space-between; }
    .library-bar-option { display: flex; align-items: center; gap: 8px; color: #cbd5e1; font-size: .8rem; cursor: pointer; }
    .view-group { display: flex; flex-direction: column; gap: 2px; padding-bottom: 8px; border-bottom: 1px solid #334155; }
    .view-group-title { color: #64748b; font-size: .65rem; font-weight: 700; letter-spacing: .08em; text-transform: uppercase; }
    .view-item { padding: 5px 8px; border: 0; border-radius: 4px; background: transparent; color: #e2e8f0; font: inherit; font-size: .8rem; text-align: left; cursor: pointer; white-space: nowrap; }
    .view-item:hover, .view-item:focus-visible { background: #1e293b; outline: 1px solid #64748b; }
    .solo-clear { color: #4ade80; border-color: #166534; }
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
        flex-shrink: 0;
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
    /* A percussion kit: its chairs under one header. Other chairs drop by the
       header's height so every fader lines up. */
    .kit-block {
        display: flex;
        flex-direction: column;
        flex: 0 0 auto;
        min-height: 0;
    }
    .kit-header {
        contain: inline-size;
        height: 18px;
        display: flex;
        align-items: center;
        gap: 6px;
        padding: 0 6px;
        border-left: 2px solid var(--accent, #334155);
        margin-left: -1px;
        border-bottom: 1px solid var(--accent, #334155);
        background: rgba(255,255,255,0.05);
        overflow: hidden;
        white-space: nowrap;
        font-size: 0.68rem;
        flex-shrink: 0;
    }
    .kit-header-label { min-width: 0; overflow: hidden; text-overflow: ellipsis; font-weight: 700; color: #e2e8f0; }
    .kit-header-count { flex-shrink: 0; color: #94a3b8; }
    .kit-chairs {
        display: flex;
        flex-direction: row;
        flex: 1;
        min-height: 0;
        align-items: stretch;
        gap: 3px;
    }
    .console.has-kits .folder-strips > .inst-group { margin-top: 19px; }
    .inst-group {
        display: flex;
        flex-direction: column;
        flex: 0 0 auto;
        min-width: 0;
        min-height: 0;
        border-left: 2px solid var(--accent, #334155);
        margin-left: -1px; /* center the 2px divider in the 1px+2px gap between groups */
    }
    /* Row below the bridge-header: the layers, then the chair strip */
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
        flex: 0 0 auto;
        min-height: 0;
    }
    .empty-chair {
        width: var(--strip-width, 120px);
        min-width: var(--strip-width, 120px);
        align-self: stretch;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 12px;
        padding: 24px 8px;
        box-sizing: content-box; /* same outer width as one channel-strip */
        text-align: center;
        overflow-wrap: anywhere;
        font-size: 0.78rem;
        color: #64748b;
        background: rgba(15, 23, 42, 0.55);
        border-right: 1px solid #1e293b;
    }
    .empty-chair button {
        width: 100%;
        min-width: 0;
        box-sizing: border-box;
        min-height: 36px;
        padding: 7px 4px;
        border: 1px solid rgba(52, 211, 153, 0.55);
        border-radius: 6px;
        background: rgba(20, 83, 45, 0.45);
        color: #a7f3d0;
        cursor: pointer;
        font: inherit;
        font-size: 0.78rem;
        font-weight: 650;
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

    /* ── Chair strip: the chair bus, to the right of its layers ── */
    .channel-strip.chair-strip {
        background: color-mix(in srgb, var(--accent, #334155) 9%, #0c1422);
        border-left: 1px solid color-mix(in srgb, var(--accent, #334155) 40%, transparent);
        margin-left: -1px; /* the same outer width as a layer strip */
    }
    .channel-strip.chair-strip:hover {
        background: color-mix(in srgb, var(--accent, #334155) 12%, #101a2c);
    }
    .channel-strip.chair-strip.selected {
        background: color-mix(in srgb, var(--accent, #334155) 14%, #1e293b);
    }
    .ch-chair-kind {
        color: color-mix(in srgb, var(--accent, #94a3b8) 75%, #cbd5e1);
        font-size: 0.62rem;
        font-weight: 700;
        letter-spacing: 0.08em;
        text-transform: uppercase;
    }
    /* Holds the layers' instrument row height, so every row lines up. */
    .ch-chair-blank {
        flex-shrink: 0;
    }
    input.fader-slider.chair-slider {
        accent-color: #d4a017;
    }
    /* Σ replaces the -∞ tick: the same line, so the faders stay aligned. */
    .fader-tick.blend-level {
        color: #94a3b8;
        white-space: nowrap;
    }
    .fader-tick.blend-level.off-target {
        color: #fbbf24;
        font-weight: 700;
    }
    .ch-chair-gain-row {
        display: flex;
        width: 100%;
        gap: 4px;
        flex-shrink: 0;
    }
    .ch-chair-gain-row .fader-value {
        flex: 1;
        min-width: 0;
    }
    .compact-strips .ch-chair-gain-row .fader-value {
        padding: 3px 1px;
        font-size: 0.68rem;
    }
    .ch-chair-gain-row .sum-lock-btn {
        min-width: 26px;
        min-height: 28px;
        padding: 2px;
    }
    /* Same height as the layers' Edit details, so the faders line up. */
    .ch-chair-bottom-blank {
        height: 32px;
        min-height: 32px;
        flex-shrink: 0;
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

    /* Instrument group bridge header */
    .bridge-header {
        /* Only the strip body determines chair width. Header labels/buttons
           must not contribute an intrinsic minimum width to the chair. */
        contain: inline-size;
        display: grid;
        grid-template-columns: minmax(0, 1fr) auto;
        grid-template-rows: 20px 28px;
        align-items: center;
        gap: 4px;
        padding: 4px;
        border-bottom: 2px solid var(--accent, #3b82f6);
        background: rgba(0,0,0,0.25);
        flex-shrink: 0;
        overflow: hidden;
    }
    .bridge-identity {
        grid-column: 1 / -1;
        min-width: 0;
        display: flex;
        align-items: center;
        gap: 4px;
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
        min-width: 0;
        flex: 1;
    }
    .bridge-port {
        font-size: 0.65rem;
        color: #94a3b8;
        white-space: nowrap;
        flex-shrink: 0;
    }
    .bridge-output {
        grid-column: 1;
        grid-row: 2;
        justify-self: start;
        width: 100%;
        min-width: 0;
        /* As wide as in a one-layer chair's header (strip + its padding, less
           the header padding, gap and "+"), however many layers the chair has. */
        max-width: calc(var(--strip-width, 120px) - 23px);
        height: 28px;
        box-sizing: border-box;
        overflow: hidden;
        /* A slim caret instead of the native arrow leaves room for "Master"
           in a one-layer chair's compact header. */
        appearance: none;
        -webkit-appearance: none;
        padding: 2px 11px 2px 4px;
        border: 1px solid #3d4c62;
        border-radius: 4px;
        background: #111c2e url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='7' height='4'%3E%3Cpath d='M0 0h7L3.5 4z' fill='%2394a3b8'/%3E%3C/svg%3E") no-repeat right 3px center;
        color: #cbd5e1;
        font: inherit;
        font-size: 0.7rem;
        font-weight: 600;
        white-space: nowrap;
        text-overflow: ellipsis;
        cursor: pointer;
    }
    .bridge-output:hover {
        border-color: #7dd3fc;
        color: #e0f2fe;
    }
    .bridge-add-layer {
        grid-column: 2;
        grid-row: 2;
        width: 28px;
        min-height: 28px;
        padding: 0;
        border: 1px solid rgba(52, 211, 153, 0.55);
        border-radius: 5px;
        background: rgba(20, 83, 45, 0.5);
        color: #a7f3d0;
        cursor: pointer;
        font: inherit;
        font-size: 1rem;
        line-height: 1;
        font-weight: 650;
        white-space: nowrap;
    }
    .bridge-add-layer:hover {
        border-color: #6ee7b7;
        background: rgba(6, 95, 70, 0.68);
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

    .ch-strip-section {
        margin: 0 -4px;
        padding: 6px 4px;
        border-radius: 5px;
        background: #1b283b;
        box-sizing: border-box;
        flex-shrink: 0;
    }
    .ch-section-title {
        display: flex;
        align-items: center;
        height: 20px;
        min-width: 0;
        flex-shrink: 0;
        color: #a3b4c8;
        font-size: 0.6875rem;
        font-weight: 600;
        letter-spacing: 0.015em;
        text-transform: uppercase;
        white-space: nowrap;
    }
    .compact-strips .ch-section-title { font-size: 0.6rem; }
    .ch-section-heading {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 2px;
        height: 24px;
        flex-shrink: 0;
    }
    .ch-instrument-section { height: 44px; display: flex; flex-direction: column; gap: 4px; }
    .ch-audio-fx {
        min-width: 22px;
        height: 24px;
        min-height: 24px;
        box-sizing: border-box;
        display: flex;
        align-items: center;
        justify-content: center;
        gap: 2px;
        position: relative;
        padding: 2px;
        border: 0;
        border-radius: 4px;
        background: transparent;
        color: #a3b4c8;
        cursor: pointer;
        flex-shrink: 0;
    }
    .ch-audio-fx svg { flex-shrink: 0; }
    .ch-insert-count {
        padding: 1px 3px;
        border-radius: 3px;
        background: #28374a;
        color: #a3b4c8;
        font-size: 0.5625rem;
        line-height: 14px;
        font-weight: 600;
        white-space: nowrap;
        font-variant-numeric: tabular-nums;
    }
    .ch-insert-count.active { background: #21435a; color: #bae6fd; }
    /* Corner badge uses the existing icon footprint on the narrowest strips. */
    .compact-strips .ch-insert-count {
        position: absolute;
        top: -3px;
        right: -3px;
        padding: 0 2px;
        min-width: 12px;
        text-align: center;
        font-size: 0.5rem;
        line-height: 12px;
    }
    .ch-audio-fx:hover, .ch-audio-fx.has-effects {
        color: #bae6fd;
    }
    .ch-audio-fx:hover { background: #2b4059; }
    .ch-strip-identity {
        display: flex;
        height: 38px;
        min-width: 0;
        flex-shrink: 0;
        flex-direction: column;
        justify-content: center;
        gap: 2px;
        text-align: center;
    }
    .ch-strip-identity strong {
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        color: #cbd5e1;
        font-size: 0.73rem;
    }
    .ch-instrument-summary {
        display: flex;
        height: var(--strip-control-height, 32px);
        min-width: 0;
        flex-shrink: 0;
    }
    .ch-instrument-summary button,
    .ch-instrument-summary select {
        width: 100%;
        min-width: 0;
        height: 100%;
        box-sizing: border-box;
        padding: 4px 6px;
        border: 1px solid #3d4c62;
        border-radius: 5px;
        background: #172337;
        color: #cbd5e1;
        font-size: 0.7rem;
        font-weight: 600;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        cursor: pointer;
    }
    .ch-instrument-summary button:hover,
    .ch-instrument-summary select:hover {
        border-color: #7dd3fc;
        color: #e0f2fe;
    }
    .ch-fx-zone {
        display: flex;
        height: calc(var(--strip-fx-height) + var(--strip-send-span, 0px));
        box-sizing: border-box;
        flex-shrink: 0;
        flex-direction: column;
        gap: 8px;
    }
    .ch-fx-section {
        display: flex;
        flex: 1;
        flex-basis: var(--strip-pre-send-span, 0px);
        min-height: 0;
        flex-direction: column;
        gap: 5px;
    }
    .ch-fx-section.post-fader { flex-basis: var(--strip-post-send-span, 0px); }
    .ch-fx-summary {
        display: flex;
        min-height: 0;
        flex: 1;
        flex-direction: column;
        gap: 5px;
        overflow-y: auto;
    }
    .ch-fx-empty {
        display: flex;
        align-items: center;
        height: 30px;
        color: #a3b4c8;
        font-size: 0.68rem;
    }
    .ch-fx-summary-row {
        display: flex;
        gap: 3px;
        height: 30px;
        flex-shrink: 0;
    }
    .ch-fx-summary-row.bypassed {
        opacity: 0.62;
    }
    .ch-fx-summary-name {
        display: flex;
        align-items: center;
        flex: 1;
        min-width: 0;
        padding: 3px 5px;
        border: 1px solid #3d4c62;
        border-radius: 4px;
        background: #111c2e;
        color: #e2e8f0;
        line-height: 14px;
        text-align: left;
        cursor: pointer;
    }
    .ch-fx-summary-name:hover:not(:disabled), .ch-fx-summary-name[aria-pressed="true"] { border-color: #7dd3fc; }
    .ch-fx-summary-name:disabled { opacity: 0.5; cursor: default; }
    .ch-fx-summary-name strong {
        min-width: 0;
        overflow: hidden;
        color: #e2e8f0;
        font-size: 0.72rem;
        font-weight: 600;
        text-overflow: ellipsis;
        white-space: nowrap;
    }
    .ch-fx-summary-actions {
        display: flex;
        flex-shrink: 0;
    }
    .ch-fx-summary-actions button {
        min-width: 0;
        min-height: 30px;
        padding: 2px 5px;
        line-height: 16px;
        border: 1px solid #3d4c62;
        border-radius: 4px;
        background: #172337;
        color: #cbd5e1;
        font-size: 0.68rem;
        font-weight: 600;
        cursor: pointer;
    }
    .ch-fx-summary-actions button:hover:not(:disabled) {
        border-color: #7dd3fc;
        color: #e0f2fe;
    }
    .ch-fx-summary-actions button.active {
        border-color: #f59e0b;
        color: #fbbf24;
    }
    .ch-fx-summary-actions button:disabled {
        opacity: 0.38;
        cursor: default;
    }
    .ch-details-btn {
        width: 100%;
        height: 32px;
        min-height: 32px;
        padding: 5px 8px;
        flex-shrink: 0;
        border: 1px solid #334155;
        border-radius: 4px;
        background: #0f172a;
        color: #cbd5e1;
        font-size: 0.72rem;
        font-weight: 600;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
        cursor: pointer;
    }
    .ch-details-btn:hover {
        border-color: #7dd3fc;
        color: #e0f2fe;
    }
    .ch-details-btn.attention {
        border-color: #f59e0b;
        color: #fbbf24;
    }
</style>
