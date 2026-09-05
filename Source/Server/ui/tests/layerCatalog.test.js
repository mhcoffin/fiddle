import test from "node:test";
import assert from "node:assert/strict";
import { filterLayerCatalog } from "../src/lib/layerCatalog.js";

const patches = [
    { id: "b", name: "Solo Violin", libraryId: "vsl", libraryName: "Vienna", character: "solo", family: "Strings" },
    { id: "a", name: "Violin I", libraryId: "vsl", libraryName: "Vienna", character: "section", family: "Strings" },
    { id: "c", name: "French Horn", libraryId: "ew", libraryName: "Hollywood", character: "section", family: "Brass" },
];

test("layer catalog is grouped by library with patches sorted by name", () => {
    const groups = filterLayerCatalog(patches, "");
    assert.deepEqual(groups.map((group) => group.name), ["Hollywood", "Vienna"]);
    assert.deepEqual(groups[1].patches.map((patch) => patch.id), ["b", "a"]);
});
test("layer search matches all terms across patch metadata", () => {
    const groups = filterLayerCatalog(patches, "vienna section");
    assert.equal(groups.length, 1);
    assert.deepEqual(groups[0].patches.map((patch) => patch.id), ["a"]);
});
