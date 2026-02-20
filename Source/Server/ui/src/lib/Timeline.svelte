<script>
    import { tick } from "svelte";
    import { FAMILY_ORDER, canonicalFamily } from "./orchestralOrder.js";

    let {
        noteHistory = [],
        heartbeat = 0,
        firstSample = 0,
        channelInstruments = {},
        instrumentMap = {},
        onHover,
        onLeave,
    } = $props();

    // Constants for time-to-pixel conversion
    const sampleRate = 44100;
    const bpm = 120;
    const samplesPerBeat = sampleRate * (60 / bpm);
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
    let scrollViewport; // bound to the .scroll-viewport div

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

    let beatMarkers = $derived(
        Array.from(
            { length: Math.ceil(totalWidth / pixelsPerBeat) },
            (_, i) => i,
        ),
    );

    const handleMouseEnter = (event, note) => {
        if (onHover) {
            const rect = event.currentTarget.getBoundingClientRect();
            onHover(rect, note);
        }
    };

    const handleMouseLeave = () => {
        if (onLeave) onLeave();
    };
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
                                style="left: {getX(
                                    firstSample + beat * samplesPerBeat,
                                )}px"
                            >
                                {beat}
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
                                    style="left: {getX(
                                        firstSample + beat * samplesPerBeat,
                                    )}px"
                                ></div>
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
        color: #94a3b8;
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
        top: 5px;
        font-size: 0.7rem;
        color: #64748b;
        transform: translateX(5px);
        pointer-events: none;
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
        color: #94a3b8;
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
        color: #64748b;
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
</style>
