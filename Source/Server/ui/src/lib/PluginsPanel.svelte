<script>
    import { onMount } from "svelte";

    let plugins = $state([]);
    let isScanning = $state(false);
    let searchQuery = $state("");
    let sortColumn = $state("name");
    let sortAsc = $state(true);

    /** @type {any} */
    const w = window;

    /**
     * Returns a callable function that invokes the named native function
     * registered via withNativeFunction() on the C++ side.
     * Uses JUCE's __juce__invoke event system.
     */
    const getNative = (name) => {
        const win = /** @type {any} */ (window);
        if (
            win.__JUCE__ &&
            win.__JUCE__.backend &&
            typeof win.__JUCE__.backend.emitEvent === "function"
        ) {
            let nextId = 0;
            return function () {
                win.__JUCE__.backend.emitEvent("__juce__invoke", {
                    name: name,
                    params: Array.prototype.slice.call(arguments),
                    resultId: nextId++,
                });
            };
        }
        return null;
    };

    w.setPluginList = (jsonStr) => {
        try {
            plugins = JSON.parse(jsonStr);
            isScanning = false;
            console.log(`[Plugins] Received ${plugins.length} plugins`);
        } catch (e) {
            console.error("[Plugins] Failed to parse plugin list:", e);
            isScanning = false;
        }
    };

    onMount(() => {
        const fn = getNative("requestPluginsState");
        console.log(
            "[Plugins] onMount: requestPluginsState",
            fn ? "FOUND" : "NOT FOUND",
        );
        if (fn) fn();
    });

    const startScan = () => {
        isScanning = true;
        const fn = getNative("scanPlugins");
        if (fn) {
            fn();
        } else {
            console.error("[Plugins] scanPlugins native function not found");
            isScanning = false;
        }
    };

    const startRescan = () => {
        isScanning = true;
        const fn = getNative("rescanPlugins");
        if (fn) {
            fn();
        } else {
            console.error("[Plugins] rescanPlugins native function not found");
            isScanning = false;
        }
    };

    const setSort = (col) => {
        if (sortColumn === col) {
            sortAsc = !sortAsc;
        } else {
            sortColumn = col;
            sortAsc = true;
        }
    };

    let filteredPlugins = $derived.by(() => {
        let result = plugins;
        if (searchQuery.trim()) {
            const q = searchQuery.trim().toLowerCase();
            result = result.filter(
                (p) =>
                    p.name.toLowerCase().includes(q) ||
                    p.manufacturer.toLowerCase().includes(q) ||
                    p.category.toLowerCase().includes(q),
            );
        }
        result = [...result].sort((a, b) => {
            const va = (a[sortColumn] || "").toString().toLowerCase();
            const vb = (b[sortColumn] || "").toString().toLowerCase();
            const cmp = va.localeCompare(vb);
            return sortAsc ? cmp : -cmp;
        });
        return result;
    });
</script>

<div class="plugins-container">
    <div class="plugins-header">
        <h2>VST3 Plugins</h2>
        <div class="header-controls">
            <button
                class="scan-button"
                onclick={startScan}
                disabled={isScanning}
            >
                {isScanning ? "Scanning…" : "Scan for Plugins"}
            </button>
            <button
                class="scan-button rescan"
                onclick={startRescan}
                disabled={isScanning}
            >
                Rescan All
            </button>
            <span class="plugin-count">
                {plugins.length} plugin{plugins.length !== 1 ? "s" : ""}
                {#if searchQuery.trim()}
                    · {filteredPlugins.length} shown
                {/if}
            </span>
        </div>
    </div>

    <div class="search-bar">
        <input
            type="text"
            placeholder="Filter by name, manufacturer, or category…"
            bind:value={searchQuery}
        />
    </div>

    {#if isScanning}
        <div class="scanning-indicator">
            <div class="spinner"></div>
            <p>Scanning VST3 directories…</p>
        </div>
    {:else if plugins.length === 0}
        <div class="empty-state">
            <p>
                No plugins found. Click <strong>Scan for Plugins</strong> to search
                for installed VST3 plugins.
            </p>
        </div>
    {:else}
        <div class="plugin-table-wrapper">
            <table class="plugin-table">
                <thead>
                    <tr>
                        <th class="sortable" onclick={() => setSort("name")}>
                            Name {sortColumn === "name"
                                ? sortAsc
                                    ? "▲"
                                    : "▼"
                                : ""}
                        </th>
                        <th
                            class="sortable"
                            onclick={() => setSort("manufacturer")}
                        >
                            Manufacturer {sortColumn === "manufacturer"
                                ? sortAsc
                                    ? "▲"
                                    : "▼"
                                : ""}
                        </th>
                        <th
                            class="sortable"
                            onclick={() => setSort("category")}
                        >
                            Category {sortColumn === "category"
                                ? sortAsc
                                    ? "▲"
                                    : "▼"
                                : ""}
                        </th>
                    </tr>
                </thead>
                <tbody>
                    {#each filteredPlugins as plugin (plugin.uid + plugin.name)}
                        <tr class:invalid={plugin.valid === false}>
                            <td class="plugin-name">
                                {#if plugin.valid === false}
                                    <span
                                        class="invalid-badge"
                                        title="Failed to load">⚠</span
                                    >
                                {/if}
                                {plugin.name}
                            </td>
                            <td>{plugin.manufacturer || ""}</td>
                            <td>{plugin.category || ""}</td>
                        </tr>
                    {/each}
                </tbody>
            </table>
        </div>
    {/if}
</div>

<style>
    .plugins-container {
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

    .plugins-header {
        display: flex;
        align-items: center;
        justify-content: space-between;
        margin-bottom: 12px;
        flex-shrink: 0;
    }

    .plugins-header h2 {
        margin: 0;
        font-size: 1.1rem;
        font-weight: 600;
        color: #f1f5f9;
    }

    .header-controls {
        display: flex;
        align-items: center;
        gap: 12px;
    }

    .scan-button {
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

    .scan-button:hover:not(:disabled) {
        background: #2563eb;
        color: #fff;
    }

    .scan-button:disabled {
        opacity: 0.5;
        cursor: not-allowed;
    }

    .plugin-count {
        color: #64748b;
        font-size: 0.75rem;
    }

    .search-bar {
        margin-bottom: 12px;
        flex-shrink: 0;
    }

    .search-bar input {
        width: 100%;
        padding: 8px 12px;
        border: 1px solid #334155;
        border-radius: 6px;
        background: #1e293b;
        color: #e2e8f0;
        font-size: 0.8rem;
        box-sizing: border-box;
    }

    .search-bar input::placeholder {
        color: #475569;
    }

    .search-bar input:focus {
        outline: none;
        border-color: #3b82f6;
    }

    .scanning-indicator {
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        flex: 1;
        gap: 12px;
        color: #94a3b8;
    }

    .spinner {
        width: 24px;
        height: 24px;
        border: 3px solid #334155;
        border-top-color: #3b82f6;
        border-radius: 50%;
        animation: spin 0.8s linear infinite;
    }

    @keyframes spin {
        to {
            transform: rotate(360deg);
        }
    }

    .empty-state {
        display: flex;
        align-items: center;
        justify-content: center;
        flex: 1;
        color: #64748b;
        font-size: 0.85rem;
    }

    .plugin-table-wrapper {
        flex: 1;
        overflow-y: auto;
        border: 1px solid #1e293b;
        border-radius: 6px;
    }

    .plugin-table {
        width: 100%;
        border-collapse: collapse;
        font-size: 0.78rem;
    }

    .plugin-table thead {
        position: sticky;
        top: 0;
        z-index: 10;
    }

    .plugin-table th {
        background: #1e293b;
        color: #94a3b8;
        font-weight: 600;
        text-align: left;
        padding: 8px 12px;
        border-bottom: 1px solid #334155;
        white-space: nowrap;
    }

    .plugin-table th.sortable {
        cursor: pointer;
        user-select: none;
    }

    .plugin-table th.sortable:hover {
        color: #e2e8f0;
    }

    .plugin-table td {
        padding: 6px 12px;
        border-bottom: 1px solid #0f172a;
        color: #cbd5e1;
    }

    .plugin-table tr:hover td {
        background: #1e293b;
    }

    .plugin-name {
        color: #f1f5f9;
        font-weight: 500;
    }

    .rescan {
        border-color: #64748b;
        background: #1e293b;
        color: #94a3b8;
    }
    .rescan:hover:not(:disabled) {
        background: #334155;
        color: #f1f5f9;
    }

    tr.invalid td {
        opacity: 0.5;
    }

    .invalid-badge {
        color: #ef4444;
        margin-right: 4px;
    }
</style>
