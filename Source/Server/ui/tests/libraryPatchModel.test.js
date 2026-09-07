import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";

import {
    createBlankLibraryPatch,
    createLibraryPatch,
    duplicateLibraryPatch,
    moveLibraryPatch,
    moveLibraryPatchByOffset,
    pluginSetupStatus,
    shouldMarkLibraryEditorDirtyForPreviewChange,
    sortLibraryPatchesOrchestrally,
    togglePatchSelection,
    updateSelectedPatches,
} from "../src/lib/libraryPatchModel.js";

const libraryManagerSource = readFileSync(
    new URL("../src/lib/LibraryManager.svelte", import.meta.url),
    "utf8",
);
const mixerPanelSource = readFileSync(
    new URL("../src/lib/MixerPanel.svelte", import.meta.url),
    "utf8",
);
const mainComponentSource = readFileSync(
    new URL("../../MainComponent.cpp", import.meta.url),
    "utf8",
);

test("library deletion is a visible, plainly labeled two-step action", () => {
    assert.match(libraryManagerSource, /'Confirm Delete'\s*:\s*'Delete Library…'/);
    const deleteRule = libraryManagerSource.match(/\.lm-card-delete\s*\{[\s\S]*?\n\s*\}/)?.[0];
    assert.ok(deleteRule, "library delete button CSS rule should exist");
    assert.doesNotMatch(deleteRule, /opacity:\s*0/);
    assert.doesNotMatch(libraryManagerSource, /\.lm-card:hover\s+\.lm-card-delete/);
});

test("plug-in preview changes dirty only the open library editor", () => {
    assert.equal(shouldMarkLibraryEditorDirtyForPreviewChange("editor"), true);
    assert.equal(shouldMarkLibraryEditorDirtyForPreviewChange("none"), false);
    assert.equal(shouldMarkLibraryEditorDirtyForPreviewChange("create"), false);
});

test("player setup status distinguishes saved, pending, and unconfigured rows", () => {
    assert.equal(pluginSetupStatus({ vstPlugin: 0 }).kind, "none");
    assert.deepEqual(
        pluginSetupStatus({ vstPlugin: 42, hasPluginState: false }),
        {
            kind: "missing",
            symbol: "!",
            label: "Player assigned — no setup saved",
        },
    );
    assert.equal(
        pluginSetupStatus({ vstPlugin: 42, hasPluginState: true }).kind,
        "saved",
    );
    assert.equal(
        pluginSetupStatus({ pluginUid: 42, pluginStatePending: true }).kind,
        "pending",
    );
    assert.match(libraryManagerSource, /class="plugin-setup-status \{setupStatus\.kind\}"/);
    assert.match(libraryManagerSource, /aria-label=\{setupStatus\.label\}/);
});

test("the Library Manager exposes guarded propagation to linked layers", () => {
    assert.match(libraryManagerSource, /"Update Layers"/);
    assert.match(
        libraryManagerSource,
        /dispatchCpp\("updateLayersFromLibraryPatch", row\.id\)/,
    );
    assert.match(
        libraryManagerSource,
        /disabled=\{editorDirty \|\| !row\.outOfDateLayerCount\}/,
    );
    assert.match(
        libraryManagerSource,
        /Save and reopen the library before updating layers/,
    );
});

test("chair layers expose a one-click, undoable refresh from their source patch", () => {
    assert.match(mixerPanelSource, /"refreshChairLayerFromLibrary", strip\.id/);
    assert.match(mixerPanelSource, /!strip\.sourcePatchOutOfDate/);
    assert.match(mixerPanelSource, /strip\.missingPatchReference/);
    assert.match(mixerPanelSource, />Refresh Library<\/button>/);
    assert.doesNotMatch(mixerPanelSource, /pendingLayerRefreshId/);
    assert.match(
        mainComponentSource,
        /undoManager_\.perform\(std::make_unique<RefreshLayerFromLibraryAction>/,
    );
});

test("a catalog patch describes a sound without a Dorico destination", () => {
    const patch = createLibraryPatch(
        { entityID: "instrument.strings.violin", name: "Violin", family: "strings" },
        "solo",
        "patch-1",
        { vstPlugin: 42 },
    );

    assert.equal(patch.id, "patch-1");
    assert.equal(patch.character, "solo");
    assert.equal(patch.vstPlugin, 42);
    assert.equal("instanceNums" in patch, false);
    assert.equal("isSolo" in patch, false);
    assert.equal("stripId" in patch, false, "catalog patches are not mixer strips");
});

test("a blank patch can be named before optional instrument classification", () => {
    const patch = createBlankLibraryPatch("patch-blank", { vstPlugin: 42 });
    assert.equal(patch.name, "New Patch");
    assert.equal(patch.entityID, "");
    assert.equal(patch.instrumentName, "");
    assert.equal(patch.family, "");
    assert.equal(patch.character, "");
    assert.equal(patch.vstPlugin, 42);
});

test("batch changes affect every selected patch and no others", () => {
    const patches = [
        { id: "a", vstPlugin: 0, exprMap: "" },
        { id: "b", vstPlugin: 0, exprMap: "" },
        { id: "c", vstPlugin: 7, exprMap: "old" },
    ];
    const selected = new Set(["a", "b"]);
    const assigned = updateSelectedPatches(patches, selected, {
        vstPlugin: 99,
    });

    assert.deepEqual(assigned.map((patch) => patch.vstPlugin), [99, 99, 7]);
    assert.equal(assigned[2], patches[2]);
});

test("selection supports replacement and command-style toggling", () => {
    assert.deepEqual([...togglePatchSelection(new Set(["a"]), "b")], ["b"]);
    assert.deepEqual(
        [...togglePatchSelection(new Set(["a"]), "b", true)].sort(),
        ["a", "b"],
    );
    assert.deepEqual(
        [...togglePatchSelection(new Set(["a", "b"]), "a", true)],
        ["b"],
    );
});

test("duplicate creates an independent row and retains its catalog defaults", () => {
    const original = {
        id: "patch-1",
        name: "Violin I",
        entityID: "instrument.strings.violin",
        character: "section",
        vstPlugin: 42,
        exprMap: "xmap.violin",
        hasPluginState: true,
    };

    const copy = duplicateLibraryPatch(original, "patch-2");
    assert.equal(copy.id, "patch-2");
    assert.equal(copy.name, "Violin I Copy");
    assert.equal(copy.entityID, original.entityID);
    assert.equal(copy.vstPlugin, 42);
    assert.equal(copy.exprMap, "xmap.violin");
    assert.equal("stripId" in copy, false);
    assert.equal(copy.sourcePatchId, "patch-1");
});

test("patches can be dragged before or after another patch", () => {
    const patches = [{ id: "a" }, { id: "b" }, { id: "c" }, { id: "d" }];

    assert.deepEqual(
        moveLibraryPatch(patches, "d", "b").map((patch) => patch.id),
        ["a", "d", "b", "c"],
    );
    assert.deepEqual(
        moveLibraryPatch(patches, "a", "c", true).map((patch) => patch.id),
        ["b", "c", "a", "d"],
    );
    assert.equal(moveLibraryPatch(patches, "a", "a"), patches);
});

test("keyboard patch reordering stops at the list boundaries", () => {
    const patches = [{ id: "a" }, { id: "b" }, { id: "c" }];

    assert.deepEqual(
        moveLibraryPatchByOffset(patches, "b", -1).map((patch) => patch.id),
        ["b", "a", "c"],
    );
    assert.deepEqual(
        moveLibraryPatchByOffset(patches, "b", 1).map((patch) => patch.id),
        ["a", "c", "b"],
    );
    assert.equal(moveLibraryPatchByOffset(patches, "a", -1), patches);
});

test("orchestral sorting is available without overriding manual order", () => {
    const patches = [
        { id: "c", entityID: "cello", name: "Cello" },
        { id: "v2", entityID: "violin", name: "Violin II" },
        { id: "v1", entityID: "violin", name: "Violin I" },
    ];
    const order = (entityID) => ({ violin: 1, cello: 2 })[entityID] ?? 99;

    assert.deepEqual(
        sortLibraryPatchesOrchestrally(patches, order).map((patch) => patch.id),
        ["v1", "v2", "c"],
    );
    assert.deepEqual(patches.map((patch) => patch.id), ["c", "v2", "v1"]);
});
