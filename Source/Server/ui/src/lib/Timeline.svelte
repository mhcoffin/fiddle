<script>
    let {
        noteHistory = [],
        heartbeat = 0,
        firstSample = 0,
        channelInstruments = {},
        onHover,
        onLeave,
    } = $props();

    // Constants for time-to-pixel conversion
    const sampleRate = 44100;
    const bpm = 120;
    const samplesPerBeat = sampleRate * (60 / bpm);
    const pixelsPerBeat = 150;
    const pixelsPerSample = pixelsPerBeat / samplesPerBeat;
    const rowHeight = 40;
    const channels = Array.from({ length: 16 }, (_, i) => i + 1);

    // heartbeat is in "seconds since play started" OR a counter.
    const currentSessionSamples = $derived(heartbeat * sampleRate);

    const getX = (sample) =>
        (Number(sample) - Number(firstSample)) * pixelsPerSample + 150 + 20;

    // Calculate the total width based on the last note's end
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

    // Generate beat markers (relative to Normalized 0)
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
    <div class="scroll-viewport">
        <div class="timeline-content" style="width: {totalWidth}px">
            <!-- Header Row: Sticky to top -->
            <div class="header">
                <div class="channel-label-header">Channel</div>
                <div class="beats-track">
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
                </div>
            </div>

            <!-- Body Area -->
            <div class="timeline-body">
                <!-- Channel Labels: Sticky to left -->
                <div class="channel-labels">
                    {#each channels as ch}
                        <div
                            class="channel-label"
                            style="height: {rowHeight}px"
                        >
                            Ch {ch}{channelInstruments[ch]
                                ? `: ${channelInstruments[ch]}`
                                : ""}
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
                            {@const isOngoing = !note.durationSamples}
                            {@const duration = isOngoing
                                ? currentSessionSamples - note.startSample
                                : note.durationSamples}
                            <div
                                class="note-box"
                                class:ongoing={isOngoing}
                                role="button"
                                tabindex="-1"
                                onmouseenter={(e) => handleMouseEnter(e, note)}
                                onmouseleave={handleMouseLeave}
                                style="
                                    left: {getX(note.startSample)}px;
                                    top: {(note.channel - 1) * rowHeight + 5}px;
                                    width: {Math.max(
                                    (duration || 0) * pixelsPerSample,
                                    isOngoing ? 20 : 10,
                                )}px;
                                    height: {rowHeight - 10}px;
                                "
                            >
                                <span class="note-label">{note.noteNumber}</span
                                >
                            </div>
                        {/each}
                    </div>
                </div>
            </div>
        </div>
    </div>

    <!-- Debug Panel -->
    <div class="debug-panel">
        <div>
            Notes In History: {noteHistory.length} | First Sample: {firstSample}
            | Total Width: {totalWidth.toFixed(0)}
        </div>
    </div>
</div>

<style>
    .debug-panel {
        background: #1e293b;
        padding: 5px 10px;
        font-family: monospace;
        font-size: 0.7rem;
        color: #94a3b8;
        border-top: 1px solid #334155;
    }

    .timeline-container {
        display: flex;
        flex-direction: column;
        height: 100%;
        background: #020617;
        overflow: hidden;
        border-top: 1px solid #1e293b;
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
        z-index: 100; /* Above everything */
    }

    .channel-label-header {
        width: 150px;
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
        z-index: 110; /* Above beats-track markers when scrolling horizontally */
    }

    .beats-track {
        flex: 1;
        position: relative;
    }

    .beat-marker {
        position: absolute;
        top: 5px;
        font-size: 0.7rem;
        color: #64748b;
        transform: translateX(5px);
    }

    .timeline-body {
        display: flex;
        position: relative;
    }

    .channel-labels {
        width: 150px;
        flex-shrink: 0;
        background: #0f172a;
        border-right: 1px solid #334155;
        position: sticky;
        left: 0;
        z-index: 50; /* Above grid/notes, below main header */
    }

    .channel-label {
        display: flex;
        align-items: center;
        justify-content: flex-start;
        padding-left: 10px;
        font-size: 0.75rem;
        color: #94a3b8;
        border-bottom: 1px solid #1e293b;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
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
