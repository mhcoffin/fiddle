<script>
    import { onDestroy, onMount } from "svelte";
    import Knob from "./Knob.svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    let buses = $state([]);
    let stemMode = $state(false);
    let includeMaster = $state(true);
    let tailSeconds = $state(5);
    let validTail = $derived(Number.isFinite(tailSeconds) && tailSeconds >= 0 && tailSeconds <= 60);
    let selectedBusIds = $state([]);
    let liveBusIds = $derived(selectedBusIds.filter(id => buses.some(bus => bus.id === id)));
    let outputCount = $derived(liveBusIds.length + (includeMaster ? 1 : 0));
    let printState = $state({
        armed: false,
        recording: false,
        finalizing: false,
        filePath: "",
        fileName: "",
        sampleRate: 0,
        durationSeconds: 0,
        droppedBlocks: 0,
        error: "",
        format: "Stereo WAV · 32-bit float",
    });

    const formatDuration = (seconds) => {
        const total = Math.max(0, Math.floor(Number(seconds) || 0));
        const hours = Math.floor(total / 3600);
        const minutes = Math.floor((total % 3600) / 60);
        const remainder = total % 60;
        return hours
            ? `${hours}:${String(minutes).padStart(2, "0")}:${String(remainder).padStart(2, "0")}`
            : `${minutes}:${String(remainder).padStart(2, "0")}`;
    };


    const unsubscribePrint = onFromCpp("setMixPrintState", (state) => {
        if (state && typeof state === "object") {
            printState = state;
            buses = Array.isArray(state.buses) ? state.buses : [];
            if (state.armed) {
                if (Number.isFinite(state.tailSeconds)) tailSeconds = state.tailSeconds;
                stemMode = Boolean(state.stemMode);
                includeMaster = state.includeMaster !== false;
                selectedBusIds = Array.isArray(state.busIds) ? state.busIds : [];
            }
        }
    });
    let printPoll;
    const poll = () => { if (!document.hidden) dispatchCpp("requestMixPrintState"); };
    const setPolling = (visible) => {
        if (printPoll) window.clearInterval(printPoll);
        printPoll = null;
        if (visible) { poll(); printPoll = window.setInterval(poll, 500); }
    };
    const unsubscribeVisibility = onFromCpp("setToolWindowVisible", setPolling);
    onMount(() => {
        setPolling(true);
    });
    onDestroy(() => {
        unsubscribePrint();
        unsubscribeVisibility();
        if (printPoll) window.clearInterval(printPoll);
    });
</script>

<main class="print-pane" aria-label="Print Audio">
    <header>
        <div><h1>Print Audio</h1><p>Record the Master mix or synchronized bus stems.</p></div>
        <button onclick={() => dispatchCpp("hidePrintWindow")} aria-label="Close Print Audio">Close</button>
    </header>
    <div class="content">
            <section class="print-section" aria-label="Print audio">
                <div class="section-heading">
                    <div>
                        <p>Select the passage in Dorico first, then arm a transport-synchronized WAV.</p>
                    </div>
                    {#if printState.armed}
                        <button class="stop-print" disabled={printState.finalizing} onclick={() => dispatchCpp("stopMixPrint")}>
                            {printState.recording ? "Stop & finish" : printState.finalizing ? "Finishing…" : "Cancel armed print"}
                        </button>
                    {:else}
                        <button class="primary" disabled={!validTail || (stemMode && (outputCount === 0 || outputCount > 64))}
                            onclick={() => stemMode ? dispatchCpp("startStemPrint", liveBusIds, includeMaster, tailSeconds) : dispatchCpp("startMixPrint", tailSeconds)}>
                            {stemMode ? "Choose folder & arm" : "Choose WAV & arm"}
                        </button>
                    {/if}
                </div>
                <fieldset class="print-options" disabled={printState.armed}>
                    <legend>Print options</legend>
                    <label><input type="radio" bind:group={stemMode} value={false} /> Master mix</label>
                    <label><input type="radio" bind:group={stemMode} value={true} /> Selected bus stems</label>
                    {#if stemMode}
                        <div class="stem-outputs">
                            <label><input type="checkbox" bind:checked={includeMaster} /> Master mix (includes Master processing)</label>
                            {#each buses as bus (bus.id)}
                                <label><input type="checkbox" bind:group={selectedBusIds} value={bus.id} /> {bus.name}</label>
                            {/each}
                            {#if !buses.length}<p>No audio buses yet. Create buses and route your instruments or sends to them first.</p>{/if}
                        </div>
                        <p>{outputCount} selected · maximum 64 files. Each take gets a new folder; existing files are never replaced.</p>
                        <p>Bus stems include bus effects and levels, follow mute/solo, and exclude Master processing. Shared FX are separate stems. Direct-to-Master instruments appear only in the Master mix.</p>
                    {/if}
                    <div class="print-tail"><span>Tail after Stop</span>
                        <Knob label="Tail after Stop" unit="seconds" min={0} max={60}
                            step={0.5} fineStep={0.1} value={tailSeconds} disabled={printState.armed}
                            oninput={(value) => { tailSeconds = value; }} />
                    </div>
                    <p>Continue recording effects after Dorico stops. Use 0 for an exact cutoff, or increase for long reverbs (up to 60 seconds).</p>
                </fieldset>
                <div class="print-status" class:recording={printState.recording} class:armed={printState.armed && !printState.recording}>
                    <strong>{printState.incomplete ? "Incomplete take" : printState.capturingTail ? "Printing tail" : printState.recording ? "Printing" : printState.finalizing ? "Finishing WAV" : printState.armed ? "Armed" : printState.fileName ? "Last print" : "Ready"}</strong>
                    <span>{formatDuration(printState.durationSeconds)} · {printState.format || "Stereo WAV · 32-bit float"}{printState.sampleRate ? ` · ${Math.round(printState.sampleRate)} Hz` : ""}</span>
                    {#if printState.filePath}
                        <span class="print-path" title={printState.filePath}>{printState.filePath}</span>
                    {/if}
                    {#if printState.files?.length > 1}
                        <span>{printState.files.length} synchronized stereo WAV files</span>
                    {/if}
                </div>
                {#if printState.armed && !printState.recording && !printState.finalizing}
                    <div class="print-instructions">Waiting for Dorico playback. The next transport Start begins every file; transport Stop finishes them together after the {printState.tailSeconds ?? tailSeconds}-second tail.</div>
                {:else if printState.capturingTail}
                    <div class="print-instructions">Capturing the {printState.tailSeconds}-second effect tail. Wait for completion before auditioning notes or starting another passage. Stop &amp; finish ends the tail immediately.</div>
                {:else if printState.recording}
                    <div class="print-instructions">Recording follows Dorico's transport and finishes automatically after playback stops and the selected tail completes.</div>
                {/if}
                {#if Number(printState.droppedBlocks || 0) > 0}
                    <div class="print-warning">The disk writer dropped {printState.droppedBlocks} audio block{Number(printState.droppedBlocks) === 1 ? "" : "s"}; this take is incomplete.</div>
                {/if}
                {#if printState.error}
                    <div class="print-warning">{printState.error}</div>
                {/if}
            </section>


        <p class="window-hint">Closing this window does not stop an armed or active print. Reopen it from View → Print Audio.</p>
    </div>
</main>

<style>
    :global(body) { margin: 0; overflow: hidden; }
    .print-pane { width: 100%; height: 100vh; display: flex; flex-direction: column; background: #0b1220; color: #e2e8f0; font-family: Inter, system-ui, sans-serif; line-height: 1.5; color-scheme: dark; }
    header { display: flex; align-items: center; justify-content: space-between; gap: 20px; padding: 20px 24px; background: #111c2e; border-bottom: 1px solid #273449; }
    h1 { margin: 0; color: #f8fafc; font-size: 1.45rem; }
    p { margin: 4px 0 0; color: #94a3b8; font-size: .86rem; }
    .content { flex: 1; min-height: 0; overflow-y: auto; padding: 24px; }
    .print-section { border: 1px solid #26364d; border-radius: 10px; background: #0f1929; padding: 20px; }
    .section-heading { display: flex; align-items: center; justify-content: space-between; gap: 20px; }
    button { font: inherit; min-height: 36px; padding: 7px 12px; border: 1px solid #475569; border-radius: 6px; background: #172337; color: #e2e8f0; cursor: pointer; }
    button:hover:not(:disabled) { border-color: #7dd3fc; background: #1e3048; }
    button:disabled { opacity: .38; cursor: default; }
    .primary { background: #075985; border-color: #38bdf8; flex-shrink: 0; }
    .window-hint { font-size: .75rem; margin-top: 14px; }
    .print-options { margin: 16px 0 0; padding: 12px; border: 1px solid #334155; border-radius: 6px; color: #cbd5e1; font-size: .8rem; }
    .print-options legend { color: #94a3b8; padding: 0 5px; }
    .print-options label { display: flex; align-items: center; gap: 8px; padding: 5px 0; }
    .print-options input { accent-color: #38bdf8; }
    .print-options .print-tail { display: flex; align-items: center; gap: 12px; margin-top: 10px; border-top: 1px solid #334155; padding-top: 12px; }
    .print-options p { font-size: .75rem; line-height: 1.5; margin-top: 8px; }
    .stem-outputs { margin-top: 8px; border-top: 1px solid #334155; padding-top: 8px; max-height: 180px; overflow-y: auto; }
    .print-status { display: flex; flex-direction: column; gap: 4px; margin-top: 16px; padding: 12px 14px; border: 1px solid #334155; border-radius: 7px; background: #111c2e; color: #94a3b8; }
    .print-status.recording { border-color: #ef4444; background: rgba(127, 29, 29, 0.2); }
    .print-status.armed { border-color: #38bdf8; background: rgba(7, 89, 133, 0.18); }
    .print-status strong { color: #f8fafc; }
    .print-status.recording strong { color: #fca5a5; }
    .print-status.armed strong { color: #7dd3fc; }
    .print-path { overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: #cbd5e1; font-size: 0.76rem; }
    .print-instructions, .print-warning { margin-top: 10px; padding: 10px 12px; border-radius: 6px; font-size: 0.8rem; line-height: 1.4; }
    .print-instructions { border: 1px solid #1d4ed8; background: rgba(30, 64, 175, 0.18); color: #bfdbfe; }
    .print-warning { border: 1px solid #991b1b; background: rgba(127, 29, 29, 0.22); color: #fecaca; }
    .stop-print { border-color: #ef4444; background: #7f1d1d; color: #fee2e2; }
    @media (max-width: 620px) {
        header, .content { padding: 16px; }
        .print-section { padding: 14px; }
        .section-heading { align-items: flex-start; flex-direction: column; gap: 12px; }
    }
</style>
