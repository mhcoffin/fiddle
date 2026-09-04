import test from "node:test";
import assert from "node:assert/strict";

import {
    createBlankLibraryPatch,
    createLibraryPatch,
    duplicateLibraryPatch,
    togglePatchSelection,
    updateSelectedPatches,
} from "../src/lib/libraryPatchModel.js";

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
        stripId: "preview-strip",
        hasPluginState: true,
    };

    const copy = duplicateLibraryPatch(original, "patch-2");
    assert.equal(copy.id, "patch-2");
    assert.equal(copy.name, "Violin I Copy");
    assert.equal(copy.entityID, original.entityID);
    assert.equal(copy.vstPlugin, 42);
    assert.equal(copy.exprMap, "xmap.violin");
    assert.equal(copy.stripId, "");
    assert.equal(copy.sourcePatchId, "patch-1");
});
