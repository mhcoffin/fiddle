<script>
    import { onMount, onDestroy } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";

    /** @type {{ stripId: string }} */
    let { stripId } = $props();

    let records = $state([]);
    let pollTimer = $state(null);

    // ── C++ → JS handler ──
    onFromCpp("setAnnotationRecords", (data) => {
        if (data.stripId === stripId) {
            records = data.records || [];
        }
    });

    const fetchRecords = () => {
        dispatchCpp("getAnnotationRecords", stripId);
    };

    onMount(() => {
        fetchRecords();
        // Poll every 500ms for new records while the inspector is open
        pollTimer = setInterval(fetchRecords, 500);
    });

    onDestroy(() => {
        if (pollTimer) clearInterval(pollTimer);
    });

    /** Format MIDI action type to human label.
     *  Must match C++ SwitchActionType enum:
     *  KeySwitch=0, NoteVelocity=1, CC=2, ControlChange=3,
     *  ChannelSwitch=4, ProgramChange=5, Unknown=6 */
    const actionTypeLabel = (type) => {
        switch (type) {
            case 0: return "Keyswitch";
            case 1: return "NoteVel";
            case 2: return "CC";
            case 3: return "CC";
            case 4: return "Channel";
            case 5: return "Program";
            default: return "?";
        }
    };

    /** Format a SwitchAction to a short string */
    const formatAction = (a) => {
        switch (a.type) {
            case 0: return `KS ${midiNoteName(a.param1)}`;
            case 2:
            case 3: return `CC${a.param1}=${a.param2}`;
            case 4: return `Ch ${a.param1}`;
            case 5: return `PC ${a.param1}`;
            default: return `${actionTypeLabel(a.type)} ${a.param1}`;
        }
    };

    /** MIDI note number to name */
    const midiNoteName = (n) => {
        const names = ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"];
        const octave = Math.floor(n / 12) - 1;
        return names[n % 12] + octave;
    };

    /** Length category label */
    const lengthLabel = (cat) => {
        switch (cat) {
            case 0: return "Very Short";
            case 1: return "Short";
            case 2: return "Medium";
            case 3: return "Long";
            case 4: return "Very Long";
            default: return "?";
        }
    };

    /** Length category color class */
    const lengthClass = (cat) => {
        switch (cat) {
            case 0: return "len-vshort";
            case 1: return "len-short";
            case 2: return "len-medium";
            case 3: return "len-long";
            case 4: return "len-vlong";
            default: return "len-default";
        }
    };

    const clearRecords = () => {
        records = [];
        dispatchCpp("clearAnnotationRecords", stripId);
    };
</script>

<div class="note-inspector">
    <div class="ni-header">
        <span class="ni-title">Note Inspector</span>
        {#if records.length > 0}
            <button class="ni-clear-btn" onclick={clearRecords}>Clear</button>
        {/if}
    </div>
    {#if records.length === 0}
        <div class="ni-empty">No notes yet — play a note to see decisions</div>
    {:else}
        <div class="ni-records">
            {#each records.toReversed() as rec, i}
                <div class="ni-record" class:skipped={rec.redundancySkipped}>
                    <!-- Row 1: Note + Input → Match -->
                    <div class="ni-row ni-match-row">
                        <span class="ni-note-name">{midiNoteName(rec.noteNumber)} ({rec.noteNumber})</span>
                        <span class="ni-input-pts">
                            {#if rec.inputTechniques.length > 0}
                                {rec.inputTechniques.join(", ")}
                            {:else}
                                <em>no PTs</em>
                            {/if}
                        </span>
                        <span class="ni-arrow">→</span>
                        <span class="ni-match {rec.baseSwitchName ? 'has-match' : 'no-match'}">
                            {#if rec.baseSwitchName}
                                <span class="ni-len-badge {lengthClass(rec.lengthCategory)}">{lengthLabel(rec.lengthCategory)}</span>
                                <span class="ni-base-name">{rec.baseSwitchName}</span>
                            {:else}
                                <span class="ni-no-match">No match</span>
                            {/if}
                        </span>
                    </div>

                    <!-- Add-ons -->
                    {#if rec.addOnNames && rec.addOnNames.length > 0}
                        <div class="ni-row ni-addons">
                            <span class="ni-label">Add-ons:</span>
                            {#each rec.addOnNames as name}
                                <span class="ni-addon-tag">{name}</span>
                            {/each}
                        </div>
                    {/if}

                    <!-- Unmatched -->
                    {#if rec.unmatchedTechniques && rec.unmatchedTechniques.length > 0}
                        <div class="ni-row ni-unmatched">
                            <span class="ni-label">Unmatched:</span>
                            {#each rec.unmatchedTechniques as pt}
                                <span class="ni-unmatched-tag">{pt}</span>
                            {/each}
                        </div>
                    {/if}



                    <!-- MIDI Actions -->
                    {#if rec.redundancySkipped}
                        <div class="ni-row ni-redundancy">
                            <span class="ni-label">MIDI:</span>
                            <span class="ni-skip-tag">skipped (same as previous)</span>
                        </div>
                    {:else if rec.midiActionsEmitted && rec.midiActionsEmitted.length > 0}
                        <div class="ni-row ni-midi">
                            <span class="ni-label">MIDI:</span>
                            {#each rec.midiActionsEmitted as action}
                                <span class="ni-midi-tag">{formatAction(action)}</span>
                            {/each}
                        </div>
                    {/if}

                    <!-- Modifiers (velocity / transpose) -->
                    {#if rec.velocityBefore !== rec.velocityAfter || rec.transposeApplied !== 0}
                        <div class="ni-row ni-modifiers">
                            {#if rec.velocityBefore !== rec.velocityAfter}
                                <span class="ni-mod">vel {rec.velocityBefore}→{rec.velocityAfter}</span>
                            {/if}
                            {#if rec.transposeApplied !== 0}
                                <span class="ni-mod">transpose {rec.transposeApplied > 0 ? '+' : ''}{rec.transposeApplied}</span>
                            {/if}
                        </div>
                    {/if}

                    <!-- Duration adjust -->
                    {#if rec.durationAdjustMs !== 0}
                        <div class="ni-row ni-duration">
                            <span class="ni-label">NoteOff adjust:</span>
                            <span class="ni-mod">{rec.durationAdjustMs > 0 ? '+' : ''}{rec.durationAdjustMs.toFixed(1)}ms</span>
                        </div>
                    {/if}

                    <!-- Candidates (Tier 3 debug) -->
                    {#if rec.candidates && rec.candidates.length > 1}
                        <details class="ni-candidates">
                            <summary class="ni-cand-summary">{rec.candidates.length} candidates considered</summary>
                            <div class="ni-cand-list">
                                {#each rec.candidates as c}
                                    <div class="ni-cand-item">
                                        <span class="ni-cand-name">{c.name}</span>
                                        <span class="ni-cand-score">score {c.score}</span>
                                    </div>
                                {/each}
                            </div>
                        </details>
                    {/if}
                </div>
            {/each}
        </div>
    {/if}
</div>

<style>
    .note-inspector {
        background: #0f172a;
        font-size: 0.75rem;
        display: flex;
        flex-direction: column;
        overflow: hidden;
        flex: 1;
        min-height: 0;
    }


    .ni-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 4px 10px;
        border-bottom: 1px solid #1e293b;
        flex-shrink: 0;
    }

    .ni-title {
        color: #94a3b8;
        font-weight: 600;
        font-size: 0.7rem;
        text-transform: uppercase;
        letter-spacing: 0.05em;
    }

    .ni-clear-btn {
        background: #1e293b;
        color: #94a3b8;
        border: 1px solid #334155;
        border-radius: 3px;
        padding: 1px 8px;
        font-size: 0.65rem;
        cursor: pointer;
        transition: all 0.15s;
    }

    .ni-clear-btn:hover {
        background: #334155;
        color: #e2e8f0;
        border-color: #475569;
    }

    .ni-empty {
        padding: 16px;
        color: #94a3b8;
        text-align: center;
        font-style: italic;
    }

    .ni-records {
        overflow-y: auto;
        flex: 1;
    }

    .ni-record {
        padding: 6px 10px;
        border-bottom: 1px solid #1e293b;
    }

    .ni-record:last-child {
        border-bottom: none;
    }

    .ni-record.skipped {
        opacity: 0.5;
    }

    .ni-row {
        display: flex;
        align-items: center;
        gap: 4px;
        flex-wrap: wrap;
        line-height: 1.6;
    }

    .ni-match-row {
        font-weight: 500;
    }

    .ni-note-name {
        color: #f1f5f9;
        font-weight: 600;
        padding-right: 6px;
        margin-right: 4px;
        border-right: 1px solid #334155;
    }

    .ni-input-pts {
        color: #cbd5e1;
    }

    .ni-input-pts em {
        color: #94a3b8;
    }

    .ni-arrow {
        color: #64748b;
        margin: 0 2px;
    }

    .ni-match {
        display: flex;
        align-items: center;
        gap: 4px;
    }

    .ni-tier-badge {
        font-size: 0.65rem;
        padding: 0 4px;
        border-radius: 3px;
        font-weight: 600;
        text-transform: uppercase;
        letter-spacing: 0.03em;
    }

    .ni-len-badge {
        padding: 0 4px;
        border-radius: 3px;
        font-weight: 600;
        font-size: 0.65em;
        text-transform: uppercase;
        letter-spacing: 0.03em;
    }

    .len-vshort .ni-len-badge, .ni-len-badge.len-vshort {
        background: #7c2d12;
        color: #fed7aa;
    }

    .len-short .ni-len-badge, .ni-len-badge.len-short {
        background: #854d0e;
        color: #fde68a;
    }

    .len-medium .ni-len-badge, .ni-len-badge.len-medium {
        background: #166534;
        color: #86efac;
    }

    .len-long .ni-len-badge, .ni-len-badge.len-long {
        background: #1e3a5f;
        color: #93c5fd;
    }

    .len-vlong .ni-len-badge, .ni-len-badge.len-vlong {
        background: #312e81;
        color: #c4b5fd;
    }

    .no-match .ni-no-match {
        color: #ef4444;
    }

    .ni-base-name {
        color: #e2e8f0;
    }

    .ni-label {
        color: #94a3b8;
        min-width: 55px;
    }

    .ni-addon-tag,
    .ni-midi-tag {
        background: #1e293b;
        color: #38bdf8;
        padding: 0 4px;
        border-radius: 3px;
        border: 1px solid #334155;
    }

    .ni-unmatched-tag {
        background: #451a03;
        color: #fdba74;
        padding: 0 4px;
        border-radius: 3px;
        border: 1px solid #9a3412;
    }

    .ni-skip-tag {
        color: #94a3b8;
        font-style: italic;
    }

    .ni-cond-text {
        color: #cbd5e1;
    }

    .ni-cond-pass {
        color: #4ade80;
    }

    .ni-cond-fail {
        color: #ef4444;
        font-weight: 600;
    }

    .ni-cond-fb {
        background: #854d0e;
        color: #fde68a;
        padding: 0 4px;
        border-radius: 3px;
        font-size: 0.65rem;
    }

    .ni-mod {
        color: #c084fc;
        background: #1e1b4b;
        padding: 0 4px;
        border-radius: 3px;
        border: 1px solid #312e81;
    }

    .ni-candidates {
        margin-top: 2px;
    }

    .ni-cand-summary {
        color: #94a3b8;
        cursor: pointer;
        font-size: 0.7rem;
    }

    .ni-cand-summary:hover {
        color: #94a3b8;
    }

    .ni-cand-list {
        padding-left: 8px;
        margin-top: 2px;
    }

    .ni-cand-item {
        display: flex;
        justify-content: space-between;
        gap: 8px;
        color: #94a3b8;
        font-size: 0.7rem;
    }

    .ni-cand-name {
        color: #cbd5e1;
    }

    .ni-cand-score {
        color: #94a3b8;
    }
</style>
