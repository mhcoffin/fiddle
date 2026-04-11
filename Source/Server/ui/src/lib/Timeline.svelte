<script>
    import { tick } from "svelte";
    import { FAMILY_ORDER, canonicalFamily } from "./orchestralOrder.js";

    let {
        noteHistory = [],
        heartbeat = 0,
        firstSample = 0,
        bpm = 120,              // live BPM from Dorico (via setTempo)
        tempoChanges = [],      // [{bpm, samplePosition, timeSigNumerator, timeSigDenominator, source}]
        channelInstruments = {},
        instrumentMap = {},
        metronomeActive = false, // true when tempo is derived from click track
        metronomeBeats = [],    // [{samplePosition, isDownbeat}] actual beat positions
        onHover,
        onLeave,
        onTempoChange,          // callback: (newTempoChanges) => void
    } = $props();

    // Constants for time-to-pixel conversion
    const sampleRate = 44100;
    let samplesPerBeat = $derived(sampleRate * (60 / bpm));
    const rowHeight = 40;

    // Layout constants
    const LABEL_WIDTH = 180; // channel labels column width (sticky left)
    const CONTENT_PAD = 20; // extra padding before beat 0

    // getX offset: positions elements within beats-track / notes-layer
    const GET_X_OFFSET = LABEL_WIDTH + CONTENT_PAD; // 200

    // Anchor offset: total non-scaling prefix in scroll-viewport content coords.
    // The beats-track/grid-layer start at LABEL_WIDTH (180px) in the flex layout,
    // and getX adds GET_X_OFFSET (200px) within those containers.
    const ANCHOR_FIXED = LABEL_WIDTH + GET_X_OFFSET; // 380

    // Zoom state — pixelsPerBeat is now reactive
    const MIN_ZOOM = 20;
    const MAX_ZOOM = 800;
    const DEFAULT_ZOOM = 150;
    const SENSITIVITY = 0.005; // pixels of drag per zoom doubling

    let pixelsPerBeat = $state(DEFAULT_ZOOM);
    let pixelsPerSample = $derived(pixelsPerBeat / samplesPerBeat);

    // Zoom drag state
    let isDragging = $state(false);
    let dragStartY = 0;
    let dragInitialZoom = 0;
    let initialScrollLeft = 0; // scrollLeft at drag start
    let mouseViewportX = 0; // mouse X relative to the scroll viewport
    let scrollViewport = $state(); // bound to the .scroll-viewport div

    function onHeaderMouseDown(e) {
        // Only left-click
        if (e.button !== 0) return;
        e.preventDefault();

        isDragging = true;
        dragStartY = e.clientY;
        dragInitialZoom = pixelsPerBeat;

        // Capture the anchor point in pixel space
        if (scrollViewport) {
            const rect = scrollViewport.getBoundingClientRect();
            mouseViewportX = e.clientX - rect.left;
            initialScrollLeft = scrollViewport.scrollLeft;
        }

        // Attach window-level listeners for move and release
        window.addEventListener("mousemove", onDragMove);
        window.addEventListener("mouseup", onDragEnd);
        document.body.classList.add("is-zooming");
    }

    async function onDragMove(e) {
        const deltaY = e.clientY - dragStartY;
        // Drag down (positive deltaY) = zoom in (increase pixelsPerBeat)
        // Use exponential scaling for smooth, proportional feel
        const factor = Math.pow(2, deltaY * SENSITIVITY);
        const newZoom = dragInitialZoom * factor;
        pixelsPerBeat = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, newZoom));

        // Wait for Svelte to update the DOM (content width changes with zoom)
        await tick();

        // Anchor scroll: keep the clicked point stationary under the cursor.
        // Pure pixel-space math using the zoom ratio.
        if (scrollViewport) {
            const ratio = pixelsPerBeat / dragInitialZoom;
            // The content-x of the anchor at the old zoom:
            const anchorX = mouseViewportX + initialScrollLeft;
            // Only the portion after ANCHOR_FIXED scales with zoom:
            const newAnchorX = (anchorX - ANCHOR_FIXED) * ratio + ANCHOR_FIXED;
            scrollViewport.scrollLeft = Math.max(
                0,
                newAnchorX - mouseViewportX,
            );
        }
    }

    function onDragEnd() {
        isDragging = false;
        window.removeEventListener("mousemove", onDragMove);
        window.removeEventListener("mouseup", onDragEnd);
        document.body.classList.remove("is-zooming");
    }

    // Derive active rows from notes that have been seen
    let activeRows = $derived.by(() => {
        const seen = new Map();
        for (const note of noteHistory) {
            const port = note.port ?? 0;
            const ch = note.channel;
            const key = `${port}:${ch}`;
            if (!seen.has(key)) {
                // Look up from instrumentMap (from MasterList) first
                const info = instrumentMap[key];
                const name = info?.name || channelInstruments[ch] || `Ch ${ch}`;
                const family = info?.family || "";
                const isSolo = info?.isSolo ?? true;
                seen.set(key, { key, port, channel: ch, name, family, isSolo });
            }
        }
        // Sort by orchestral order, then by name
        return [...seen.values()].sort((a, b) => {
            const fa = FAMILY_ORDER.indexOf(canonicalFamily(a.family));
            const fb = FAMILY_ORDER.indexOf(canonicalFamily(b.family));
            const faIdx = fa >= 0 ? fa : FAMILY_ORDER.length;
            const fbIdx = fb >= 0 ? fb : FAMILY_ORDER.length;
            if (faIdx !== fbIdx) return faIdx - fbIdx;
            return a.name.localeCompare(b.name);
        });
    });

    // Build a quick lookup from "port:channel" → row index
    let rowIndexMap = $derived.by(() => {
        const map = {};
        activeRows.forEach((row, i) => {
            map[row.key] = i;
        });
        return map;
    });

    const currentSessionSamples = $derived(heartbeat * sampleRate);

    const getX = (sample) =>
        (Number(sample) - Number(firstSample)) * pixelsPerSample + GET_X_OFFSET;

    let totalWidth = $derived.by(() => {
        if (noteHistory.length === 0) return 2000;
        const maxScroll = Math.max(
            ...noteHistory.map((n) => {
                const dur =
                    n.durationSamples || currentSessionSamples - n.startSample;
                return getX(Number(n.startSample) + Number(dur > 0 ? dur : 0));
            }),
        );
        return Math.max(2000, maxScroll + 500);
    });

    let beatMarkers = $derived.by(() => {
        // When metronome is active, use actual beat positions from the click track
        if (metronomeActive && metronomeBeats.length > 0) {
            return metronomeBeats.map((b, i) => {
                // Classify by Dorico click pattern:
                //   note 83 = strong (downbeat), note 79 = medium, note 71 = weak
                const prominence = b.noteNumber === 83 ? 'strong'
                                 : b.noteNumber === 79 ? 'medium'
                                 : 'weak';
                return {
                    index: i,
                    samplePosition: b.samplePosition,
                    isBar: b.isDownbeat,
                    prominence,
                };
            });
        }

        // Fallback: compute uniform beat grid from BPM
        const startBeat = Math.floor(firstSample / samplesPerBeat);
        const numBeats = Math.ceil(totalWidth / pixelsPerBeat) + 2;
        return Array.from({ length: numBeats }, (_, i) => ({
            index: startBeat + i,
            samplePosition: (startBeat + i) * samplesPerBeat,
            isBar: (startBeat + i) % 4 === 0,
        }));
    });

    const handleMouseEnter = (event, note) => {
        if (onHover) {
            const rect = event.currentTarget.getBoundingClientRect();
            onHover(rect, note);
        }
    };

    const handleMouseLeave = () => {
        if (onLeave) onLeave();
    };

    // ── Tempo editing ──────────────────────────────────────────────────
    let editingTempoIdx = $state(-1);  // -1 = not editing
    let editBpm = $state("");
    let editTsNum = $state("");
    let editTsDen = $state("");

    function openTempoEditor(idx, e) {
        e.stopPropagation();
        const tc = tempoChanges[idx];
        editingTempoIdx = idx;
        editBpm = String(Math.round(tc.bpm * 10) / 10);
        editTsNum = String(tc.timeSigNumerator ?? 4);
        editTsDen = String(tc.timeSigDenominator ?? 4);
    }

    function commitTempoEdit() {
        if (editingTempoIdx < 0) return;
        const newBpm = parseFloat(editBpm);
        const newNum = parseInt(editTsNum);
        const newDen = parseInt(editTsDen);
        if (isNaN(newBpm) || newBpm <= 0 || isNaN(newNum) || isNaN(newDen) || newNum <= 0 || newDen <= 0) {
            editingTempoIdx = -1;
            return;
        }
        const updated = tempoChanges.map((tc, i) => {
            if (i === editingTempoIdx) {
                return { ...tc, bpm: newBpm, timeSigNumerator: newNum, timeSigDenominator: newDen };
            }
            return tc;
        });
        editingTempoIdx = -1;
        if (onTempoChange) onTempoChange(updated);
    }

    function cancelTempoEdit() {
        editingTempoIdx = -1;
    }

    function handleTempoKeydown(e) {
        if (e.key === "Enter") { e.preventDefault(); commitTempoEdit(); }
        if (e.key === "Escape") { e.preventDefault(); cancelTempoEdit(); }
    }

    function addTempoAtPosition(e) {
        // Double-click on tempo track: add a new tempo change at the clicked X
        const rect = e.currentTarget.getBoundingClientRect();
        const clickX = e.clientX - rect.left;
        // Convert X back to sample position
        const sample = (clickX - GET_X_OFFSET) / pixelsPerSample + Number(firstSample);
        if (sample < 0) return;
        // Inherit BPM/timeSig from the last change, or defaults
        const last = tempoChanges.length > 0 ? tempoChanges[tempoChanges.length - 1] : null;
        const newTc = {
            bpm: last?.bpm ?? 120,
            samplePosition: Math.round(sample),
            timeSigNumerator: last?.timeSigNumerator ?? 4,
            timeSigDenominator: last?.timeSigDenominator ?? 4,
        };
        // Insert sorted by samplePosition
        const updated = [...tempoChanges, newTc].sort((a, b) => a.samplePosition - b.samplePosition);
        if (onTempoChange) onTempoChange(updated);
        // Open editor on the newly added marker
        const newIdx = updated.findIndex(tc => tc === newTc);
        tick().then(() => openTempoEditor(newIdx, e));
    }

    function deleteTempoChange(idx, e) {
        e.stopPropagation();
        if (tempoChanges.length <= 1) return; // Keep at least one
        const updated = tempoChanges.filter((_, i) => i !== idx);
        editingTempoIdx = -1;
        if (onTempoChange) onTempoChange(updated);
    }
</script>

<div class="timeline-container">
    {#if activeRows.length === 0}
        <div class="empty-state">
            <p>No instruments active</p>
            <p class="empty-hint">Play some notes in Dorico to see them here</p>
        </div>
    {:else}
        <div class="scroll-viewport" bind:this={scrollViewport}>
            <div class="timeline-content" style="width: {totalWidth}px">
                <!-- Header Row: Sticky to top -->
                <div class="header">
                    <div class="channel-label-header">Instrument</div>
                    <div
                        class="beats-track"
                        class:is-dragging={isDragging}
                        role="slider"
                        tabindex="-1"
                        aria-label="Timeline zoom"
                        aria-valuenow={Math.round(
                            (pixelsPerBeat / DEFAULT_ZOOM) * 100,
                        )}
                        onmousedown={onHeaderMouseDown}
                    >
                        {#each beatMarkers as beat}
                            <div
                                class="beat-marker"
                                style="left: {getX(beat.samplePosition)}px"
                            >
                                <div class="beat-tick"
                                     class:bar-tick={beat.isBar}
                                     class:tick-medium={beat.prominence === 'medium'}
                                     class:tick-weak={beat.prominence === 'weak'}
                                ></div>
                                {#if beat.prominence !== 'weak'}
                                    <span class="beat-num"
                                          class:bar-num={beat.isBar}
                                    >{beat.index}</span>
                                {/if}
                            </div>
                        {/each}
                        {#if isDragging}
                            <div class="zoom-badge">
                                {Math.round(
                                    (pixelsPerBeat / DEFAULT_ZOOM) * 100,
                                )}%
                            </div>
                        {/if}
                    </div>
                </div>

                <!-- Body Area -->
                <div class="timeline-body">
                    <!-- Channel Labels: Sticky to left -->
                    <div class="channel-labels">
                        <!-- Tempo track label -->
                        <div class="channel-label tempo-label">
                            <span class="label-name">♪ Tempo</span>
                            {#if metronomeActive}
                                <span class="metronome-live">● LIVE</span>
                            {/if}
                        </div>
                        {#each activeRows as row (row.key)}
                            <div
                                class="channel-label"
                                style="height: {rowHeight}px"
                            >
                                <span class="label-name"
                                    ><span class="solo-icon"
                                        >{row.isSolo ? "👤" : "👥"}</span
                                    >{row.name}</span
                                >
                                <span class="label-port-ch"
                                    >P{row.port + 1} Ch{row.channel}</span
                                >
                            </div>
                        {/each}
                    </div>

                    <!-- Main Grid and Notes Area -->
                    <div class="grid-layer">
                        <div class="grid-background">
                            {#each beatMarkers as beat}
                                <div
                                    class="grid-line"
                                    class:grid-strong={beat.prominence === 'strong'}
                                    class:grid-medium={beat.prominence === 'medium'}
                                    class:grid-weak={beat.prominence === 'weak'}
                                    style="left: {getX(beat.samplePosition)}px"
                                ></div>
                            {/each}
                        </div>

                        <!-- Tempo track row (above notes) -->
                        <div class="tempo-track" ondblclick={addTempoAtPosition}>
                            {#each tempoChanges as tc, i}
                                {@const x = getX(tc.samplePosition)}
                                {@const nextX = i + 1 < tempoChanges.length
                                    ? getX(tempoChanges[i + 1].samplePosition)
                                    : null}
                                <!-- Span bar to next change (if exists) -->
                                {#if nextX !== null}
                                    <div
                                        class="tempo-span"
                                        style="left: {Math.max(LABEL_WIDTH, x)}px; width: {Math.max(0, nextX - Math.max(LABEL_WIDTH, x))}px"
                                    ></div>
                                {:else}
                                    <div
                                        class="tempo-span tempo-span-open"
                                        style="left: {Math.max(LABEL_WIDTH, x)}px; right: 0"
                                    ></div>
                                {/if}
                                <!-- Marker flag: clickable to edit -->
                                <div
                                    class="tempo-marker"
                                    class:editing={editingTempoIdx === i}
                                    style="left: {Math.max(LABEL_WIDTH, x)}px"
                                >
                                    {#if editingTempoIdx === i}
                                        <!-- Inline editor popover -->
                                        <!-- svelte-ignore a11y_no_static_element_interactions -->
                                        <div class="tempo-editor" onkeydown={handleTempoKeydown}>
                                            <label>
                                                <span class="editor-label">BPM</span>
                                                <input
                                                    type="number"
                                                    bind:value={editBpm}
                                                    min="1" max="999" step="0.1"
                                                    class="editor-input bpm-input"
                                                />
                                            </label>
                                            <label>
                                                <span class="editor-label">Time</span>
                                                <span class="ts-inputs">
                                                    <input
                                                        type="number"
                                                        bind:value={editTsNum}
                                                        min="1" max="32"
                                                        class="editor-input ts-input"
                                                    />
                                                    <span class="ts-slash">/</span>
                                                    <input
                                                        type="number"
                                                        bind:value={editTsDen}
                                                        min="1" max="32"
                                                        class="editor-input ts-input"
                                                    />
                                                </span>
                                            </label>
                                            <div class="editor-actions">
                                                <button class="editor-btn ok-btn" onclick={commitTempoEdit}>✓</button>
                                                <button class="editor-btn cancel-btn" onclick={cancelTempoEdit}>✗</button>
                                                {#if tempoChanges.length > 1}
                                                    <button class="editor-btn del-btn" onclick={(e) => deleteTempoChange(i, e)}>🗑</button>
                                                {/if}
                                            </div>
                                        </div>
                                    {:else}
                                        <!-- svelte-ignore a11y_no_static_element_interactions -->
                                        <div class="tempo-flag" class:tempo-auto={tc.source === 'metronome'} onclick={(e) => openTempoEditor(i, e)}>
                                            {#if tc.source === 'metronome'}
                                                <span class="tempo-bpm">{Math.round(tc.bpm)}</span>
                                                <span class="tempo-unit">BPM</span>
                                            {:else}
                                                <span class="tempo-bpm">{Math.round(tc.bpm * 10) / 10}</span>
                                                <span class="tempo-unit">BPM</span>
                                                {#if tc.timeSigNumerator && tc.timeSigDenominator}
                                                    <span class="tempo-ts">{tc.timeSigNumerator}/{tc.timeSigDenominator}</span>
                                                {/if}
                                            {/if}
                                        </div>
                                    {/if}
                                    <div class="tempo-stem"></div>
                                </div>
                            {/each}
                        </div>

                        <div class="notes-layer">
                            {#each noteHistory as note (note.id)}
                                {@const noteKey = `${note.port ?? 0}:${note.channel}`}
                                {@const rowIdx = rowIndexMap[noteKey]}
                                {#if rowIdx !== undefined}
                                    {@const isOngoing = !note.durationSamples}
                                    {@const duration = isOngoing
                                        ? currentSessionSamples -
                                          note.startSample
                                        : note.durationSamples}
                                    <div
                                        class="note-box"
                                        class:ongoing={isOngoing}
                                        class:prom-strong={note.beatProminence === 0}
                                        class:prom-medium={note.beatProminence === 1}
                                        class:prom-weak={note.beatProminence === 2}
                                        class:prom-offbeat={note.beatProminence === 3 || note.beatProminence === undefined}
                                        role="button"
                                        tabindex="-1"
                                        onmouseenter={(e) =>
                                            handleMouseEnter(e, note)}
                                        onmouseleave={handleMouseLeave}
                                        style="
                                        left: {getX(note.startSample)}px;
                                        top: {rowIdx * rowHeight + 5}px;
                                        width: {Math.max(
                                            (duration || 0) * pixelsPerSample,
                                            isOngoing ? 20 : 10,
                                        )}px;
                                        height: {rowHeight - 10}px;
                                    "
                                    >
                                        <span class="note-label"
                                            >{note.noteNumber}</span
                                        >
                                    </div>
                                {/if}
                            {/each}
                        </div>
                    </div>
                </div>
            </div>
        </div>
    {/if}
</div>

<style>
    .timeline-container {
        display: flex;
        flex-direction: column;
        height: 100%;
        background: #020617;
        overflow: hidden;
        border-top: 1px solid #1e293b;
    }

    .empty-state {
        flex: 1;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        color: #475569;
        font-size: 1rem;
    }

    .empty-state p {
        margin: 4px 0;
    }

    .empty-hint {
        font-size: 0.8rem;
        color: #334155;
    }

    .scroll-viewport {
        flex: 1;
        overflow: auto;
        position: relative;
    }

    .timeline-content {
        position: relative;
        min-height: 100%;
    }

    .header {
        position: sticky;
        top: 0;
        display: flex;
        height: 30px;
        background: #0f172a;
        border-bottom: 1px solid #334155;
        z-index: 100;
    }

    .channel-label-header {
        width: 180px;
        flex-shrink: 0;
        display: flex;
        align-items: center;
        justify-content: center;
        font-size: 0.7rem;
        font-weight: bold;
        color: #cbd5e1;
        border-right: 1px solid #334155;
        background: #0f172a;
        position: sticky;
        left: 0;
        z-index: 110;
    }

    .beats-track {
        flex: 1;
        position: relative;
        cursor: ns-resize;
        user-select: none;
    }

    .beats-track.is-dragging {
        background: #1e293b;
    }

    .beat-marker {
        position: absolute;
        top: 0;
        display: flex;
        flex-direction: column;
        align-items: center;
        pointer-events: none;
    }

    .beat-tick {
        width: 1px;
        height: 8px;
        background: #475569;
    }

    .beat-tick.bar-tick {
        height: 14px;
        background: #94a3b8;
    }

    .beat-tick.tick-medium {
        height: 10px;
        background: #64748b;
    }

    .beat-tick.tick-weak {
        height: 5px;
        background: #64748b;
    }

    .beat-num {
        font-size: 0.6rem;
        color: #64748b;
        margin-top: 1px;
        transform: translateX(1px);
    }

    .beat-num.bar-num {
        font-size: 0.7rem;
        color: #94a3b8;
        font-weight: 600;
    }

    .beat-num.num-weak {
        font-size: 0.5rem;
        color: #334155;
    }

    .zoom-badge {
        position: fixed;
        top: 50%;
        left: 50%;
        transform: translate(-50%, -50%);
        background: rgba(15, 23, 42, 0.9);
        border: 1px solid #38bdf8;
        color: #38bdf8;
        padding: 6px 16px;
        border-radius: 8px;
        font-size: 1.1rem;
        font-weight: 700;
        letter-spacing: 0.05em;
        pointer-events: none;
        z-index: 9999;
        backdrop-filter: blur(4px);
    }


    /* ── Tempo track ─────────────────────────────────────────────────────── */
    .tempo-label {
        height: 36px;
        background: #0a1628;
        border-bottom: 1px solid #f59e0b44;
    }

    .tempo-label .label-name {
        color: #f59e0b;
        font-size: 0.7rem;
        font-weight: 700;
        letter-spacing: 0.06em;
        text-transform: uppercase;
    }

    .tempo-track {
        position: relative;
        height: 36px;
        border-bottom: 1px solid #f59e0b44;
        background: #0a1628;
        flex-shrink: 0;
        z-index: 20;
    }

    /* Horizontal bar spanning from this change to the next */
    .tempo-span {
        position: absolute;
        top: 50%;
        height: 2px;
        background: #f59e0b55;
        transform: translateY(-50%);
        pointer-events: none;
    }

    /* Marker: flag + vertical stem */
    .tempo-marker {
        position: absolute;
        top: 0;
        display: flex;
        flex-direction: column;
        align-items: flex-start;
        pointer-events: auto;
        z-index: 10;
        cursor: pointer;
    }

    .tempo-marker.editing {
        z-index: 100;
    }

    .tempo-flag {
        display: flex;
        align-items: baseline;
        gap: 3px;
        background: #f59e0b;
        color: #020617;
        padding: 2px 6px 2px 5px;
        border-radius: 0 4px 4px 0;
        font-weight: 800;
        line-height: 1;
        white-space: nowrap;
        cursor: pointer;
        transition: filter 0.15s, transform 0.15s;
    }

    .tempo-flag:hover {
        filter: brightness(1.15);
        transform: scale(1.05);
    }

    .tempo-bpm {
        font-size: 0.75rem;
    }

    .tempo-unit {
        font-size: 0.55rem;
        font-weight: 700;
        opacity: 0.75;
        text-transform: uppercase;
        letter-spacing: 0.04em;
    }

    .tempo-ts {
        font-size: 0.6rem;
        font-weight: 600;
        margin-left: 3px;
        opacity: 0.8;
        border-left: 1px solid #02061744;
        padding-left: 4px;
    }

    .tempo-stem {
        width: 1px;
        flex: 1;
        background: #f59e0b;
        align-self: center;
        margin-left: 1px;
        min-height: 4px;
    }

    /* ── Metronome live indicator ──────────────────────────────────── */
    .metronome-live {
        font-size: 0.55rem;
        font-weight: 700;
        color: #22c55e;
        letter-spacing: 0.05em;
        margin-left: 6px;
        animation: pulse-live 1.5s ease-in-out infinite;
    }

    @keyframes pulse-live {
        0%, 100% { opacity: 1; }
        50% { opacity: 0.4; }
    }

    /* ── Auto-detected tempo marker ────────────────────────────────── */
    .tempo-flag.tempo-auto {
        background: #22c55e;
    }

    .tempo-auto-badge {
        font-size: 0.45rem;
        font-weight: 800;
        letter-spacing: 0.08em;
        opacity: 0.7;
        margin-right: 2px;
    }

    /* ── Tempo editor popover ───────────────────────────────────────── */
    .tempo-editor {
        background: #1e293b;
        border: 1px solid #f59e0b88;
        border-radius: 6px;
        padding: 6px 8px;
        display: flex;
        flex-direction: column;
        gap: 4px;
        box-shadow: 0 4px 16px rgba(0,0,0,0.5);
        min-width: 120px;
    }

    .tempo-editor label {
        display: flex;
        align-items: center;
        gap: 4px;
    }

    .editor-label {
        font-size: 0.6rem;
        color: #94a3b8;
        text-transform: uppercase;
        letter-spacing: 0.05em;
        width: 28px;
        flex-shrink: 0;
    }

    .editor-input {
        background: #0f172a;
        border: 1px solid #334155;
        border-radius: 3px;
        color: #f1f5f9;
        font-size: 0.75rem;
        padding: 2px 4px;
        outline: none;
        font-family: inherit;
    }

    .editor-input:focus {
        border-color: #f59e0b;
    }

    .bpm-input {
        width: 56px;
    }

    .ts-inputs {
        display: flex;
        align-items: center;
        gap: 2px;
    }

    .ts-input {
        width: 30px;
        text-align: center;
    }

    .ts-slash {
        color: #64748b;
        font-size: 0.75rem;
    }

    .editor-actions {
        display: flex;
        gap: 4px;
        justify-content: flex-end;
        margin-top: 2px;
    }

    .editor-btn {
        border: none;
        border-radius: 3px;
        padding: 2px 8px;
        font-size: 0.7rem;
        cursor: pointer;
        line-height: 1.2;
    }

    .ok-btn {
        background: #f59e0b;
        color: #020617;
        font-weight: 700;
    }

    .cancel-btn {
        background: #334155;
        color: #94a3b8;
    }

    .del-btn {
        background: #7f1d1d;
        color: #fca5a5;
        margin-left: auto;
    }

    .editor-btn:hover {
        filter: brightness(1.2);
    }

    /* Global cursor override during zoom drag — added to <body> */
    :global(body.is-zooming),
    :global(body.is-zooming *) {
        cursor: ns-resize !important;
    }

    .timeline-body {
        display: flex;
        position: relative;
    }

    .channel-labels {
        width: 180px;
        flex-shrink: 0;
        background: #0f172a;
        border-right: 1px solid #334155;
        position: sticky;
        left: 0;
        z-index: 50;
    }

    .channel-label {
        display: flex;
        flex-direction: column;
        justify-content: center;
        padding-left: 10px;
        font-size: 0.75rem;
        color: #cbd5e1;
        border-bottom: 1px solid #1e293b;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
    }

    .label-name {
        color: #e2e8f0;
        font-weight: 600;
        font-size: 0.75rem;
        line-height: 1.2;
    }

    .solo-icon {
        margin-right: 3px;
        font-size: 0.65rem;
    }

    .label-port-ch {
        color: #94a3b8;
        font-size: 0.6rem;
        line-height: 1.2;
    }

    .grid-layer {
        flex: 1;
        position: relative;
    }

    .grid-background {
        position: absolute;
        top: 0;
        left: 0;
        width: 100%;
        height: 100%;
        pointer-events: none;
    }

    .grid-line {
        position: absolute;
        top: 0;
        bottom: 0;
        width: 1px;
        background: #1e293b;
    }

    .grid-line.grid-strong {
        background: #334155;
    }

    .grid-line.grid-medium {
        background: #1e293b;
    }

    .grid-line.grid-weak {
        background: #111827;
    }

    .notes-layer {
        position: relative;
        height: 100%;
    }

    .note-box {
        position: absolute;
        background: #38bdf8;
        border: 1px solid #0ea5e9;
        border-radius: 4px;
        display: flex;
        align-items: center;
        justify-content: center;
        color: #020617;
        font-size: 0.7rem;
        font-weight: bold;
        cursor: pointer;
        transition:
            background 0.2s,
            transform 0.1s;
    }

    .note-box.ongoing {
        background: linear-gradient(90deg, #38bdf8, #0ea5e9);
        border-style: dashed;
        box-shadow: 0 0 15px rgba(56, 189, 248, 0.3);
        animation: pulse-ongoing 2s infinite ease-in-out;
    }

    @keyframes pulse-ongoing {
        0%,
        100% {
            opacity: 1;
        }
        50% {
            opacity: 0.8;
        }
    }

    .note-box:hover {
        background: #7dd3fc;
        z-index: 100;
    }

    .note-label {
        pointer-events: none;
        white-space: nowrap;
        overflow: hidden;
    }

    /* ── Note prominence styling ───────────────────────────────────── */
    /* 0 = strong (downbeat), 1 = medium, 2 = weak, 3 = off-beat      */
    .note-box.prom-strong {
        background: #38bdf8;
        border-color: #0ea5e9;
        border-left: 3px solid #7dd3fc;
    }

    .note-box.prom-medium {
        background: #2196c4;
        border-color: #1a7fa6;
    }

    .note-box.prom-weak {
        background: #1a6d94;
        border-color: #155a7a;
        opacity: 0.85;
    }

    .note-box.prom-offbeat {
        background: #134e6b;
        border-color: #0f3d55;
        border-style: dashed;
        opacity: 0.7;
    }
</style>
