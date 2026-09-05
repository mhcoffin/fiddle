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
    };
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

export function togglePatchSelection(selectedIds, patchId, additive = false) {
    const next = additive ? new Set(selectedIds) : new Set();
    if (additive && next.has(patchId)) next.delete(patchId);
    else next.add(patchId);
    return next;
}

export function shouldMarkLibraryEditorDirtyForPreviewChange(modalView) {
    return modalView === "editor";
}
