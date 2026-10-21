<script>
    import { onMount } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { formatLength, instrumentLabel, normalizeState, recordingStatus, runHeadline } from "./scoreTests.js";

    let { visible = true, onClose = () => {} } = $props();
    let state = $state(normalizeState(null));
    let copied = $state(false);
    let armedTitle = $derived(state.tests.find(test => test.id === state.armedTest)?.title || state.armedTest);

    onMount(() => onFromCpp("scoreTestState", value => { state = normalizeState(value); }));
    $effect(() => {
        if (!visible) return;
        dispatchCpp("requestScoreTests");
        // Live note count while armed.
        const timer = setInterval(() => { if (state.armed) dispatchCpp("requestScoreTests"); }, 1000);
        return () => clearInterval(timer);
    });
    const copyRunId = () => {
        dispatchCpp("copyScoreTestText", state.lastRun.run_id);
        copied = true;
        setTimeout(() => { copied = false; }, 1500);
    };
</script>

<main aria-label="Score Tests">
    <header>
        <div>
            <h1>Score Tests</h1>
            <p>Play a test score in Dorico and check that Fiddle receives it as written.</p>
        </div>
        <button onclick={onClose}>Close</button>
    </header>

    {#if state.error}<div class="notice error" role="alert">{state.error}</div>{/if}

    {#if state.armed}
        <section class="armed" aria-label="Armed test">
            <div>
                <h2>Armed: {armedTitle}</h2>
                <p>Open the score in Dorico, play it from the start to the end, then press Done.</p>
                <p class="status" aria-live="polite">{recordingStatus(state)}</p>
            </div>
            <div class="actions">
                <button onclick={() => dispatchCpp("openScoreTestScore", state.armedTest)}>Open in Dorico</button>
                <button class="primary" onclick={() => dispatchCpp("finishScoreTest")}>Done</button>
                <button onclick={() => dispatchCpp("cancelScoreTest")}>Cancel</button>
            </div>
        </section>
    {/if}

    {#if state.lastRun}
        <section class="result" class:pass={state.lastRun.pass} class:fail={!state.lastRun.pass} aria-label="Last run">
            <div class="result-head">
                <div>
                    <h2>{state.lastRun.title}: {runHeadline(state.lastRun)}</h2>
                    <p class="run-id">Run ID <code>{state.lastRun.run_id}</code></p>
                </div>
                <div class="actions">
                    <button onclick={copyRunId}>{copied ? "Copied" : "Copy run ID"}</button>
                    <button onclick={() => dispatchCpp("revealScoreTestRun", state.lastRun.run_id)}>Show run record</button>
                </div>
            </div>
            {#if state.lastRun.failures?.length}
                <ul class="failures">
                    {#each state.lastRun.failures as failure}<li>{failure}</li>{/each}
                </ul>
                {#if state.lastRun.more_failures}<p>…and {state.lastRun.more_failures} more in the run record.</p>{/if}
            {/if}
            <p class="hint">To ask Claude about this run, give it the run ID.</p>
        </section>
    {/if}

    <div class="table-scroll">
        <table>
            <thead><tr><th>Test</th><th>Instruments</th><th>Notes</th><th>Length</th><th>Score</th><th></th></tr></thead>
            <tbody>
                {#each state.tests as test (test.id)}
                    <tr class:armed-row={state.armed && state.armedTest === test.id}>
                        <td><strong>{test.title}</strong><small>{test.description}</small></td>
                        <td>{(test.instruments || []).map(instrumentLabel).join(", ")}</td>
                        <td>{test.notes}</td>
                        <td>{formatLength(test.length_ms)}</td>
                        <td class="links">
                            <button class="link" onclick={() => dispatchCpp("openScoreTestScore", test.id)}
                                    aria-label={`Open ${test.musicxml} in Dorico`} title={test.musicxml_path}>{test.musicxml}</button>
                            <button class="link small" onclick={() => dispatchCpp("revealScoreTestScore", test.id)}>Show in Finder</button>
                        </td>
                        <td>
                            <button aria-label={`Arm ${test.title}`} disabled={state.armed}
                                    onclick={() => dispatchCpp("armScoreTest", test.id)}>Arm</button>
                        </td>
                    </tr>
                {:else}
                    <tr><td colspan="6" class="empty">No score tests found.</td></tr>
                {/each}
            </tbody>
        </table>
    </div>

    <details class="setup">
        <summary>Before your first run</summary>
        <ol>
            <li>In Fiddle, make one chair per instrument the tests use (Violin, Flute), each with a layer, then install the playback template from Chairs (with Dorico closed).</li>
            <li>In Dorico, Library ▸ Playback Options: set humanize (start, end, dynamics) and the beat dynamic increases to 0, leave pitch contour emphasis off, and press Save as Default.</li>
            <li>Make Fiddle Dorico's default playback template (Preferences ▸ Play), or apply it to each test project.</li>
        </ol>
        <p>Details: tests/score/README.md, “One-time setup”.</p>
        <p class="hint">Run records: {state.runsFolder} <button class="link small" onclick={() => dispatchCpp("revealScoreTestRun", "")}>Show</button></p>
    </details>
</main>

<style>
    main { height:100vh; display:flex; flex-direction:column; overflow:hidden; background:#0b1220; font-size:13px; }
    header { display:flex; align-items:center; justify-content:space-between; padding:18px 24px; gap:16px; background:#111c2e; border-bottom:1px solid #334155; }
    h1 { font-size:23px; margin:0; color:#f1f5f9; } h2 { font-size:16px; margin:0 0 4px; color:#f1f5f9; }
    p { margin:6px 0; color:#aebdd0; line-height:1.5; }
    button { font:inherit; color:inherit; background:#172337; border:1px solid #475569; border-radius:6px; padding:7px 10px; cursor:pointer; }
    button:disabled { opacity:.4; cursor:default; } button:hover:not(:disabled) { border-color:#7dd3fc; } :focus-visible { outline:2px solid #7dd3fc; outline-offset:2px; }
    button.primary { background:#0e4a6b; border-color:#7dd3fc; font-weight:600; }
    button.link { background:transparent; border:0; padding:0; color:#7dd3fc; text-decoration:underline; text-align:left; overflow-wrap:anywhere; }
    button.small { font-size:11px; }
    .actions { display:flex; gap:8px; flex-wrap:wrap; align-items:center; }
    .notice { padding:10px 24px; } .error { background:#3b1d24; color:#fecaca; }
    section { padding:14px 24px; border-bottom:1px solid #334155; }
    .armed { display:flex; justify-content:space-between; gap:16px; align-items:center; flex-wrap:wrap; background:#14283d; }
    .status { color:#bae6fd; }
    .result.pass { background:#12291f; } .result.fail { background:#2a1a1f; }
    .result-head { display:flex; justify-content:space-between; gap:16px; align-items:flex-start; flex-wrap:wrap; }
    .run-id code { font-size:12px; color:#f1f5f9; background:#0b1220; padding:2px 6px; border-radius:4px; user-select:all; }
    .failures { margin:8px 0; padding-left:20px; max-height:180px; overflow:auto; font-size:12px; color:#fecaca; }
    .failures li { margin:2px 0; overflow-wrap:anywhere; }
    .hint { font-size:12px; color:#94a3b8; }
    .table-scroll { overflow:auto; flex:1; min-height:0; }
    table { width:100%; border-collapse:collapse; }
    th { position:sticky; top:0; background:#172337; text-align:left; font-size:12px; z-index:1; }
    th, td { padding:10px 12px; border-bottom:1px solid #273449; vertical-align:top; } td { font-size:12px; }
    small { display:block; font-size:11px; color:#94a3b8; margin-top:2px; max-width:520px; }
    .links { display:flex; flex-direction:column; gap:4px; }
    tr.armed-row { background:#17324a; }
    .empty { padding:24px; color:#94a3b8; }
    .setup { padding:10px 24px; border-top:1px solid #334155; background:#0f1929; max-height:40vh; overflow:auto; }
    .setup summary { cursor:pointer; color:#bae6fd; } .setup ol { margin:8px 0; padding-left:20px; color:#aebdd0; } .setup li { margin:4px 0; }
    @media (max-width:750px) { main { overflow:auto; } .table-scroll { flex:none; } th:nth-child(3), td:nth-child(3), th:nth-child(4), td:nth-child(4) { display:none; } }
</style>
