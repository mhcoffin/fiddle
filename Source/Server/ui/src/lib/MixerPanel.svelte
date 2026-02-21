<script>
    import { onMount } from "svelte";
    import { FAMILY_ORDER, canonicalFamily } from "./orchestralOrder.js";

    let strips = $state([]);
    let availableInputs = $state([]);
    let scannedPlugins = $state([]);
    let playbackDelay = $state(1000);
    let editingDelay = $state(false);

    /** @type {any} */
    const w = window;

    const getNative = (name) => {
        const win = /** @type {any} */ (window);
        return (
            (win.__JUCE__ &&
                win.__JUCE__.backend &&
                win.__JUCE__.backend[name]) ||
            win[name] ||
            (win.juce && win.juce[name]) ||
            (win.__juce__ && win.__juce__[name])
        );
    };

    /** Family display colors (same as InstrumentList.svelte) */
    const FAMILY_COLORS = {
        woodwinds: {
            accent: "#38bdf8",
            header: "#0e4d6e",
            text: "#bae6fd",
        },
        brass: {
            accent: "#f59e0b",
            header: "#713f12",
            text: "#fde68a",
        },
        percussion: {
            accent: "#a8896b",
            header: "#44342a",
            text: "#d4c4b0",
        },
        keys: {
            accent: "#c084fc",
            header: "#581c87",
            text: "#e9d5ff",
        },
        strings: {
            accent: "#34d399",
            header: "#14532d",
            text: "#a7f3d0",
        },
        choir: {
            accent: "#94a3b8",
            header: "#334155",
            text: "#e2e8f0",
        },
    };

    const defaultColors = FAMILY_COLORS.choir;

    // Callbacks from C++ backend
    w.setMixerState = (jsonStr) => {
        try {
            strips = JSON.parse(jsonStr);
        } catch (e) {
            console.error("[Mixer] Failed to parse mixer state:", e);
        }
    };

    w.setAvailableInputs = (jsonStr) => {
        try {
            availableInputs = JSON.parse(jsonStr);
        } catch (e) {
            console.error("[Mixer] Failed to parse available inputs:", e);
        }
    };

    // Playback delay callback from C++
    w.setPlaybackDelay = (ms) => {
        playbackDelay = ms;
    };

    // Request current delay on mount
    onMount(() => {
        const fn = getNative("getPlaybackDelay");
        if (fn) fn();
    });

    const updateDelay = (ms) => {
        const val = Math.max(0, Math.min(5000, parseInt(ms) || 0));
        playbackDelay = val;
        const fn = getNative("setPlaybackDelay");
        if (fn) fn(val);
    };

    // Also listen for plugin list updates (reuse from PluginsPanel)
    const origSetPluginList = w.setPluginList;
    w.setPluginList = (jsonStr) => {
        if (origSetPluginList) origSetPluginList(jsonStr);
        try {
            scannedPlugins = JSON.parse(jsonStr);
        } catch (e) {
            /* ignore */
        }
    };

    onMount(() => {
        // Request available inputs
        const fn = getNative("getAvailableInputs");
        if (fn) fn();

        const fnMixer = getNative("requestMixerState");
        if (fnMixer) fnMixer();

        const fnPlugins = getNative("requestPluginsState");
        if (fnPlugins) fnPlugins();
    });

    // Actions
    const addStrip = () => {
        const fn = getNative("addMixerStrip");
        if (fn) fn();
    };

    const removeStrip = (id) => {
        const fn = getNative("removeMixerStrip");
        if (fn) fn(id);
    };

    const setInput = (stripId, port, channel) => {
        const fn = getNative("setStripInput");
        if (fn) fn(stripId, port, channel);

        // Auto-name strip based on input instrument
        const input = availableInputs.find(
            (i) => i.port === port && i.channel === channel,
        );
        if (input) {
            const instrumentName = input.label || input.name;
            const defaultName = `${instrumentName} Audio`;
            const strip = strips.find((s) => s.id === stripId);
            // Only auto-rename if name is still a default
            if (
                strip &&
                (strip.name.startsWith("Strip") || strip.name === "")
            ) {
                const renameFn = getNative("setStripName");
                if (renameFn) renameFn(stripId, defaultName);
            }
        }
    };

    const setPlugin = (stripId, pluginUid) => {
        const fn = getNative("setStripPlugin");
        if (fn) fn(stripId, pluginUid);
    };

    const showEditor = (stripId) => {
        const fn = getNative("showStripEditor");
        if (fn) fn(stripId);
    };

    // Editable name logic
    let editingId = $state(null);
    let editValue = $state("");

    const startEditing = (strip) => {
        editingId = strip.id;
        editValue = strip.name;
    };

    const commitEdit = (stripId) => {
        if (editValue.trim()) {
            const fn = getNative("setStripName");
            if (fn) fn(stripId, editValue.trim());
        }
        editingId = null;
    };

    const cancelEdit = () => {
        editingId = null;
    };

    const handleNameKeydown = (e, stripId) => {
        if (e.key === "Enter") {
            e.preventDefault();
            commitEdit(stripId);
        } else if (e.key === "Escape") {
            cancelEdit();
        }
    };

    // Build input label from available inputs
    const getInputLabel = (port, channel) => {
        if (port < 0 || channel < 0) return "No Input";
        const match = availableInputs.find(
            (i) => i.port === port && i.channel === channel,
        );
        if (match) {
            const icon = match.isSolo ? "👤" : "👥";
            return `${icon} ${match.label || match.name}`;
        }
        return `P${port + 1} Ch${channel}`;
    };

    // Build plugin label
    const getPluginLabel = (pluginUid) => {
        if (!pluginUid) return "No Plugin";
        const match = scannedPlugins.find((p) => p.uid === pluginUid);
        return match ? match.name : `UID:${pluginUid}`;
    };

    // Group strips by family, in orchestral order, omitting empty families
    let groupedStrips = $derived.by(() => {
        /** @type {{ family: string, strips: any[], colors: any }[]} */
        const groups = [];
        const familyMap = new Map();

        for (const strip of strips) {
            const fam = canonicalFamily(strip.family || "");
            if (!familyMap.has(fam)) {
                const group = {
                    family: fam,
                    displayName: strip.family || "Other",
                    strips: [],
                    colors: FAMILY_COLORS[fam] || defaultColors,
                };
                familyMap.set(fam, group);
                groups.push(group);
            }
            familyMap.get(fam).strips.push(strip);
        }

        // Sort by FAMILY_ORDER
        groups.sort((a, b) => {
            const ia = FAMILY_ORDER.indexOf(a.family);
            const ib = FAMILY_ORDER.indexOf(b.family);
            return (ia === -1 ? 99 : ia) - (ib === -1 ? 99 : ib);
        });

        return groups;
    });
</script>

<div class="mixer-container">
    <div class="mixer-header">
        <h2>Mixer</h2>
        <div class="header-controls">
            <div class="delay-control">
                <label class="delay-label">Delay</label>
                <input
                    class="delay-slider"
                    type="range"
                    min="0"
                    max="3000"
                    step="50"
                    value={playbackDelay}
                    oninput={(e) => updateDelay(e.target.value)}
                />
                {#if editingDelay}
                    <input
                        class="delay-value-input"
                        type="number"
                        min="0"
                        max="5000"
                        value={playbackDelay}
                        onchange={(e) => {
                            updateDelay(e.target.value);
                            editingDelay = false;
                        }}
                        onblur={() => {
                            editingDelay = false;
                        }}
                        onkeydown={(e) => {
                            if (e.key === "Escape") editingDelay = false;
                        }}
                    />
                {:else}
                    <span
                        class="delay-value"
                        ondblclick={() => {
                            editingDelay = true;
                        }}
                        title="Double-click to type">{playbackDelay}ms</span
                    >
                {/if}
            </div>
            <button class="add-strip-btn" onclick={addStrip}>+ Add Strip</button
            >
        </div>
    </div>

    {#if strips.length === 0}
        <div class="empty-state">
            <p>
                No channel strips. Add instruments in the <strong>Setup</strong>
                tab.
            </p>
        </div>
    {:else}
        <div class="families-container">
            {#each groupedStrips as group (group.family)}
                <div class="family-folder">
                    <!-- Family header -->
                    <div
                        class="family-header"
                        style="background: {group.colors.header}; color: {group
                            .colors.text}; border-left: 3px solid {group.colors
                            .accent};"
                    >
                        <span class="family-name"
                            >{group.displayName.toUpperCase()}</span
                        >
                        <span
                            class="count-pill"
                            style="background: {group.colors
                                .accent}20; color: {group.colors.accent};"
                        >
                            {group.strips.length}
                        </span>
                    </div>

                    <!-- Strips in this family -->
                    <div
                        class="strips-row"
                        style="border-left: 3px solid {group.colors.accent};"
                    >
                        {#each group.strips as strip (strip.id)}
                            <div class="strip">
                                <button
                                    class="strip-delete"
                                    onclick={() => removeStrip(strip.id)}
                                    title="Delete strip">✕</button
                                >

                                <!-- Input Selector -->
                                <div class="strip-section">
                                    <label class="strip-label">Input</label>
                                    <select
                                        class="strip-select"
                                        value={`${strip.inputPort}:${strip.inputChannel}`}
                                        onchange={(e) => {
                                            const [p, c] = e.target.value
                                                .split(":")
                                                .map(Number);
                                            setInput(strip.id, p, c);
                                        }}
                                    >
                                        <option value="-1:-1">-- None --</option
                                        >
                                        {#each availableInputs as input}
                                            {@const icon = input.isSolo
                                                ? "👤"
                                                : "👥"}
                                            <option
                                                value={`${input.port}:${input.channel}`}
                                            >
                                                {icon}
                                                {input.label || input.name}
                                            </option>
                                        {/each}
                                    </select>
                                    {#if strip.inputPort >= 0 && strip.inputChannel >= 0}
                                        <div class="input-port-ch">
                                            P{strip.inputPort + 1} Ch{strip.inputChannel +
                                                1}
                                        </div>
                                    {/if}
                                </div>

                                <!-- Spacer -->
                                <div class="strip-spacer"></div>

                                <!-- Plugin Selector -->
                                <div class="strip-section">
                                    <label class="strip-label">Plugin</label>
                                    <select
                                        class="strip-select"
                                        value={strip.pluginUid || 0}
                                        onchange={(e) => {
                                            const uid = Number(e.target.value);
                                            if (uid) setPlugin(strip.id, uid);
                                        }}
                                    >
                                        <option value="0">— None —</option>
                                        {#each scannedPlugins as plugin}
                                            <option value={plugin.uid}>
                                                {plugin.name}
                                            </option>
                                        {/each}
                                    </select>
                                    {#if strip.hasPlugin}
                                        <button
                                            class="editor-btn"
                                            onclick={() => showEditor(strip.id)}
                                            title="Show plugin editor"
                                            ><span class="btn-icon">⚙️</span
                                            ><span class="btn-text">Open</span
                                            ></button
                                        >
                                    {/if}
                                </div>

                                <!-- Name (bottom, editable on double-click) -->
                                <div class="strip-name-area">
                                    {#if editingId === strip.id}
                                        <input
                                            class="strip-name-input"
                                            type="text"
                                            bind:value={editValue}
                                            onblur={() => commitEdit(strip.id)}
                                            onkeydown={(e) =>
                                                handleNameKeydown(e, strip.id)}
                                        />
                                    {:else}
                                        <div
                                            class="strip-name"
                                            ondblclick={() =>
                                                startEditing(strip)}
                                            title="Double-click to rename"
                                        >
                                            {strip.name}
                                        </div>
                                    {/if}
                                </div>
                            </div>
                        {/each}
                    </div>
                </div>
            {/each}
        </div>
    {/if}
</div>

<style>
    .mixer-container {
        display: flex;
        flex-direction: column;
        height: 100%;
        padding: 16px;
        color: #e2e8f0;
        font-family:
            "Inter",
            -apple-system,
            sans-serif;
        overflow: hidden;
    }

    .mixer-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 16px;
        flex-shrink: 0;
    }

    .mixer-header h2 {
        margin: 0;
        font-size: 1.1rem;
        font-weight: 600;
        color: #f1f5f9;
    }

    .add-strip-btn {
        padding: 6px 16px;
        border: 1px solid #3b82f6;
        border-radius: 6px;
        background: #1e3a5f;
        color: #93c5fd;
        font-size: 0.8rem;
        font-weight: 600;
        cursor: pointer;
        transition: all 0.15s ease;
    }

    .add-strip-btn:hover {
        background: #2563eb;
        color: #fff;
    }

    .header-controls {
        display: flex;
        align-items: center;
        gap: 12px;
    }

    .delay-control {
        display: flex;
        align-items: center;
        gap: 4px;
    }

    .delay-label {
        font-size: 0.7rem;
        color: #94a3b8;
        font-weight: 500;
    }

    .delay-slider {
        width: 80px;
        height: 4px;
        accent-color: #3b82f6;
        cursor: pointer;
    }

    .delay-value {
        font-size: 0.7rem;
        color: #94a3b8;
        min-width: 40px;
        text-align: right;
        cursor: default;
        user-select: none;
    }

    .delay-value-input {
        width: 50px;
        padding: 2px 4px;
        border: 1px solid #3b82f6;
        border-radius: 3px;
        background: #0f172a;
        color: #e2e8f0;
        font-size: 0.7rem;
        text-align: right;
        -moz-appearance: textfield;
    }

    .delay-value-input:focus {
        outline: none;
    }

    .delay-value-input::-webkit-inner-spin-button,
    .delay-value-input::-webkit-outer-spin-button {
        -webkit-appearance: none;
    }

    .empty-state {
        display: flex;
        align-items: center;
        justify-content: center;
        flex: 1;
        color: #64748b;
        font-size: 0.85rem;
    }

    /* Families container: vertical scroll of family folders */
    .families-container {
        display: flex;
        flex-direction: column;
        gap: 12px;
        flex: 1;
        overflow-y: auto;
        overflow-x: hidden;
    }

    .family-folder {
        border-radius: 8px;
        overflow: hidden;
    }

    .family-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        padding: 6px 12px;
        font-size: 0.7rem;
        font-weight: 600;
        letter-spacing: 0.08em;
    }

    .family-name {
        font-weight: 700;
    }

    .count-pill {
        font-size: 0.65rem;
        font-weight: 700;
        padding: 1px 7px;
        border-radius: 99px;
        min-width: 18px;
        text-align: center;
    }

    .strips-row {
        display: flex;
        flex-direction: row;
        gap: 8px;
        padding: 8px;
        overflow-x: auto;
        overflow-y: hidden;
        margin-left: 0;
        background: rgba(0, 0, 0, 0.15);
    }

    .strip {
        display: flex;
        flex-direction: column;
        width: 100px;
        min-width: 100px;
        background: #1e293b;
        border: 1px solid #334155;
        border-radius: 8px;
        padding: 8px;
        position: relative;
        gap: 6px;
    }

    .strip-delete {
        position: absolute;
        top: 4px;
        right: 4px;
        width: 18px;
        height: 18px;
        border: none;
        background: transparent;
        color: #475569;
        font-size: 0.65rem;
        cursor: pointer;
        border-radius: 3px;
        display: flex;
        align-items: center;
        justify-content: center;
        padding: 0;
        line-height: 1;
    }

    .strip-delete:hover {
        background: #dc2626;
        color: #fff;
    }

    .strip-section {
        display: flex;
        flex-direction: column;
        gap: 3px;
    }

    .strip-label {
        font-size: 0.6rem;
        color: #64748b;
        text-transform: uppercase;
        font-weight: 600;
        letter-spacing: 0.05em;
    }

    .strip-select {
        width: 100%;
        padding: 3px 4px;
        border: 1px solid #334155;
        border-radius: 4px;
        background: #0f172a;
        color: #cbd5e1;
        font-size: 0.65rem;
        cursor: pointer;
        appearance: auto;
    }

    .strip-select:focus {
        outline: none;
        border-color: #3b82f6;
    }

    .input-port-ch {
        font-size: 0.55rem;
        color: #64748b;
        letter-spacing: 0.03em;
    }

    .strip-spacer {
        flex: 1;
    }

    .editor-btn {
        margin-top: 3px;
        padding: 4px 6px;
        border: 1px solid #334155;
        border-radius: 4px;
        background: #0f172a;
        color: #94a3b8;
        font-size: 0.7rem;
        cursor: pointer;
        display: flex;
        align-items: center;
        position: relative;
    }

    .btn-icon {
        font-size: 0.55rem;
        position: absolute;
        left: 5px;
    }

    .btn-text {
        flex: 1;
        text-align: center;
    }

    .editor-btn:hover {
        background: #1e3a5f;
        color: #93c5fd;
        border-color: #3b82f6;
    }

    .strip-name-area {
        border-top: 1px solid #334155;
        padding-top: 6px;
    }

    .strip-name {
        font-size: 0.7rem;
        font-weight: 600;
        color: #f1f5f9;
        text-align: center;
        cursor: default;
        user-select: none;
        white-space: nowrap;
        overflow: hidden;
        text-overflow: ellipsis;
    }

    .strip-name-input {
        width: 100%;
        padding: 2px 4px;
        border: 1px solid #3b82f6;
        border-radius: 3px;
        background: #0f172a;
        color: #f1f5f9;
        font-size: 0.7rem;
        font-weight: 600;
        text-align: center;
        box-sizing: border-box;
    }

    .strip-name-input:focus {
        outline: none;
    }
</style>
