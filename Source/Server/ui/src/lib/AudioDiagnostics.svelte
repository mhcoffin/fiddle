<script>
    import { onMount } from "svelte";
    import { onFromCpp, dispatchCpp } from "./ipc.js";
    import { audioCpuLabel, appendDiagnosticSample, diagnosticReport, pluginTimingRows } from "./audioDiagnostics.js";

    let data = $state(null);
    let receivedAt = $state(0);
    let now = $state(Date.now());
    let history = $state([]);
    let marks = $state([]);
    let open = $state(false);
    let dialog = $state(null);
    let copied = $state(false);
    const fixed = (value, digits = 1) => Number.isFinite(value) ? value.toFixed(digits) : "—";
    const nativeValue = (key) => data?.returnFresh && now - receivedAt < 3000 ? data[key] : "—";
    let label = $derived(audioCpuLabel(data, now - receivedAt));
    let pluginRows = $derived(pluginTimingRows(data));
    let rateMismatch = $derived(data?.returnFresh && data?.sampleRate > 0 && data?.returnSampleRate > 0 && Math.abs(data.sampleRate - data.returnSampleRate) > 1);

    onMount(() => {
        const unsubscribe = onFromCpp("setAudioDiagnostics", (value) => {
            now = Date.now();
            receivedAt = now;
            data = value;
            history = appendDiagnosticSample(history, value, now);
        });
        const timer = setInterval(() => { now = Date.now(); }, 1000);
        return () => { unsubscribe(); clearInterval(timer); };
    });

    $effect(() => {
        if (!dialog) return;
        if (open && !dialog.open) dialog.showModal();
        else if (!open && dialog.open) dialog.close();
    });

    function markCrackle() {
        marks = [...marks.slice(-9), { time: new Date().toISOString(), snapshot: data }];
        copied = false;
    }
    function copyReport() {
        dispatchCpp("copyAudioDiagnosticsReport", diagnosticReport(history, marks));
        copied = true;
    }
</script>

<button class="cpu" class:hot={data?.running && data?.peakLoad >= 100}
    title="Audio block time-budget usage. Click for peaks, overruns and return-buffer diagnostics."
    aria-haspopup="dialog" onclick={() => { open = true; copied = false; }}>{label}</button>

<dialog bind:this={dialog} onclose={() => { open = false; }} aria-labelledby="audio-diagnostics-title">
    <header>
        <div><h2 id="audio-diagnostics-title">Audio performance</h2>
            <p>100% takes one block's duration to render. A queued reserve can absorb a brief spike. This is not total machine CPU.</p></div>
        <button onclick={() => { open = false; }}>Close</button>
    </header>
    <div class="content">
        <div class="headline">{label}<span>Recent peak {fixed(data?.peakLoad)}%</span></div>
        <section class="device">
            <h3>Audio setup · {data?.buildConfiguration ?? "Unknown build"}</h3>
            <p>{data?.device?.name ?? "No active device"} · {data?.device?.type ?? "—"}<br />
                {data?.device?.sampleRate ?? "—"} Hz · {data?.device?.bufferSize ?? "—"} frames
                ({fixed(1000 * data?.device?.bufferSize / data?.device?.sampleRate, 2)} ms per block)</p>
            <button onclick={() => { open = false; dispatchCpp("showAudioSettings"); }}>Audio Settings…</button>
            <p>Stop playback before changing settings. These are local to this Mac, not saved in project versions. Dorico has its own audio settings.</p>
            {#if data?.device?.warning}<p class="warning">{data.device.warning}</p>{/if}
        </section>
        {#if rateMismatch}<p class="warning">Sample rates differ between Fiddle and Dorico. Audio is not resampled on this connection.</p>{/if}
        {#if data?.renderAheadError}<p class="warning">{data.renderAheadError}</p>{/if}
        {#if data?.renderAhead?.enabled}
            <section class="device">
                <h3>Render-ahead reserve</h3>
                <p>{fixed(data.renderAhead.queuedMs)} ms queued · {fixed(data.renderAhead.targetMs)} ms target<br />
                    Playback delay: {data.effectiveDelayMs} ms effective / {data.requestedDelayMs} ms requested</p>
                <p>The worker follows Dorico's sample consumption. The reserve uses part of the playback delay; it is not added to it. Live control changes can take up to the queued duration to be heard.</p>
                <p>Skipped late frames: {data.renderAhead.skippedFrames ?? 0} · Host clock age: {fixed(data.renderAhead.hostClockAgeMs)} ms</p>
                {#if !data.renderAhead.realtimeScheduling}<p class="warning">Real-time scheduling was unavailable; the worker is using high priority.</p>{/if}
                {#if data.returnFresh && data.returnProtocol !== 2}<p class="warning">The Dorico plugin uses an older audio protocol. Close Dorico and install the rebuilt Fiddle plugin.</p>{/if}
            </section>
        {/if}
        <div class="columns">
            <section>
                <h3>Fiddle rendering</h3>
                <dl>
                    <dt>Sample rate / current block</dt><dd>{data?.sampleRate ?? "—"} Hz / {data?.blockSize ?? "—"} frames</dd>
                    <dt>Smallest / largest block</dt><dd>{data?.minBlockSize ?? "—"} / {data?.maxBlockSize ?? "—"} frames</dd>
                    <dt>Highest block load</dt><dd>{fixed(data?.maxLoad)}%</dd>
                    <dt>Plugin calls / other Fiddle work</dt><dd>{fixed(data?.pluginLoad)}% / {fixed(data?.otherLoad)}%</dd>
                    <dt>Longest render</dt><dd>{fixed(data?.maxRenderMs, 2)} ms</dd>
                    <dt>Deadline overruns</dt><dd>{data?.overruns ?? "—"}</dd>
                    <dt>Last overrun</dt><dd>{!data ? "—" : data.lastOverrunAgeMs >= 0 ? `${fixed(data.lastOverrunAgeMs / 1000)} s ago` : "None recorded"}</dd>
                    {#if !data?.renderAhead?.enabled}
                    <dt>Long callback gaps (&gt;1.5 blocks)</dt><dd>{data?.longGaps ?? "—"}</dd>
                    <dt>Longest callback gap</dt><dd>{fixed(data?.maxGapMs, 2)} ms</dd>
                    <dt>Recent longest callback gap</dt><dd>{fixed(data?.recentMaxGapMs, 2)} ms</dd>
                    <dt>Last long gap / preceding render</dt><dd>{fixed(data?.lastLongGapMs, 2)} / {fixed(data?.renderBeforeLongGapMs, 2)} ms</dd>
                    {/if}
                    <dt>JUCE device xruns</dt><dd>{data?.deviceXruns ?? "—"}</dd>
                    <dt>Return-ring overflow blocks</dt><dd>{data?.ringOverflows ?? "—"}</dd>
                    <dt>Ring unavailable blocks</dt><dd>{data?.unavailableBlocks ?? "—"}</dd>
                    <dt>Dropped diagnostic reports</dt><dd>{data?.droppedReports ?? "—"}</dd>
                </dl>
            </section>
            <section>
                <h3>Dorico audio return</h3>
                {#if !data?.returnFresh || now - receivedAt >= 3000}
                    <p class="warning">Return telemetry unavailable or stale. Load the rebuilt Fiddle plugin in Dorico and connect it.</p>
                {/if}
                <dl>
                    <dt>Host sample rate</dt><dd>{nativeValue("returnSampleRate")} Hz</dd>
                    <dt>Return callbacks</dt><dd>{nativeValue("returnCallbacks")}</dd>
                    <dt>Underrun episodes</dt><dd>{nativeValue("underruns")}</dd>
                    <dt>Underrun silence (frames)</dt><dd>{nativeValue("bufferingFrames")}</dd>
                    <dt>Unavailable-ring silence (frames)</dt><dd>{nativeValue("unavailableFrames")}</dd>
                    <dt>Dropped outgoing MIDI events</dt><dd>{nativeValue("droppedMidiEvents")}</dd>
                </dl>
                <p>One underrun episode can produce several silent blocks. With render-ahead, initial priming silence is excluded; missed audio is not replayed later.</p>
                <p>Counts and maxima accumulate while these objects live. Restart Fiddle for fresh server counters; recreate the Dorico plugin for fresh return counters. JUCE xruns may overlap our overrun count—do not add them together.</p>
            </section>
        </div>
        <section class="timings">
            <h3>Instrument and FX processing</h3>
            <p>Sorted by recent average wall time, including waits inside each player. Independent windows: peaks may come from different callbacks and must not be added together. “Other Fiddle work” includes mixing, MIDI handling, graph overhead and diagnostic overhead.</p>
            <div class="table-scroll"><table>
                <thead><tr><th>Layer / bus · plugin</th><th>Average</th><th>Recent peak</th><th>Highest</th><th>Block</th></tr></thead>
                <tbody>
                    {#each pluginRows as row}
                        <tr class:stale={!row.fresh}>
                            <td><strong>{row.owner}</strong><br />{row.plugin}<br /><small>{row.kind}{row.bypassed ? " · Bypassed" : !row.fresh ? " · No recent processing" : ""}</small></td>
                            <td>{row.fresh ? fixed(row.averageMs, 2) : "—"} ms</td>
                            <td>{row.fresh ? fixed(row.peakMs, 2) : "—"} ms</td>
                            <td>{fixed(row.maxMs, 2)} ms</td>
                            <td>{row.blockSize || "—"}</td>
                        </tr>
                    {:else}<tr><td colspan="5">No loaded instruments or effects.</td></tr>{/each}
                </tbody>
            </table></div>
            <p>Highest times last for each loaded plugin instance. Removing or replacing it starts fresh timing. Buffer changes re-prepare plugins; plugin-private streaming and multicore settings remain in the player's editor.</p>
        </section>
        <p>Mark an audible glitch, then copy the report here. The report includes up to two minutes of one-second observations and ten listening marks. These are receipt timestamps, not sample-accurate glitch locations.</p>
    </div>
    <footer>
        <button onclick={markCrackle}>Mark crackle</button>
        <button onclick={copyReport}>{copied ? "Report copied" : "Copy report"}</button>
        <span>{marks.length} listening marks · {history.length} recent samples</span>
    </footer>
</dialog>

<style>
    button { color: #e2e8f0; background: #182638; border: 1px solid #64748b; border-radius: 6px; padding: 9px 14px; font-size: 14px; cursor: pointer; white-space: nowrap; }
    button:hover { background: #29405a; }
    button:focus-visible { outline: 2px solid #7dd3fc; outline-offset: 2px; }
    .cpu { min-width: 145px; font-variant-numeric: tabular-nums; color: #bae6fd; }
    .hot { border-color: #fb923c; color: #fed7aa; }
    dialog { width: min(920px, 92vw); max-height: 88vh; padding: 0; background: #0f1929; color: #e2e8f0; border: 1px solid #64748b; border-radius: 12px; font-size: 15px; }
    dialog[open] { display: flex; flex-direction: column; overflow: hidden; }
    dialog::backdrop { background: #0009; }
    header { display: flex; flex-shrink: 0; align-items: center; justify-content: space-between; gap: 20px; padding: 22px 26px; border-bottom: 1px solid #334155; }
    h2, h3 { margin: 0 0 10px; }
    p { color: #aebdd0; line-height: 1.5; margin: 8px 0; }
    .content { padding: 22px 26px; overflow: auto; min-height: 0; }
    .headline { font-size: 26px; margin-bottom: 24px; font-variant-numeric: tabular-nums; }
    .headline span { font-size: 17px; margin-left: 24px; color: #aebdd0; }
    .columns { display: grid; grid-template-columns: 1fr 1fr; gap: 30px; }
    dl { display: grid; grid-template-columns: 1fr auto; gap: 13px 16px; }
    dt { color: #aebdd0; } dd { margin: 0; text-align: right; font-variant-numeric: tabular-nums; }
    .warning { color: #fed7aa; }
    .device, .timings { border-top: 1px solid #334155; padding-top: 18px; margin: 18px 0; }
    .table-scroll { overflow-x: auto; }
    table { width: 100%; border-collapse: collapse; font-variant-numeric: tabular-nums; }
    th, td { padding: 12px 10px; text-align: right; border-bottom: 1px solid #334155; white-space: nowrap; }
    th:first-child, td:first-child { text-align: left; white-space: normal; min-width: 230px; overflow-wrap: anywhere; }
    small, .stale { color: #aebdd0; }
    footer { display: flex; flex-shrink: 0; flex-wrap: wrap; gap: 14px; align-items: center; border-top: 1px solid #334155; padding: 16px 26px; }
    footer span { color: #aebdd0; }
    @media (max-width: 700px) { .columns { grid-template-columns: 1fr; } }
</style>
