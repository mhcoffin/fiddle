<script>
    /** @type {import('svelte')} */
    import { onMount } from "svelte";
    import InstrumentList from "./InstrumentList.svelte";

    /**
     * Each "chair" in the ensemble — one individual player or section slot.
     * @typedef {{ entityID: string, name: string, family: string, musicXMLSoundID: string, isSolo: boolean, id: string }} Chair
     */

    let allInstruments = $state([]);
    /** @type {Chair[]} */
    let chairs = $state([]);
    let searchQuery = $state("");
    let familyFilter = $state("");
    let families = $state([]);
    let statusMessage = $state("");
    let statusIsError = $state(false);
    let isLoading = $state(true);

    const getNative = (name) => {
        const w = /** @type {any} */ (window);
        return (
            (w.__JUCE__ && w.__JUCE__.backend && w.__JUCE__.backend[name]) ||
            window[name] ||
            (w.juce && w.juce[name]) ||
            (w.__juce__ && w.__juce__[name])
        );
    };

    // ── Roman numeral helper ──────────────────────────────────────
    const toRoman = (n) => {
        const vals = [10, 9, 5, 4, 1];
        const syms = ["X", "IX", "V", "IV", "I"];
        let result = "";
        for (let i = 0; i < vals.length; i++) {
            while (n >= vals[i]) {
                result += syms[i];
                n -= vals[i];
            }
        }
        return result;
    };

    // ── Derive display labels from chairs ─────────────────────────
    /** Get a display label for a chair, based on its position among siblings
     *  Solo: "Violin 1", "Violin 2" (Arabic)
     *  Section: "Violin I", "Violin II" (Roman)   */
    let labeledChairs = $derived.by(() => {
        // Count how many solo and section of each entityID precede each chair
        const soloCounters = {};
        const sectionCounters = {};
        const soloTotals = {};
        const sectionTotals = {};

        // Sort: group by instrument name, solos before sections
        const sorted = [...chairs].sort((a, b) => {
            const nameCompare = a.name.localeCompare(b.name);
            if (nameCompare !== 0) return nameCompare;
            // Solos before sections for same instrument
            if (a.isSolo !== b.isSolo) return a.isSolo ? -1 : 1;
            return 0;
        });

        // First pass: count totals per entityID for solo/section
        for (const c of sorted) {
            if (c.isSolo)
                soloTotals[c.entityID] = (soloTotals[c.entityID] || 0) + 1;
            else
                sectionTotals[c.entityID] =
                    (sectionTotals[c.entityID] || 0) + 1;
        }

        return sorted.map((c) => {
            let label;
            if (c.isSolo) {
                const idx = (soloCounters[c.entityID] =
                    (soloCounters[c.entityID] || 0) + 1);
                label =
                    soloTotals[c.entityID] > 1 ? `${c.name} ${idx}` : c.name;
            } else {
                const idx = (sectionCounters[c.entityID] =
                    (sectionCounters[c.entityID] || 0) + 1);
                label =
                    sectionTotals[c.entityID] > 1
                        ? `${c.name} ${toRoman(idx)}`
                        : c.name;
            }
            return { ...c, label };
        });
    });

    // ── Global window callbacks for C++ backend ───────────────────

    /** @type {any} */
    const w = window;

    w.setDoricoInstruments = (jsonStr) => {
        try {
            allInstruments = JSON.parse(jsonStr);
            const familySet = new Set(
                allInstruments.map((i) => i.family).filter(Boolean),
            );
            families = [...familySet].sort();
            console.log(
                `[Setup] Received ${allInstruments.length} instruments, ${families.length} families`,
            );
        } catch (e) {
            console.error("[Setup] Failed to parse instruments JSON:", e);
            statusMessage = "Failed to load instruments from Dorico";
            statusIsError = true;
        }
        isLoading = false;
    };

    w.setSelectedInstruments = (jsonStr) => {
        try {
            const parsed = JSON.parse(jsonStr);
            // Expand EnsembleSlots (soloCount/sectionCount) into individual chairs
            /** @type {Chair[]} */
            const expanded = [];
            for (const s of parsed) {
                const soloCount = s.soloCount ?? 1;
                const sectionCount = s.sectionCount ?? 1;
                for (let i = 0; i < soloCount; i++) {
                    expanded.push({
                        entityID: s.entityID,
                        name: s.name,
                        musicXMLSoundID: s.musicXMLSoundID || "",
                        family: s.family || "",
                        isSolo: true,
                        id: crypto.randomUUID(),
                    });
                }
                for (let i = 0; i < sectionCount; i++) {
                    expanded.push({
                        entityID: s.entityID,
                        name: s.name,
                        musicXMLSoundID: s.musicXMLSoundID || "",
                        family: s.family || "",
                        isSolo: false,
                        id: crypto.randomUUID(),
                    });
                }
            }
            chairs = expanded;
            console.log(`[Setup] Expanded to ${chairs.length} chairs`);
        } catch (e) {
            console.error("[Setup] Failed to parse selected JSON:", e);
        }
    };

    w.setSaveResult = (resultStr) => {
        if (resultStr && typeof resultStr === "string") {
            if (resultStr.startsWith("OK:")) {
                statusMessage = resultStr.substring(4);
                statusIsError = false;
            } else if (resultStr.startsWith("Error:")) {
                statusMessage = resultStr;
                statusIsError = true;
            } else {
                statusMessage = resultStr;
                statusIsError = false;
            }
        } else {
            statusMessage = "No response from backend";
            statusIsError = true;
        }
    };

    onMount(() => {
        const fn = getNative("requestSetupData");
        if (fn) {
            fn();
        } else {
            setTimeout(() => {
                const retry = getNative("requestSetupData");
                if (retry) retry();
                else {
                    isLoading = false;
                    statusMessage = "Could not connect to backend";
                    statusIsError = true;
                }
            }, 500);
        }
    });

    // ── Filtered instrument browser ───────────────────────────────
    let filteredInstruments = $derived.by(() => {
        let result = allInstruments;
        if (searchQuery.trim()) {
            const q = searchQuery.trim().toLowerCase();
            result = result.filter(
                (i) =>
                    i.name.toLowerCase().includes(q) ||
                    i.entityID.toLowerCase().includes(q),
            );
        }
        if (familyFilter) {
            result = result.filter((i) => i.family === familyFilter);
        }
        return result;
    });

    // ── Actions ───────────────────────────────────────────────────

    const addChair = (instr, isSolo) => {
        chairs = [
            ...chairs,
            {
                entityID: instr.entityID,
                name: instr.name,
                musicXMLSoundID: instr.musicXMLSoundID || "",
                family: instr.family || "",
                isSolo,
                id: crypto.randomUUID(),
            },
        ];
    };

    const removeChair = (id) => {
        chairs = chairs.filter((c) => c.id !== id);
    };

    /** Aggregate chairs back into EnsembleSlot format for the backend */
    const chairsToSlots = () => {
        const slotMap = new Map();
        for (const c of chairs) {
            let slot = slotMap.get(c.entityID);
            if (!slot) {
                slot = {
                    entityID: c.entityID,
                    name: c.name,
                    musicXMLSoundID: c.musicXMLSoundID,
                    family: c.family,
                    soloCount: 0,
                    sectionCount: 0,
                };
                slotMap.set(c.entityID, slot);
            }
            if (c.isSolo) slot.soloCount++;
            else slot.sectionCount++;
        }
        return [...slotMap.values()];
    };

    const saveAndGenerate = () => {
        statusMessage = "Saving and generating...";
        statusIsError = false;

        const slots = chairsToSlots();
        const json = JSON.stringify(slots);
        const fn = getNative("saveSelectedInstruments");
        if (fn) {
            fn(json);
        } else {
            statusMessage = "Backend not available";
            statusIsError = true;
        }
    };
</script>

<div class="setup-container">
    {#if isLoading}
        <div class="setup-loading">
            <p>Loading instruments from Dorico...</p>
        </div>
    {:else}
        <div class="setup-panels">
            <!-- Left: Instrument Browser -->
            <div class="browser-panel">
                <div class="panel-header">
                    <h3>Dorico Instruments</h3>
                    <span class="count-badge">{filteredInstruments.length}</span
                    >
                </div>

                <div class="browser-filters">
                    <input
                        type="text"
                        class="search-input"
                        placeholder="Search instruments..."
                        bind:value={searchQuery}
                    />
                    <select class="family-select" bind:value={familyFilter}>
                        <option value="">All Families</option>
                        {#each families as fam}
                            <option value={fam}>{fam}</option>
                        {/each}
                    </select>
                </div>

                <div class="instrument-list">
                    {#each filteredInstruments as instr (instr.entityID)}
                        <div class="instrument-row">
                            <div class="instr-info">
                                <span class="instr-name">{instr.name}</span>
                                <span class="instr-family"
                                    >{instr.family || "—"}</span
                                >
                            </div>
                            <div class="add-buttons">
                                <button
                                    class="add-btn solo"
                                    title="Add solo player"
                                    onclick={() => addChair(instr, true)}
                                >
                                    <svg
                                        viewBox="0 0 20 20"
                                        width="16"
                                        height="16"
                                        fill="currentColor"
                                    >
                                        <circle cx="10" cy="6" r="3.5" />
                                        <path
                                            d="M3.5 17.5c0-3.6 2.9-6.5 6.5-6.5s6.5 2.9 6.5 6.5"
                                        />
                                        <line
                                            x1="15"
                                            y1="3"
                                            x2="15"
                                            y2="9"
                                            stroke="currentColor"
                                            stroke-width="1.5"
                                        />
                                        <line
                                            x1="12"
                                            y1="6"
                                            x2="18"
                                            y2="6"
                                            stroke="currentColor"
                                            stroke-width="1.5"
                                        />
                                    </svg>
                                </button>
                                <button
                                    class="add-btn section"
                                    title="Add section"
                                    onclick={() => addChair(instr, false)}
                                >
                                    <svg
                                        viewBox="0 0 24 20"
                                        width="20"
                                        height="16"
                                        fill="currentColor"
                                    >
                                        <circle cx="8" cy="6" r="3" />
                                        <path
                                            d="M2 17c0-3.3 2.7-6 6-6s6 2.7 6 6"
                                        />
                                        <circle cx="16" cy="6" r="3" />
                                        <path
                                            d="M10 17c0-3.3 2.7-6 6-6s6 2.7 6 6"
                                        />
                                        <line
                                            x1="19"
                                            y1="3"
                                            x2="19"
                                            y2="9"
                                            stroke="currentColor"
                                            stroke-width="1.5"
                                        />
                                        <line
                                            x1="16"
                                            y1="6"
                                            x2="22"
                                            y2="6"
                                            stroke="currentColor"
                                            stroke-width="1.5"
                                        />
                                    </svg>
                                </button>
                            </div>
                        </div>
                    {/each}
                    {#if filteredInstruments.length === 0}
                        <div class="empty-state">
                            No instruments match your search.
                        </div>
                    {/if}
                </div>
            </div>

            <!-- Right: Ensemble Chairs -->
            <div class="selected-panel">
                <div class="panel-header">
                    <h3>Ensemble</h3>
                    <span class="count-badge">{chairs.length} players</span>
                </div>

                <div class="selected-list">
                    <InstrumentList
                        instruments={labeledChairs.map((c, i) => ({
                            id: c.id,
                            name: c.label,
                            family: c.family || "strings",
                            isSection: !c.isSolo,
                            channel: (i % 16) + 1,
                        }))}
                        onremove={(id) => removeChair(id)}
                    />
                </div>

                <div class="action-bar">
                    {#if statusMessage}
                        <div
                            class="status-message"
                            class:error={statusIsError}
                            class:success={!statusIsError}
                        >
                            {statusMessage}
                        </div>
                    {/if}
                    <button
                        class="generate-btn"
                        onclick={saveAndGenerate}
                        disabled={chairs.length === 0}
                    >
                        Generate & Install ({chairs.length} presets)
                    </button>
                </div>
            </div>
        </div>
    {/if}
</div>

<style>
    .setup-container {
        height: 100%;
        display: flex;
        flex-direction: column;
        background: #121212;
        color: #e0e0e0;
    }

    .setup-loading {
        display: flex;
        align-items: center;
        justify-content: center;
        height: 100%;
        color: #888;
        font-size: 1.1em;
    }

    .setup-panels {
        display: flex;
        flex: 1;
        gap: 1px;
        background: #333;
        overflow: hidden;
    }

    .browser-panel,
    .selected-panel {
        flex: 1;
        display: flex;
        flex-direction: column;
        background: #1e1e1e;
        overflow: hidden;
    }

    .panel-header {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 12px 16px;
        background: #252525;
        border-bottom: 1px solid #333;
    }

    .panel-header h3 {
        margin: 0;
        font-size: 0.95em;
        font-weight: 600;
        color: #bb86fc;
    }

    .count-badge {
        font-size: 0.8em;
        padding: 2px 8px;
        background: rgba(187, 134, 252, 0.15);
        color: #bb86fc;
        border-radius: 10px;
    }

    .browser-filters {
        display: flex;
        gap: 8px;
        padding: 8px 12px;
        background: #1a1a1a;
        border-bottom: 1px solid #2a2a2a;
    }

    .search-input {
        flex: 1;
        padding: 6px 10px;
        background: #2a2a2a;
        border: 1px solid #444;
        border-radius: 4px;
        color: #e0e0e0;
        font-size: 0.85em;
    }

    .family-select {
        padding: 6px 8px;
        background: #2a2a2a;
        border: 1px solid #444;
        border-radius: 4px;
        color: #e0e0e0;
        font-size: 0.85em;
    }

    .instrument-list,
    .selected-list {
        flex: 1;
        overflow-y: auto;
        padding: 4px 0;
    }

    .instrument-row {
        display: flex;
        align-items: center;
        padding: 6px 12px;
        border-bottom: 1px solid #222;
        gap: 8px;
    }

    .instrument-row:hover {
        background: #262626;
    }

    .instr-info {
        flex: 1;
        display: flex;
        flex-direction: column;
        gap: 1px;
        min-width: 0;
    }

    .instr-name {
        font-size: 0.9em;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
    }

    .instr-family {
        font-size: 0.75em;
        color: #888;
    }

    /* Add buttons */
    .add-buttons {
        display: flex;
        gap: 4px;
        flex-shrink: 0;
    }

    .add-btn {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 30px;
        height: 26px;
        background: #2a2a2a;
        border: 1px solid #444;
        border-radius: 4px;
        cursor: pointer;
        color: #aaa;
        transition: all 0.15s ease;
    }

    .add-btn:hover {
        background: #383838;
        color: #bb86fc;
        border-color: rgba(187, 134, 252, 0.4);
    }

    .add-btn:active {
        background: #444;
    }

    /* Chair rows on the right */
    .chair-row {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 5px 12px;
        border-bottom: 1px solid #222;
    }

    .chair-row:hover {
        background: #262626;
    }

    .chair-icon {
        display: flex;
        align-items: center;
        color: #888;
        flex-shrink: 0;
    }

    .chair-label {
        flex: 1;
        font-size: 0.9em;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
    }

    .remove-btn {
        background: none;
        border: none;
        color: #555;
        cursor: pointer;
        font-size: 0.85em;
        padding: 2px 6px;
        border-radius: 3px;
        opacity: 0;
        transition: opacity 0.15s ease;
    }

    .chair-row:hover .remove-btn {
        opacity: 1;
    }

    .remove-btn:hover {
        color: #cf6679;
        background: rgba(207, 102, 121, 0.1);
    }

    .empty-state {
        padding: 24px 16px;
        color: #666;
        text-align: center;
        font-size: 0.9em;
        line-height: 1.6;
    }

    .action-bar {
        padding: 12px;
        border-top: 1px solid #333;
        background: #1a1a1a;
    }

    .status-message {
        padding: 6px 10px;
        border-radius: 4px;
        font-size: 0.85em;
        margin-bottom: 8px;
    }

    .status-message.error {
        background: rgba(207, 102, 121, 0.15);
        color: #cf6679;
    }

    .status-message.success {
        background: rgba(3, 218, 198, 0.1);
        color: #03dac6;
    }

    .generate-btn {
        width: 100%;
        padding: 10px;
        background: #bb86fc;
        color: #121212;
        border: none;
        border-radius: 6px;
        font-weight: 600;
        font-size: 0.95em;
        cursor: pointer;
    }

    .generate-btn:hover:not(:disabled) {
        background: #d4a5ff;
    }

    .generate-btn:disabled {
        opacity: 0.4;
        cursor: default;
    }
</style>
