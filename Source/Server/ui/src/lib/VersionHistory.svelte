<script>
    import { onMount } from "svelte";
    import { dispatchCpp, onFromCpp } from "./ipc.js";

    let rawVersions = $state([]);
    let layoutNodes = $state([]);
    let edges = $state([]);
    let branches = $state([]);

    const ROW_HEIGHT = 40;
    const LANE_WIDTH = 20;

    // ── Context menu & prompts ─────────────────────────────────────
    /** @type {{ x: number, y: number, node: any } | null} */
    let contextMenu = $state(null);

    /** Which sub-prompt is active: null | "newBranch" | "merge" */
    let subPrompt = $state(null);
    let promptNode = $state(null);
    let newBranchName = $state("");
    let mergeTargetId = $state("");

    /** Result banner: { ok: bool, message: string } | null */
    let banner = $state(null);
    let bannerTimer = null;

    /** Set of version hashes that are currently branch heads */
    let headHashes = $derived(new Set(branches.map((b) => b.headHash)));

    /**
     * Map from version hash → number of children that reference it as a parent.
     * Used to determine if a version is a branch point (≥2 children).
     */
    let childCountMap = $derived.by(() => {
        const map = new Map();
        for (const v of rawVersions) {
            if (v.parentHash) {
                map.set(v.parentHash, (map.get(v.parentHash) ?? 0) + 1);
            }
            if (v.mergeParentHash) {
                map.set(
                    v.mergeParentHash,
                    (map.get(v.mergeParentHash) ?? 0) + 1,
                );
            }
        }
        return map;
    });

    /** Returns true if the version can be deleted (has a parent and is not a branch point). */
    function canDeleteNode(node) {
        if (!node.parentHash) return false; // root version
        if ((childCountMap.get(node.hash) ?? 0) > 1) return false; // branch point
        return true;
    }

    /** The version ID currently loaded in the mixer */
    let currentVersionId = $state("");

    /** True when the mixer is showing a non-HEAD (historical) version */
    let isDetachedHead = $state(false);

    function showBanner(ok, message) {
        if (bannerTimer) clearTimeout(bannerTimer);
        banner = { ok, message };
        bannerTimer = setTimeout(() => (banner = null), 4000);
    }

    function closeContextMenu() {
        contextMenu = null;
        subPrompt = null;
        promptNode = null;
        newBranchName = "";
        mergeTargetId = "";
    }

    /** @param {MouseEvent} e @param {any} node */
    function handleContextMenu(e, node) {
        e.preventDefault();
        e.stopPropagation();
        closeContextMenu();
        contextMenu = { x: e.clientX, y: e.clientY, node };
    }

    function handleClickOutside(e) {
        if (
            !e.target.closest(".ctx-menu") &&
            !e.target.closest(".sub-prompt")
        ) {
            closeContextMenu();
        }
    }

    // ── Actions ────────────────────────────────────────────────────

    function doCheckoutVersion(node) {
        dispatchCpp("checkoutVersion", node.hash);
        closeContextMenu();
    }

    function openNewBranchPrompt(node) {
        subPrompt = "newBranch";
        promptNode = node;
        newBranchName = "";
    }

    function submitNewBranch() {
        if (!newBranchName.trim()) return;
        dispatchCpp("createBranch", newBranchName.trim(), promptNode.hash);
        closeContextMenu();
    }

    function openMergePrompt(node) {
        subPrompt = "merge";
        promptNode = node;
        // Default target: first branch that isn't the node's branch
        const other = branches.find((b) => b.id !== node.branchId);
        mergeTargetId = other ? other.id : "";
    }

    function submitMerge() {
        if (!mergeTargetId) return;
        dispatchCpp("mergeBranch", promptNode.branchId, mergeTargetId);
        closeContextMenu();
    }

    function doDelete(node) {
        subPrompt = "delete";
        promptNode = node;
    }

    function submitDelete() {
        dispatchCpp("deleteVersion", promptNode.hash);
        closeContextMenu();
    }

    // ── C++ → JS handlers ──────────────────────────────────────────

    onFromCpp("setDagHistory", (data) => {
        try {
            rawVersions = data;
            computeLayout(rawVersions);
        } catch (e) {
            console.error("[Versions] setDagHistory error:", e);
        }
    });

    onFromCpp("setBranches", (data) => {
        try {
            branches = data;
        } catch (e) {
            console.error("[Versions] setBranches error:", e);
        }
    });

    onFromCpp("setCurrentVersion", (data) => {
        currentVersionId = data ?? "";
    });

    onFromCpp("mergeResult", (data) => {
        if (data.ok) {
            const kindLabel =
                data.kind === "fast-forward" ? "fast-forward" : "three-way";
            showBanner(true, `Merged (${kindLabel})`);
        } else {
            showBanner(false, `Merge failed: ${data.error}`);
        }
    });

    onFromCpp("deleteResult", (data) => {
        if (data.ok) {
            showBanner(true, "Version deleted");
        } else {
            showBanner(false, `Delete failed: ${data.error}`);
        }
    });

    onFromCpp("setDetachedHead", (detached) => {
        isDetachedHead = detached;
    });

    onMount(() => {
        dispatchCpp("requestBranches");
    });

    // ── Layout helpers ─────────────────────────────────────────────

    const w = /** @type {any} */ (window);
    function getBranchName(branchId) {
        const b = branches.find((br) => br.id === branchId);
        return b ? b.name : branchId;
    }

    /**
     * Format a SQLite timestamp ("2026-03-07 16:28:12") into a smart string.
     * - Same day:   "4:28 PM"
     * - This week:  "Mon 4:28 PM"
     * - Otherwise:  "Mar 7, 4:28 PM"
     */
    function formatTimestamp(sqliteTs) {
        if (!sqliteTs) return "–";
        const iso = sqliteTs.replace(" ", "T") + "Z";
        const d = new Date(iso);
        if (isNaN(d.getTime())) return sqliteTs;

        const now = new Date();
        const startOfToday = new Date(
            now.getFullYear(),
            now.getMonth(),
            now.getDate(),
        );
        const startOfThisWeek = new Date(startOfToday);
        startOfThisWeek.setDate(startOfToday.getDate() - startOfToday.getDay());

        const timeStr = d.toLocaleTimeString([], {
            hour: "numeric",
            minute: "2-digit",
        });

        if (d >= startOfToday) {
            return timeStr;
        } else if (d >= startOfThisWeek) {
            const dayName = d.toLocaleDateString([], { weekday: "short" });
            return `${dayName} ${timeStr}`;
        } else {
            const dateStr = d.toLocaleDateString([], {
                month: "short",
                day: "numeric",
            });
            return `${dateStr}, ${timeStr}`;
        }
    }

    // Vibrant colors for branches
    const branchColors = [
        "#38bdf8",
        "#34d399",
        "#f472b6",
        "#fbbf24",
        "#a78bfa",
        "#fb7185",
        "#2dd4bf",
        "#818cf8",
        "#f87171",
        "#60a5fa",
    ];
    let branchColorMap = {};
    let nextColorIdx = 0;
    function getColorForBranch(branchId) {
        if (!branchColorMap[branchId]) {
            branchColorMap[branchId] =
                branchColors[nextColorIdx % branchColors.length];
            nextColorIdx++;
        }
        return branchColorMap[branchId];
    }

    function computeLayout(versions) {
        if (!versions || versions.length === 0) {
            layoutNodes = [];
            edges = [];
            return;
        }

        // 1. Topological Sort (Children before Parents)
        const inDegree = new Map();
        const parentsMap = new Map();
        const nodeMap = new Map();

        versions.forEach((v) => {
            nodeMap.set(v.hash, v);
            if (!inDegree.has(v.hash)) inDegree.set(v.hash, 0);

            const parents = [];
            if (v.parentHash) parents.push(v.parentHash);
            if (v.mergeParentHash) parents.push(v.mergeParentHash);
            parentsMap.set(v.hash, parents);

            parents.forEach((p) => {
                inDegree.set(p, (inDegree.get(p) || 0) + 1);
            });
        });

        let queue = [];
        for (const [hash, deg] of inDegree.entries()) {
            if (deg === 0 && nodeMap.has(hash)) {
                queue.push(hash);
            }
        }

        const sorted = [];
        while (queue.length > 0) {
            queue.sort((a, b) => {
                const ta = nodeMap.get(a)?.createdAt ?? "";
                const tb = nodeMap.get(b)?.createdAt ?? "";
                if (ta !== tb) return tb.localeCompare(ta);
                return a.localeCompare(b);
            });

            const current = queue.shift();
            if (!nodeMap.has(current)) continue;
            sorted.push(nodeMap.get(current));

            const parents = parentsMap.get(current) || [];
            parents.forEach((p) => {
                if (inDegree.has(p)) {
                    let deg = inDegree.get(p) - 1;
                    inDegree.set(p, deg);
                    if (deg === 0 && nodeMap.has(p)) {
                        queue.push(p);
                    }
                }
            });
        }

        versions.forEach((v) => {
            if (!sorted.includes(v)) sorted.push(v);
        });

        // 2. Lane Assignment
        let activeLanes = [];
        let nodes = [];
        let edgesList = [];

        sorted.forEach((v, rowIndex) => {
            let x = activeLanes.indexOf(v.hash);

            if (x === -1) {
                x = activeLanes.indexOf(null);
                if (x === -1) {
                    x = activeLanes.length;
                    activeLanes.push(null);
                }
            }

            const nodeInfo = {
                hash: v.hash,
                stateHash: v.stateHash,
                branchId: v.branchId,
                parentHash: v.parentHash,
                mergeParentHash: v.mergeParentHash,
                createdAt: v.createdAt || "",
                x: x,
                y: rowIndex,
                cx: x * LANE_WIDTH + LANE_WIDTH,
                cy: rowIndex * ROW_HEIGHT + ROW_HEIGHT / 2,
                color: getColorForBranch(v.branchId),
            };
            nodes.push(nodeInfo);

            if (v.parentHash) {
                activeLanes[x] = v.parentHash;
            } else {
                activeLanes[x] = null;
            }

            if (v.mergeParentHash) {
                let mx = activeLanes.indexOf(v.mergeParentHash);
                if (mx === -1) {
                    mx = activeLanes.indexOf(null);
                    if (mx === -1) {
                        mx = activeLanes.length;
                        activeLanes.push(null);
                    }
                    activeLanes[mx] = v.mergeParentHash;
                }
            }
        });

        // 3. Build Edges
        const coordMap = new Map();
        nodes.forEach((n) => coordMap.set(n.hash, n));

        nodes.forEach((n) => {
            if (n.parentHash && coordMap.has(n.parentHash)) {
                const p = coordMap.get(n.parentHash);
                edgesList.push({
                    x1: n.cx,
                    y1: n.cy,
                    x2: p.cx,
                    y2: p.cy,
                    color: getColorForBranch(n.branchId),
                    isMerge: false,
                });
            }
            if (n.mergeParentHash && coordMap.has(n.mergeParentHash)) {
                const p = coordMap.get(n.mergeParentHash);
                edgesList.push({
                    x1: n.cx,
                    y1: n.cy,
                    x2: p.cx,
                    y2: p.cy,
                    color: getColorForBranch(p.branchId),
                    isMerge: true,
                });
            }
        });

        layoutNodes = nodes;
        edges = edgesList;
    }
</script>

<svelte:window
    onclick={handleClickOutside}
    onkeydown={(e) => e.key === "Escape" && closeContextMenu()}
/>

<div class="version-history-container">
    <div class="toolbar">
        <h2>Version History</h2>
    </div>

    <!-- Detached HEAD warning banner -->
    {#if isDetachedHead}
        <div class="banner banner-detached">
            ⚡ Viewing historical version — save is disabled.
        </div>
    {/if}

    <!-- Result banner -->
    {#if banner}
        <div
            class="banner"
            class:banner-ok={banner.ok}
            class:banner-err={!banner.ok}
        >
            {banner.ok ? "✓" : "✗"}
            {banner.message}
        </div>
    {/if}

    <div class="dag-canvas">
        {#if layoutNodes.length === 0}
            <div class="empty-state">Loading history...</div>
        {:else}
            <div class="dag-layout">
                <!-- SVG graph -->
                <svg
                    class="dag-svg"
                    width={Math.max(...layoutNodes.map((n) => n.x)) *
                        LANE_WIDTH +
                        LANE_WIDTH * 2}
                    height={layoutNodes.length * ROW_HEIGHT + ROW_HEIGHT}
                >
                    {#each edges as edge}
                        <path
                            d="M {edge.x1} {edge.y1} C {edge.x1} {(edge.y1 +
                                edge.y2) /
                                2}, {edge.x2} {(edge.y1 + edge.y2) /
                                2}, {edge.x2} {edge.y2}"
                            stroke={edge.color}
                            stroke-width="3"
                            fill="none"
                            stroke-dasharray={edge.isMerge ? "5,5" : "none"}
                        />
                    {/each}

                    {#each layoutNodes as node}
                        {#if node.hash === currentVersionId}
                            <!-- Outer glow ring for current version -->
                            <circle
                                cx={node.cx}
                                cy={node.cy}
                                r="11"
                                fill="#4ade8018"
                                stroke="#4ade80"
                                stroke-width="1.5"
                                class="current-ring"
                            />
                        {/if}
                        <circle
                            cx={node.cx}
                            cy={node.cy}
                            r="5"
                            fill={node.hash === currentVersionId
                                ? "#4ade80"
                                : node.color}
                            stroke={node.hash === currentVersionId
                                ? "#166534"
                                : "#0f172a"}
                            stroke-width="2"
                        />
                    {/each}
                </svg>

                <!-- Commit list -->
                <div class="commit-list">
                    {#each layoutNodes as node}
                        <!-- svelte-ignore a11y_no_static_element_interactions -->
                        <div
                            class="commit-row"
                            class:current-row={node.hash === currentVersionId}
                            style="height: {ROW_HEIGHT}px;"
                            oncontextmenu={(e) => handleContextMenu(e, node)}
                        >
                            <div class="commit-info">
                                <span class="timestamp"
                                    >{formatTimestamp(node.createdAt)}</span
                                >
                                <span
                                    class="branch"
                                    style="color: {node.color}; padding: 2px 6px; border: 1px solid {node.color}40; border-radius: 4px; background: {node.color}15"
                                >
                                    {getBranchName(node.branchId)}
                                </span>
                                {#if headHashes.has(node.hash)}
                                    <span class="head-badge">HEAD</span>
                                {/if}
                                {#if node.hash === currentVersionId}
                                    <span class="current-badge">● loaded</span>
                                {/if}
                            </div>
                            <div class="commit-hash">
                                {node.hash.slice(0, 8)}
                            </div>
                        </div>
                    {/each}
                </div>
            </div>
        {/if}
    </div>
</div>

<!-- Context menu -->
{#if contextMenu}
    <div
        class="ctx-menu"
        style="left: {contextMenu.x}px; top: {contextMenu.y}px;"
        role="menu"
        onclick={(e) => e.stopPropagation()}
    >
        {#if subPrompt === null}
            <!-- Main menu -->
            <button
                class="ctx-item"
                onclick={() => doCheckoutVersion(contextMenu.node)}
                role="menuitem"
            >
                Load this version
            </button>
            <button
                class="ctx-item"
                onclick={() => openNewBranchPrompt(contextMenu.node)}
                role="menuitem"
            >
                New branch from here…
            </button>
            <button
                class="ctx-item"
                onclick={() => openMergePrompt(contextMenu.node)}
                role="menuitem"
                disabled={branches.filter(
                    (b) => b.id !== contextMenu.node.branchId,
                ).length === 0}
            >
                Merge into…
            </button>
            <div class="ctx-divider"></div>
            <button
                class="ctx-item ctx-item-danger"
                onclick={() => doDelete(contextMenu.node)}
                role="menuitem"
                disabled={!canDeleteNode(contextMenu.node)}
            >
                Delete…
            </button>
        {:else if subPrompt === "newBranch"}
            <!-- New branch name prompt -->
            <div class="sub-prompt">
                <div class="sub-prompt-label">Branch name</div>
                <!-- svelte-ignore a11y_autofocus -->
                <input
                    class="sub-prompt-input"
                    type="text"
                    placeholder="e.g. Experiment"
                    bind:value={newBranchName}
                    autofocus
                    onkeydown={(e) => {
                        if (e.key === "Enter") submitNewBranch();
                        if (e.key === "Escape") closeContextMenu();
                    }}
                />
                <div class="sub-prompt-actions">
                    <button
                        class="sub-btn sub-btn-cancel"
                        onclick={closeContextMenu}>Cancel</button
                    >
                    <button
                        class="sub-btn sub-btn-ok"
                        disabled={!newBranchName.trim()}
                        onclick={submitNewBranch}>Create</button
                    >
                </div>
            </div>
        {:else if subPrompt === "merge"}
            <!-- Merge target picker -->
            <div class="sub-prompt">
                <div class="sub-prompt-label">
                    Merge <em>{getBranchName(promptNode.branchId)}</em> into…
                </div>
                <select class="sub-prompt-select" bind:value={mergeTargetId}>
                    {#each branches.filter((b) => b.id !== promptNode.branchId) as b}
                        <option value={b.id}>{b.name}</option>
                    {/each}
                </select>
                <div class="sub-prompt-actions">
                    <button
                        class="sub-btn sub-btn-cancel"
                        onclick={closeContextMenu}>Cancel</button
                    >
                    <button
                        class="sub-btn sub-btn-ok"
                        disabled={!mergeTargetId}
                        onclick={submitMerge}>Merge</button
                    >
                </div>
            </div>
        {:else if subPrompt === "delete"}
            <!-- Delete confirmation -->
            <div class="sub-prompt">
                <div class="sub-prompt-label">
                    Delete this version? This cannot be undone.
                </div>
                <div class="sub-prompt-actions">
                    <button
                        class="sub-btn sub-btn-cancel"
                        onclick={closeContextMenu}>Cancel</button
                    >
                    <button
                        class="sub-btn sub-btn-ok sub-btn-danger"
                        onclick={submitDelete}>Delete</button
                    >
                </div>
            </div>
        {/if}
    </div>
{/if}

<style>
    .version-history-container {
        width: 100%;
        height: 100%;
        display: flex;
        flex-direction: column;
        background: #0f172a;
        color: #f8fafc;
    }
    .toolbar {
        display: flex;
        justify-content: space-between;
        align-items: center;
        padding: 12px 20px;
        background: #020617;
        border-bottom: 1px solid #1e293b;
    }
    .toolbar h2 {
        margin: 0;
        font-size: 1.2rem;
        font-weight: 500;
        color: #e2e8f0;
    }

    /* Banner */
    .banner {
        padding: 8px 20px;
        font-size: 0.85rem;
        font-weight: 500;
    }
    .banner-ok {
        background: #14532d;
        color: #86efac;
    }
    .banner-err {
        background: #450a0a;
        color: #fca5a5;
    }
    .banner-detached {
        background: #451a03;
        color: #fdba74;
        border-bottom: 1px solid #78350f40;
    }

    .dag-canvas {
        flex: 1;
        padding: 20px;
        overflow: auto;
        background: #0f172a;
    }
    .dag-layout {
        display: flex;
        position: relative;
    }
    .dag-svg {
        position: absolute;
        top: 0;
        left: 0;
        pointer-events: none;
    }
    .commit-list {
        display: flex;
        flex-direction: column;
        width: 100%;
        position: relative;
        z-index: 10;
        padding-left: 200px;
    }
    .commit-row {
        display: flex;
        align-items: center;
        justify-content: space-between;
        border-bottom: 1px solid transparent;
        transition: background-color 0.15s;
        border-radius: 4px;
        padding-right: 12px;
        cursor: context-menu;
    }
    .commit-row:hover {
        background-color: rgba(255, 255, 255, 0.05);
    }
    .commit-info {
        display: flex;
        align-items: center;
        gap: 12px;
        flex: 1;
    }
    .timestamp {
        font-size: 0.85em;
        color: #cbd5e1;
        min-width: 100px;
    }
    .branch {
        font-size: 0.8rem;
        font-weight: 500;
    }
    .head-badge {
        font-size: 0.7rem;
        font-weight: 700;
        letter-spacing: 0.04em;
        color: #fbbf24;
        background: #78350f40;
        border: 1px solid #fbbf2430;
        border-radius: 3px;
        padding: 1px 5px;
    }
    .current-badge {
        font-size: 0.7rem;
        font-weight: 600;
        letter-spacing: 0.03em;
        color: #4ade80;
        background: rgba(74, 222, 128, 0.1);
        border: 1px solid rgba(74, 222, 128, 0.3);
        border-radius: 3px;
        padding: 1px 6px;
    }
    .current-row {
        background: rgba(74, 222, 128, 0.04);
        border-left: 2px solid #4ade80 !important;
        padding-left: 2px;
    }
    .current-row:hover {
        background: rgba(74, 222, 128, 0.08) !important;
    }
    .commit-hash {
        font-size: 0.75rem;
        font-family: monospace;
        color: #475569;
    }
    @keyframes current-pulse {
        0%,
        100% {
            opacity: 0.7;
            r: 11px;
        }
        50% {
            opacity: 0.35;
            r: 13px;
        }
    }
    .current-ring {
        animation: current-pulse 2.5s ease-in-out infinite;
    }

    /* ── Context menu ── */
    .ctx-menu {
        position: fixed;
        z-index: 1000;
        background: #1e293b;
        border: 1px solid rgba(255, 255, 255, 0.12);
        border-radius: 8px;
        box-shadow: 0 8px 24px rgba(0, 0, 0, 0.6);
        min-width: 200px;
        padding: 4px 0;
        overflow: hidden;
    }
    .ctx-item {
        display: block;
        width: 100%;
        background: none;
        border: none;
        color: #e2e8f0;
        padding: 8px 16px;
        text-align: left;
        font-size: 0.875rem;
        cursor: pointer;
        transition: background 0.12s;
    }
    .ctx-item:hover:not(:disabled) {
        background: rgba(255, 255, 255, 0.07);
    }
    .ctx-item:disabled {
        color: #475569;
        cursor: not-allowed;
    }
    .ctx-item-danger {
        color: #f87171;
    }
    .ctx-item-danger:hover:not(:disabled) {
        background: rgba(248, 113, 113, 0.1);
    }
    .ctx-divider {
        height: 1px;
        background: rgba(255, 255, 255, 0.08);
        margin: 4px 0;
    }

    /* ── Sub-prompt (inline form inside the menu) ── */
    .sub-prompt {
        padding: 12px 16px;
        display: flex;
        flex-direction: column;
        gap: 8px;
    }
    .sub-prompt-label {
        font-size: 0.78rem;
        color: #cbd5e1;
        font-weight: 500;
    }
    .sub-prompt-label em {
        color: #e2e8f0;
        font-style: normal;
        font-weight: 600;
    }
    .sub-prompt-input,
    .sub-prompt-select {
        background: rgba(0, 0, 0, 0.35);
        border: 1px solid rgba(255, 255, 255, 0.15);
        color: #f8fafc;
        border-radius: 5px;
        padding: 6px 8px;
        font-size: 0.85rem;
        width: 100%;
        box-sizing: border-box;
    }
    .sub-prompt-input:focus,
    .sub-prompt-select:focus {
        outline: none;
        border-color: #38bdf8;
    }
    .sub-prompt-actions {
        display: flex;
        gap: 6px;
        justify-content: flex-end;
    }
    .sub-btn {
        padding: 5px 12px;
        border-radius: 4px;
        font-size: 0.8rem;
        font-weight: 500;
        cursor: pointer;
        border: 1px solid transparent;
        transition: all 0.12s;
    }
    .sub-btn-cancel {
        background: rgba(255, 255, 255, 0.06);
        color: #cbd5e1;
        border-color: rgba(255, 255, 255, 0.08);
    }
    .sub-btn-cancel:hover {
        background: rgba(255, 255, 255, 0.1);
        color: #e2e8f0;
    }
    .sub-btn-ok {
        background: #0284c7;
        color: #f0f9ff;
        border-color: #0369a1;
    }
    .sub-btn-ok:hover:not(:disabled) {
        background: #0369a1;
    }
    .sub-btn-ok:disabled {
        opacity: 0.4;
        cursor: not-allowed;
    }
    .sub-btn-danger {
        background: #b91c1c;
        color: #fef2f2;
        border-color: #991b1b;
    }
    .sub-btn-danger:hover {
        background: #991b1b;
    }
</style>
