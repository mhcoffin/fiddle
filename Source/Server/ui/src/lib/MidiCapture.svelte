<script>
    import { onMount, onDestroy } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";

    // Strip list data from C++
    let strips = $state([]);
    let selectedStripId = $state("");

    // Capture state
    let incomingRecording = $state(false);
    let emittedRecording = $state(false);
    let incomingEvents = $state([]);
    let emittedEvents = $state([]);
    let pollTimer = $state(null);

    // Diff results
    let diffResults = $state(null);
    let showDiff = $state(false);

    const EVENT_TYPES = ["NoteOn", "NoteOff", "CC", "PC"];

    const fmtType = (t) => EVENT_TYPES[t] || `?${t}`;
    const fmtTime = (ms) => (ms / 1000).toFixed(3) + "s";
    const NOTE_NAMES = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
    const midiNoteName = (n) => NOTE_NAMES[n % 12] + (Math.floor(n / 12) - 1);
    const fmtParam = (e) => (e.type <= 1) ? midiNoteName(e.p1) : e.p1;

    // ── C++ → JS handlers ──
    onFromCpp("setCaptureStripList", (data) => {
        strips = data || [];
        if (strips.length > 0 && !selectedStripId) {
            selectedStripId = strips[0].id;
        }
    });

    onFromCpp("setMidiCapture", (data) => {
        if (data.stripId !== selectedStripId) return;
        if (data.mode === "incoming") {
            incomingEvents = data.events || [];
        } else if (data.mode === "emitted") {
            emittedEvents = data.events || [];
        }
    });

    // Fetch strip list and poll for capture data
    const fetchStrips = () => dispatchCpp("getCaptureStripList");
    const fetchCapture = () => {
        if (!selectedStripId) return;
        dispatchCpp("getMidiCapture", selectedStripId, "incoming");
        dispatchCpp("getMidiCapture", selectedStripId, "emitted");
    };

    onMount(() => {
        fetchStrips();
        pollTimer = setInterval(() => {
            if (incomingRecording || emittedRecording) fetchCapture();
        }, 500);
    });

    onDestroy(() => {
        if (pollTimer) clearInterval(pollTimer);
    });

    // ── Actions ──
    const startRecording = (mode) => {
        dispatchCpp("startMidiCapture", selectedStripId, mode);
        if (mode === "incoming" || mode === "both") incomingRecording = true;
        if (mode === "emitted" || mode === "both") emittedRecording = true;
    };

    const stopRecording = (mode) => {
        dispatchCpp("stopMidiCapture", selectedStripId, mode);
        if (mode === "incoming" || mode === "both") incomingRecording = false;
        if (mode === "emitted" || mode === "both") emittedRecording = false;
        // Final fetch to get all captured data
        setTimeout(fetchCapture, 100);
    };

    const clearCapture = (mode) => {
        dispatchCpp("clearMidiCapture", selectedStripId, mode);
        if (mode === "incoming" || mode === "both") incomingEvents = [];
        if (mode === "emitted" || mode === "both") emittedEvents = [];
        if (mode === "both") { diffResults = null; showDiff = false; }
    };

    const onStripChange = (e) => {
        selectedStripId = e.target.value;
        incomingEvents = [];
        emittedEvents = [];
        diffResults = null;
        showDiff = false;
        fetchCapture();
    };

    // ── Client-Side Diff ──
    function compareCaptures() {
        const result = [];
        const inc = [...incomingEvents];
        const emi = [...emittedEvents];

        // Match by event type + param1 (note#/CC#) in order
        const usedEmi = new Set();

        for (let i = 0; i < inc.length; i++) {
            const ie = inc[i];
            let matched = false;
            for (let j = 0; j < emi.length; j++) {
                if (usedEmi.has(j)) continue;
                const ee = emi[j];
                if (ie.type === ee.type && ie.p1 === ee.p1) {
                    // Matched — check for differences
                    const diffs = [];
                    if (ie.ch !== ee.ch) diffs.push(`ch: ${ie.ch}→${ee.ch}`);
                    if (ie.p2 !== ee.p2) diffs.push(`val: ${ie.p2}→${ee.p2}`);
                    // Label comparison: incoming lbl is pipe-delimited candidate set
                    if (ie.lbl && ee.lbl) {
                        const candidates = ie.lbl.split("|");
                        if (!candidates.includes(ee.lbl))
                            diffs.push(`label: ${ee.lbl} ∉ {${candidates.join(", ")}}`);
                    }
                    const timeDelta = Math.abs(ie.t - ee.t);
                    result.push({
                        status: diffs.length > 0 ? "changed" : "match",
                        incoming: ie,
                        emitted: ee,
                        diffs,
                        timeDelta,
                    });
                    usedEmi.add(j);
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                result.push({
                    status: "missing",
                    incoming: ie,
                    emitted: null,
                    diffs: ["not emitted"],
                    timeDelta: 0,
                });
            }
        }

        // Extra events in emitted that weren't matched
        for (let j = 0; j < emi.length; j++) {
            if (!usedEmi.has(j)) {
                result.push({
                    status: "extra",
                    incoming: null,
                    emitted: emi[j],
                    diffs: ["extra in emitted"],
                    timeDelta: 0,
                });
            }
        }

        diffResults = result;
        showDiff = true;
    }

    // Export capture as JSON file download
    const exportCapture = (mode) => {
        const data = mode === "incoming" ? incomingEvents : emittedEvents;
        // Format as readable text lines matching the capture display
        const lines = data.map(e => {
            let line = `${fmtTime(e.t)}\t${fmtType(e.type)}\tCh${e.ch}\t${fmtParam(e)}\t${e.p2}`;
            if (e.lbl) line += `\t${e.lbl.replaceAll('|', ' / ')}`;
            return line;
        });
        const text = lines.join("\n");
        const defaultName = `${selectedStripId}_${mode}_capture`;
        dispatchCpp("exportCaptureFile", text, defaultName);
    };
</script>

<div class="capture-container">
    <!-- Toolbar -->
    <div class="capture-toolbar">
        <div class="toolbar-left">
            <label class="strip-label" for="capture-strip-select">Strip:</label>
            <select id="capture-strip-select" class="strip-select" value={selectedStripId} onchange={onStripChange}>
                {#each strips as s}
                    <option value={s.id}>{s.name} (Port {s.port} Ch{s.channel + 1})</option>
                {/each}
                {#if strips.length === 0}
                    <option value="">No strips</option>
                {/if}
            </select>
            <button class="toolbar-btn refresh-btn" onclick={fetchStrips} title="Refresh strip list">⟳</button>
        </div>
        <div class="toolbar-right">
            <button
                class="toolbar-btn compare-btn"
                onclick={compareCaptures}
                disabled={incomingEvents.length === 0 && emittedEvents.length === 0}
            >Compare</button>
        </div>
    </div>

    <!-- Two-column capture panels -->
    <div class="capture-columns" class:show-diff={showDiff}>
        <!-- Incoming Column -->
        <div class="capture-column">
            <div class="column-header incoming-header">
                <span class="column-title">Incoming (Dorico)</span>
                <span class="event-count">{incomingEvents.length} events</span>
            </div>
            <div class="column-controls">
                {#if !incomingRecording}
                    <button class="ctrl-btn record-btn" onclick={() => startRecording("incoming")}
                        disabled={!selectedStripId}>● Rec</button>
                {:else}
                    <button class="ctrl-btn stop-btn" onclick={() => stopRecording("incoming")}>■ Stop</button>
                {/if}
                <button class="ctrl-btn" onclick={() => clearCapture("incoming")}
                    disabled={incomingRecording}>Clear</button>
                <button class="ctrl-btn" onclick={() => exportCapture("incoming")}
                    disabled={incomingEvents.length === 0}>Export</button>
            </div>
            <div class="event-list">
                {#each incomingEvents as e, i}
                    <div class="event-row type-{e.type}">
                        <span class="ev-time">{fmtTime(e.t)}</span>
                        <span class="ev-type">{fmtType(e.type)}</span>
                        <span class="ev-ch">Ch{e.ch}</span>
                        <span class="ev-param">{fmtParam(e)}</span>
                        <span class="ev-val">{e.p2}</span>
                        {#if e.lbl}
                            <span class="ev-label">{e.lbl.replaceAll('|', ' / ')}</span>
                        {/if}
                    </div>
                {/each}
                {#if incomingEvents.length === 0}
                    <div class="empty-msg">No events captured</div>
                {/if}
            </div>
        </div>

        <!-- Emitted Column -->
        <div class="capture-column">
            <div class="column-header emitted-header">
                <span class="column-title">Emitted (FiddleServer)</span>
                <span class="event-count">{emittedEvents.length} events</span>
            </div>
            <div class="column-controls">
                {#if !emittedRecording}
                    <button class="ctrl-btn record-btn" onclick={() => startRecording("emitted")}
                        disabled={!selectedStripId}>● Rec</button>
                {:else}
                    <button class="ctrl-btn stop-btn" onclick={() => stopRecording("emitted")}>■ Stop</button>
                {/if}
                <button class="ctrl-btn" onclick={() => clearCapture("emitted")}
                    disabled={emittedRecording}>Clear</button>
                <button class="ctrl-btn" onclick={() => exportCapture("emitted")}
                    disabled={emittedEvents.length === 0}>Export</button>
            </div>
            <div class="event-list">
                {#each emittedEvents as e, i}
                    <div class="event-row type-{e.type}">
                        <span class="ev-time">{fmtTime(e.t)}</span>
                        <span class="ev-type">{fmtType(e.type)}</span>
                        <span class="ev-ch">Ch{e.ch}</span>
                        <span class="ev-param">{fmtParam(e)}</span>
                        <span class="ev-val">{e.p2}</span>
                        {#if e.lbl}
                            <span class="ev-label">{e.lbl}</span>
                        {/if}
                    </div>
                {/each}
                {#if emittedEvents.length === 0}
                    <div class="empty-msg">No events captured</div>
                {/if}
            </div>
        </div>
    </div>

    <!-- Diff Results -->
    {#if showDiff && diffResults}
        <div class="diff-panel">
            <div class="diff-header">
                <span class="diff-title">Comparison Results</span>
                <span class="diff-summary">
                    {diffResults.filter(r => r.status === "match").length} matched,
                    {diffResults.filter(r => r.status === "changed").length} changed,
                    {diffResults.filter(r => r.status === "missing").length} missing,
                    {diffResults.filter(r => r.status === "extra").length} extra
                </span>
                <button class="toolbar-btn" onclick={() => { showDiff = false; }}>Close</button>
            </div>
            <div class="diff-list">
                {#each diffResults as r, i}
                    <div class="diff-row status-{r.status}">
                        <span class="diff-idx">#{i + 1}</span>
                        <span class="diff-status">{r.status}</span>
                        <span class="diff-type">
                            {r.incoming ? fmtType(r.incoming.type) : fmtType(r.emitted.type)}
                        </span>
                        <span class="diff-param">
                            {r.incoming ? r.incoming.p1 : r.emitted.p1}
                        </span>
                        <span class="diff-detail">
                            {#if r.diffs.length > 0}
                                {r.diffs.join(", ")}
                            {:else}
                                ✓
                            {/if}
                        </span>
                    </div>
                {/each}
            </div>
        </div>
    {/if}
</div>

<style>
    .capture-container {
        width: 100%;
        height: 100%;
        background: #020617;
        font-family: "JetBrains Mono", "Fira Code", monospace;
        font-size: 0.8rem;
        display: flex;
        flex-direction: column;
        color: #cbd5e1;
    }

    .capture-toolbar {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 6px 12px;
        background: #0f172a;
        border-bottom: 1px solid #1e293b;
        flex-shrink: 0;
        gap: 12px;
    }

    .toolbar-left, .toolbar-right {
        display: flex;
        align-items: center;
        gap: 8px;
    }

    .strip-label { color: #94a3b8; font-size: 0.75rem; }

    .strip-select {
        background: #1e293b;
        border: 1px solid #334155;
        color: #e2e8f0;
        border-radius: 4px;
        padding: 3px 8px;
        font-size: 0.75rem;
        font-family: inherit;
    }

    .toolbar-btn {
        padding: 3px 10px;
        background: #1e293b;
        border: 1px solid #334155;
        border-radius: 4px;
        color: #cbd5e1;
        font-size: 0.7rem;
        cursor: pointer;
        transition: all 0.15s;
    }
    .toolbar-btn:hover { background: #334155; }
    .toolbar-btn:disabled { opacity: 0.4; cursor: default; }

    .refresh-btn { padding: 3px 6px; font-size: 0.85rem; }

    .compare-btn {
        background: #1e3a5f;
        border-color: #2563eb;
        color: #93c5fd;
    }
    .compare-btn:hover:not(:disabled) { background: #1d4ed8; }

    /* Two-column layout */
    .capture-columns {
        display: grid;
        grid-template-columns: 1fr 1fr;
        gap: 1px;
        flex: 1;
        min-height: 0;
        background: #1e293b;
    }
    .capture-columns.show-diff { flex: 0.6; }

    .capture-column {
        display: flex;
        flex-direction: column;
        background: #020617;
        min-height: 0;
    }

    .column-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 4px 10px;
        border-bottom: 1px solid #1e293b;
        flex-shrink: 0;
    }
    .incoming-header { background: #0c1f2e; }
    .emitted-header { background: #1a0f2e; }

    .column-title { font-weight: 600; font-size: 0.75rem; }
    .incoming-header .column-title { color: #38bdf8; }
    .emitted-header .column-title { color: #c084fc; }
    .event-count { color: #64748b; font-size: 0.7rem; }

    .column-controls {
        display: flex;
        gap: 4px;
        padding: 4px 8px;
        border-bottom: 1px solid #1e293b;
        flex-shrink: 0;
    }

    .ctrl-btn {
        padding: 2px 8px;
        background: #1e293b;
        border: 1px solid #334155;
        border-radius: 3px;
        color: #cbd5e1;
        font-size: 0.65rem;
        font-family: inherit;
        cursor: pointer;
        transition: all 0.15s;
    }
    .ctrl-btn:hover:not(:disabled) { background: #334155; }
    .ctrl-btn:disabled { opacity: 0.35; cursor: default; }

    .record-btn { color: #f87171; border-color: #991b1b; }
    .record-btn:hover:not(:disabled) { background: #450a0a; }

    .stop-btn {
        color: #fef08a;
        border-color: #854d0e;
        background: #422006;
        animation: pulse-rec 1s infinite;
    }

    @keyframes pulse-rec {
        0%, 100% { opacity: 1; }
        50% { opacity: 0.7; }
    }

    .event-list {
        flex: 1;
        overflow-y: auto;
        min-height: 0;
    }

    .event-row {
        display: grid;
        grid-template-columns: 70px 55px 35px 30px 30px;
        gap: 6px;
        padding: 2px 10px;
        border-bottom: 1px solid #0f172a;
        font-size: 0.7rem;
    }
    .event-row:hover { background: #0f172a; }

    .ev-time { color: #64748b; }
    .ev-type { font-weight: 600; }
    .ev-ch { color: #94a3b8; }
    .ev-param { color: #e2e8f0; }
    .ev-val { color: #94a3b8; }
    .ev-label { color: #fbbf24; font-style: italic; font-size: 0.65rem; grid-column: 1 / -1; }

    .type-0 .ev-type { color: #4ade80; } /* NoteOn */
    .type-1 .ev-type { color: #f87171; } /* NoteOff */
    .type-2 .ev-type { color: #60a5fa; } /* CC */
    .type-3 .ev-type { color: #c084fc; } /* PC */

    .empty-msg {
        padding: 20px;
        text-align: center;
        color: #475569;
        font-style: italic;
    }

    /* Diff panel */
    .diff-panel {
        flex: 0.4;
        display: flex;
        flex-direction: column;
        border-top: 2px solid #334155;
        min-height: 0;
    }

    .diff-header {
        display: flex;
        align-items: center;
        gap: 12px;
        padding: 4px 10px;
        background: #0f172a;
        border-bottom: 1px solid #1e293b;
        flex-shrink: 0;
    }
    .diff-title { font-weight: 600; color: #e2e8f0; font-size: 0.75rem; }
    .diff-summary { color: #94a3b8; font-size: 0.7rem; flex: 1; }

    .diff-list {
        flex: 1;
        overflow-y: auto;
        min-height: 0;
    }

    .diff-row {
        display: grid;
        grid-template-columns: 35px 60px 55px 35px 1fr;
        gap: 6px;
        padding: 2px 10px;
        border-bottom: 1px solid #0f172a;
        font-size: 0.7rem;
    }
    .diff-row:hover { background: #0f172a; }

    .diff-idx { color: #475569; }
    .diff-status { font-weight: 600; }
    .diff-type { color: #94a3b8; }
    .diff-param { color: #e2e8f0; }
    .diff-detail { color: #94a3b8; }

    .status-match .diff-status { color: #4ade80; }
    .status-changed .diff-status { color: #fbbf24; }
    .status-changed { background: rgba(251, 191, 36, 0.05); }
    .status-missing .diff-status { color: #f87171; }
    .status-missing { background: rgba(248, 113, 113, 0.05); }
    .status-extra .diff-status { color: #c084fc; }
    .status-extra { background: rgba(192, 132, 252, 0.05); }
</style>
