<script>
    /**
     * InstrumentList — grouped, sorted, collapsible instrument view.
     *
     * Takes a flat list of instruments and renders them grouped by family
     * in standard orchestral score order (Woodwinds → Brass → Percussion → Keys → Strings).
     *
     * @typedef {{
     *   id: string,
     *   name: string,
     *   family: string,
     *   isSection: boolean,
     *   channel: number
     * }} Instrument
     */

    /** @type {{ instruments: Instrument[], onremove?: (id: string) => void }} */
    let { instruments = [], onremove } = $props();

    // ── Score-order constants ─────────────────────────────────────

    /** Family ordering (top → bottom of score) */
    const FAMILY_ORDER = [
        "woodwinds",
        "brass",
        "percussion",
        "keys",
        "strings",
        "choir",
    ];

    /** Map backend family names to canonical color keys */
    const FAMILY_ALIASES = {
        wind: "woodwinds",
        woodwind: "woodwinds",
        woodwinds: "woodwinds",
        brass: "brass",
        drum: "percussion",
        drums: "percussion",
        percussion: "percussion",
        keys: "keys",
        keyboard: "keys",
        keyboards: "keys",
        strings: "strings",
        string: "strings",
        choir: "choir",
        vocal: "choir",
        vocals: "choir",
        voice: "choir",
    };

    /** Normalize a backend family name to a canonical key */
    const canonicalFamily = (fam) =>
        FAMILY_ALIASES[fam.toLowerCase()] || fam.toLowerCase();

    /** Tessitura ranking within each family — lower = higher pitch = higher on page.
     *  Missing instruments default to 500 (middle). */
    const INSTRUMENT_RANKING = {
        // Woodwinds
        piccolo: 10,
        flute: 20,
        "alto flute": 25,
        "bass flute": 28,
        oboe: 30,
        "english horn": 35,
        "cor anglais": 35,
        clarinet: 40,
        "bass clarinet": 45,
        bassoon: 50,
        contrabassoon: 55,

        // Brass
        trumpet: 10,
        horn: 20,
        trombone: 30,
        "tenor trombone": 30,
        "alto trombone": 28,
        "bass trombone": 35,
        "contrabass trombone": 37,
        tuba: 40,
        "bass tuba": 40,

        // Percussion
        timpani: 10,
        "snare drum": 20,
        "bass drum": 30,
        cymbals: 40,
        xylophone: 50,
        marimba: 55,
        vibraphone: 60,
        glockenspiel: 65,
        "tubular bells": 70,
        triangle: 75,
        "tam-tam": 80,

        // Keys
        piano: 10,
        celesta: 20,
        harpsichord: 30,
        organ: 40,
        harp: 50,

        // Strings
        violin: 10,
        viola: 20,
        violoncello: 30,
        cello: 30,
        "double bass": 40,
        contrabass: 40,
    };

    /** Family display colors */
    const FAMILY_COLORS = {
        woodwinds: {
            accent: "#38bdf8",
            header: "#0e4d6e",
            text: "#bae6fd",
            solo: "#0a3a52",
            section: "#072e42",
        },
        brass: {
            accent: "#f59e0b",
            header: "#713f12",
            text: "#fde68a",
            solo: "#4a2c0a",
            section: "#3b2308",
        },
        percussion: {
            accent: "#a8896b",
            header: "#44342a",
            text: "#d4c4b0",
            solo: "#312520",
            section: "#291f1a",
        },
        keys: {
            accent: "#c084fc",
            header: "#581c87",
            text: "#e9d5ff",
            solo: "#3b1160",
            section: "#2e0d4a",
        },
        strings: {
            accent: "#34d399",
            header: "#14532d",
            text: "#a7f3d0",
            solo: "#0e3a22",
            section: "#0a2e1b",
        },
        choir: {
            accent: "#94a3b8",
            header: "#334155",
            text: "#e2e8f0",
            solo: "#1e293b",
            section: "#182230",
        },
    };

    // ── Sorting ──────────────────────────────────────────────────

    /**
     * Get the tessitura rank for an instrument name.
     * Matches the longest matching key in INSTRUMENT_RANKING.
     */
    const getRank = (name) => {
        const lower = name.toLowerCase();
        let bestRank = 500;
        let bestLen = 0;
        for (const [key, rank] of Object.entries(INSTRUMENT_RANKING)) {
            if (lower.includes(key) && key.length > bestLen) {
                bestRank = rank;
                bestLen = key.length;
            }
        }
        return bestRank;
    };

    /** Compare instruments in score order */
    const sortScoreOrder = (a, b) => {
        // Primary: family order
        const fa = FAMILY_ORDER.indexOf(canonicalFamily(a.family));
        const fb = FAMILY_ORDER.indexOf(canonicalFamily(b.family));
        if (fa !== fb) return fa - fb;

        // Secondary: tessitura
        const ra = getRank(a.name);
        const rb = getRank(b.name);
        if (ra !== rb) return ra - rb;

        // Tertiary: name (alphabetical tiebreaker)
        const nameComp = a.name.localeCompare(b.name);
        if (nameComp !== 0) return nameComp;

        // Quaternary: solo before section
        if (a.isSection !== b.isSection) return a.isSection ? 1 : -1;

        return 0;
    };

    // ── Grouping (reactive) ──────────────────────────────────────

    /** @type {Record<string, boolean>} */
    let collapsedFamilies = $state({});

    let groupedInstruments = $derived.by(() => {
        const sorted = [...instruments].sort(sortScoreOrder);

        /** @type {{ family: string, instruments: Instrument[], colors: any }[]} */
        const groups = [];
        const familyMap = new Map();

        for (const inst of sorted) {
            const fam = canonicalFamily(inst.family);
            if (!familyMap.has(fam)) {
                const group = {
                    family: fam,
                    instruments: [],
                    colors: FAMILY_COLORS[fam] || FAMILY_COLORS.choir,
                };
                familyMap.set(fam, group);
                groups.push(group);
            }
            familyMap.get(fam).instruments.push(inst);
        }

        return groups;
    });

    const toggleFamily = (family) => {
        collapsedFamilies = {
            ...collapsedFamilies,
            [family]: !collapsedFamilies[family],
        };
    };
</script>

<div class="instrument-list-container">
    {#if instruments.length === 0}
        <div class="empty-state">
            <svg
                viewBox="0 0 24 24"
                width="32"
                height="32"
                fill="none"
                stroke="currentColor"
                stroke-width="1.5"
            >
                <path d="M9 19V6l12-3v13" />
                <circle cx="6" cy="18" r="3" />
                <circle cx="18" cy="15" r="3" />
            </svg>
            <p>No instruments selected</p>
            <p class="empty-hint">
                Add instruments from the browser to get started
            </p>
        </div>
    {:else}
        {#each groupedInstruments as group (group.family)}
            <div class="family-group">
                <!-- Family Header -->
                <button
                    class="family-header"
                    style="background: {group.colors.header}; color: {group
                        .colors.text};"
                    onclick={() => toggleFamily(group.family)}
                >
                    <span class="header-left">
                        <svg
                            class="chevron"
                            class:collapsed={collapsedFamilies[group.family]}
                            viewBox="0 0 20 20"
                            width="14"
                            height="14"
                            fill="currentColor"
                        >
                            <path d="M6 4l8 6-8 6z" />
                        </svg>
                        <span class="family-name"
                            >{group.family.toUpperCase()}</span
                        >
                    </span>
                    <span
                        class="count-pill"
                        style="background: {group.colors
                            .accent}20; color: {group.colors.accent};"
                    >
                        {group.instruments.length}
                    </span>
                </button>

                <!-- Instrument Rows -->
                {#if !collapsedFamilies[group.family]}
                    <div
                        class="family-items"
                        style="border-left: 3px solid {group.colors.accent};"
                    >
                        {#each group.instruments as inst (inst.id)}
                            <div
                                class="instrument-row"
                                style="background: {inst.isSection
                                    ? group.colors.section
                                    : group.colors.solo};"
                            >
                                <span
                                    class="inst-icon"
                                    title={inst.isSection ? "Section" : "Solo"}
                                >
                                    {#if inst.isSection}
                                        <!-- Section icon: two people -->
                                        <svg
                                            viewBox="0 0 24 20"
                                            width="18"
                                            height="14"
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
                                        </svg>
                                    {:else}
                                        <!-- Solo icon: single person -->
                                        <svg
                                            viewBox="0 0 20 20"
                                            width="16"
                                            height="14"
                                            fill="currentColor"
                                        >
                                            <circle cx="10" cy="6" r="3.5" />
                                            <path
                                                d="M3.5 17.5c0-3.6 2.9-6.5 6.5-6.5s6.5 2.9 6.5 6.5"
                                            />
                                        </svg>
                                    {/if}
                                </span>
                                <span class="inst-name">{inst.name}</span>
                                <span class="inst-channel" title="MIDI Channel"
                                    >Ch {inst.channel}</span
                                >
                                {#if onremove}
                                    <button
                                        class="remove-btn"
                                        onclick={() => onremove(inst.id)}
                                        title="Remove instrument">✕</button
                                    >
                                {/if}
                            </div>
                        {/each}
                    </div>
                {/if}
            </div>
        {/each}
    {/if}
</div>

<style>
    .instrument-list-container {
        display: flex;
        flex-direction: column;
        gap: 2px;
        font-family:
            Inter,
            system-ui,
            -apple-system,
            sans-serif;
        font-size: 0.85rem;
    }

    /* ── Empty state ────────────────────────── */
    .empty-state {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        padding: 3rem 1rem;
        color: #475569;
        gap: 0.5rem;
    }
    .empty-state p {
        margin: 0;
    }
    .empty-hint {
        font-size: 0.75rem;
        color: #334155;
    }

    /* ── Family group ───────────────────────── */
    .family-group {
        border-radius: 6px;
        overflow: hidden;
    }

    .family-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        width: 100%;
        padding: 8px 12px;
        border: none;
        cursor: pointer;
        font-size: 0.75rem;
        font-weight: 600;
        letter-spacing: 0.08em;
        transition: filter 0.15s;
    }
    .family-header:hover {
        filter: brightness(1.2);
    }

    .header-left {
        display: flex;
        align-items: center;
        gap: 8px;
    }

    .chevron {
        transition: transform 0.2s ease;
        transform: rotate(90deg);
        flex-shrink: 0;
    }
    .chevron.collapsed {
        transform: rotate(0deg);
    }

    .count-pill {
        font-size: 0.7rem;
        font-weight: 700;
        padding: 1px 8px;
        border-radius: 99px;
        min-width: 22px;
        text-align: center;
    }

    /* ── Instrument rows ────────────────────── */
    .family-items {
        display: flex;
        flex-direction: column;
        margin-left: 4px;
    }

    .instrument-row {
        display: flex;
        align-items: center;
        gap: 8px;
        padding: 5px 12px 5px 16px;
        color: #e2e8f0;
        transition: background 0.12s;
    }
    .instrument-row:hover {
        background: rgba(255, 255, 255, 0.04);
    }
    .instrument-row:hover .remove-btn {
        opacity: 1;
    }

    .inst-icon {
        display: flex;
        align-items: center;
        justify-content: center;
        width: 22px;
        flex-shrink: 0;
        color: #64748b;
    }

    .inst-name {
        flex: 1;
        min-width: 0;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
    }

    .inst-channel {
        font-size: 0.7rem;
        color: #475569;
        font-variant-numeric: tabular-nums;
        flex-shrink: 0;
    }

    .remove-btn {
        opacity: 0;
        border: none;
        background: none;
        color: #64748b;
        cursor: pointer;
        padding: 2px 6px;
        border-radius: 4px;
        font-size: 0.75rem;
        transition: all 0.15s;
        flex-shrink: 0;
    }
    .remove-btn:hover {
        color: #ef4444;
        background: rgba(239, 68, 68, 0.1);
    }
</style>
