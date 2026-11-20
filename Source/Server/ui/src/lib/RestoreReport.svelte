<script>
    // Banner shown when a Dorico project connects: which Fiddle setup loaded,
    // what differs from when the project was saved, and what to do about it.
    import { onDestroy } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";
    import { restoreProblems, showRestoreReport } from "./restoreReport.js";

    let report = $state(null);
    let strips = $state([]);
    const problems = $derived(restoreProblems(strips));
    const visible = $derived(showRestoreReport(report, problems));

    const unsubscribers = [
        onFromCpp("setRestoreReport", (data) => { report = data && data.kind ? data : null; }),
        onFromCpp("setMixerState", (data) => { strips = Array.isArray(data) ? data : []; }),
    ];
    onDestroy(() => unsubscribers.forEach((off) => off()));

    function dismiss() {
        report = null;
        dispatchCpp("dismissRestoreReport");
    }
</script>

{#if visible}
    <div class="restore-report" class:warning={report.severity === "warning"} role="status" aria-live="polite">
        <div class="text">
            <strong>{report.title}</strong>
            <span>{report.message}</span>
            {#if report.options?.length}
                <ul class="options">
                    {#each report.options as option}
                        <li><b>{option.label}</b>{option.text ? `: ${option.text}` : ""}</li>
                    {/each}
                </ul>
            {/if}
            {#each problems as problem}
                <span class="problem">{problem}</span>
            {/each}
        </div>
        <div class="actions">
            {#if report.action === "useCurrentSetup"}
                <button type="button" class="primary" onclick={() => dispatchCpp("useCurrentSetupForProject")}>Use current setup</button>
            {:else if report.action === "retry"}
                <button type="button" class="primary" onclick={() => dispatchCpp("retryProjectRestore")}>Load project setup</button>
            {/if}
            <button type="button" aria-label="Dismiss project restore report" onclick={dismiss}>Dismiss</button>
        </div>
    </div>
{/if}

<style>
    .restore-report {
        flex: 0 0 auto;
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 20px;
        padding: 12px 18px;
        border-bottom: 1px solid rgba(96, 165, 250, 0.45);
        background: #12263f;
        color: #dbeafe;
        box-shadow: 0 4px 14px rgba(0, 0, 0, 0.28);
        z-index: 100;
    }
    .restore-report.warning {
        border-bottom-color: rgba(251, 191, 36, 0.55);
        background: #3f2f0b;
        color: #fef3c7;
    }
    .text { display: flex; flex-direction: column; gap: 3px; min-width: 0; }
    strong { color: #bfdbfe; }
    .warning strong { color: #fde68a; }
    span { font-size: 0.9rem; }
    .problem::before { content: "• "; }
    .options { margin: 2px 0 0; padding-left: 20px; display: flex; flex-direction: column; gap: 2px; font-size: 0.9rem; }
    .actions { display: flex; gap: 8px; flex: 0 0 auto; }
    button {
        padding: 7px 13px;
        border: 1px solid #3b6fb0;
        border-radius: 6px;
        background: #1c3a5e;
        color: #e0edff;
        font: inherit;
        font-weight: 650;
        cursor: pointer;
    }
    button:hover { background: #24507f; }
    .warning button { border-color: #d6a629; background: #5b4311; color: #fff4bd; }
    .warning button:hover { background: #765719; }
    button.primary { background: #2b5f98; }
    .warning button.primary { background: #7a5a14; }
    button:focus-visible { outline: 2px solid #93c5fd; outline-offset: 2px; }
</style>
