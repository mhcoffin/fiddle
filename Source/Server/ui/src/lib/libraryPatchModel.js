export const PATCH_CHARACTERS = ["solo", "section", "ensemble", "overlay"];

export function createLibraryPatch(instrument, character, id, defaults = {}) {
    return {
        id,
        entityID: instrument.entityID,
        instrumentName: instrument.name,
        name: defaults.name || instrument.name,
        family: instrument.family || "",
        character: PATCH_CHARACTERS.includes(character) ? character : "",
        vstPlugin: Number(defaults.vstPlugin) || 0,
        exprMap: defaults.exprMap || "",
        pluginUid: Number(defaults.vstPlugin) || 0,
        hasPluginState: false,
        pluginStatePending: false,
        expectedPresetName: defaults.expectedPresetName || "",
        setupComplete: defaults.setupComplete !== false,
        usageCount: 0,
        outOfDateLayerCount: 0,
    };
}

export function pluginSetupStatus(patch) {
    const hasPlayer = Boolean(Number(patch?.vstPlugin) || Number(patch?.pluginUid));
    if (!hasPlayer) return { kind: "none", symbol: "", label: "" };
    if (patch?.pluginStatePending) {
        return {
            kind: "pending",
            symbol: "•",
            label: "Player setup changed — save the library",
        };
    }
    if (patch?.hasPluginState && patch?.setupComplete !== false) {
        return {
            kind: "saved",
            symbol: "✓",
            label: "Player setup saved",
        };
    }
    return {
        kind: "missing",
        symbol: "!",
        label: "Player assigned — no setup saved",
    };
}

export function guidedSetupPatches(patches) {
    return patches.filter((patch) => Boolean(patch.expectedPresetName));
}

export function guidedSetupProgress(patches) {
    const guided = guidedSetupPatches(patches);
    const configured = guided.filter((patch) => patch.setupComplete).length;
    return { total: guided.length, configured, remaining: guided.length - configured };
}

export function nextGuidedPatch(patches, afterId = "") {
    const guided = guidedSetupPatches(patches);
    if (!guided.length) return null;
    const afterIndex = guided.findIndex((patch) => patch.id === afterId);
    for (let offset = 1; offset <= guided.length; ++offset) {
        const patch = guided[(Math.max(afterIndex, -1) + offset) % guided.length];
        if (!patch.setupComplete) return patch;
    }
    return null;
}

export function canCaptureGuidedPatch(patch) {
    return Boolean(
        patch?.expectedPresetName
        && !patch.setupComplete
        && patch.pluginStatePending,
    );
}

export function createBlankLibraryPatch(id, defaults = {}) {
    return createLibraryPatch(
        { entityID: "", name: "", family: "" },
        "",
        id,
        { ...defaults, name: defaults.name || "New Patch" },
    );
}

export function updateSelectedPatches(patches, selectedIds, changes) {
    if (!selectedIds || selectedIds.size === 0) return patches;
    return patches.map((patch) =>
        selectedIds.has(patch.id) ? { ...patch, ...changes } : patch,
    );
}

export function duplicateLibraryPatch(patch, id) {
    return {
        ...patch,
        id,
        name: `${patch.name} Copy`,
        sourcePatchId: patch.id,
    };
}

export function moveLibraryPatch(patches, patchId, targetId, insertAfter = false) {
    if (patchId === targetId) return patches;

    const sourceIndex = patches.findIndex((patch) => patch.id === patchId);
    if (sourceIndex < 0 || !patches.some((patch) => patch.id === targetId)) return patches;

    const next = [...patches];
    const [moved] = next.splice(sourceIndex, 1);
    let targetIndex = next.findIndex((patch) => patch.id === targetId);
    if (insertAfter) targetIndex += 1;
    next.splice(targetIndex, 0, moved);

    return next.every((patch, index) => patch.id === patches[index].id)
        ? patches
        : next;
}

export function moveLibraryPatchByOffset(patches, patchId, offset) {
    const sourceIndex = patches.findIndex((patch) => patch.id === patchId);
    if (sourceIndex < 0) return patches;

    const targetIndex = Math.max(0, Math.min(patches.length - 1, sourceIndex + offset));
    if (sourceIndex === targetIndex) return patches;

    const next = [...patches];
    const [moved] = next.splice(sourceIndex, 1);
    next.splice(targetIndex, 0, moved);
    return next;
}

export function sortLibraryPatchesOrchestrally(patches, scoreOrder) {
    const next = [...patches].sort((a, b) => {
        const orderDifference = scoreOrder(a.entityID) - scoreOrder(b.entityID);
        return orderDifference || a.name.localeCompare(b.name);
    });
    return next.every((patch, index) => patch.id === patches[index].id)
        ? patches
        : next;
}

export function togglePatchSelection(selectedIds, patchId, additive = false) {
    const next = additive ? new Set(selectedIds) : new Set();
    if (additive && next.has(patchId)) next.delete(patchId);
    else next.add(patchId);
    return next;
}

export function shouldMarkLibraryEditorDirtyForPreviewChange(modalView) {
    return modalView === "editor";
}
