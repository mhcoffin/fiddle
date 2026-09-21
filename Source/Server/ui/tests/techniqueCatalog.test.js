import assert from "node:assert/strict";
import test from "node:test";
import { catalogRows, defaultCatalogScope, definitionLabel, mapsInScope } from "../src/lib/techniqueCatalog.js";

const report = {
    sources: [{ kind: "installed" }, { kind: "reference" }, { kind: "bridge" }],
    maps: [
        { name: "VSL Violin", source: 0, usage: [{ scope: "project" }] },
        { name: "AC Violin", source: 0, usage: [{ scope: "library" }] },
        { name: "AC Organ", source: 1, usage: [] },
    ],
    techniques: [
        { id: "pt.lyrical", names: ["Lyrical"], maps: [0, 1], appearances: [{ text: "lyrical line" }], declared: false, variants: true, definitionState: "local" },
        { id: "pt.natural", names: ["Natural"], maps: [0, 1, 2], appearances: [], declared: true, variants: false, definitionState: "local" },
        { id: "pt.stop", names: ["Organ stop"], maps: [2], appearances: [], declared: false, variants: false, definitionState: "reference" },
        { id: "pt.missing", names: [], maps: [1], appearances: [], declared: false, variants: false, definitionState: "missing" },
    ],
};
test("reference maps never count as installed or assigned instruments", () => {
    assert.deepEqual([...mapsInScope(report, "installed")], [0, 1]);
    assert.deepEqual([...mapsInScope(report, "reference")], [2]);
    assert.equal(defaultCatalogScope(report), "project");
    assert.equal(catalogRows(report).length, 2);
    assert.equal(catalogRows(report, { scope: "all" }).length, 4);
});
test("coverage flags are independent of missing or conflicting definitions", () => {
    assert.deepEqual(catalogRows(report, { status: "gap" }).map(t => t.id), ["pt.lyrical"]);
    assert.deepEqual(catalogRows(report, { scope: "library", status: "missing" }).map(t => t.id), ["pt.missing"]);
    assert.equal(definitionLabel(report.techniques[2]), "Reference definition only");
    assert.equal(definitionLabel(report.techniques[0]), "Reference variants differ");
});
test("search joins notation names, technique identities and map names, without merging alternatives", () => {
    assert.equal(catalogRows(report, { query: "LYRICAL line" }).length, 1);
    assert.equal(catalogRows(report, { scope: "all", mapIndex: "1" }).length, 3);
    assert.equal(catalogRows(report, { scope: "project", mapIndex: "1" }).length, 0);
    assert.equal(catalogRows(report, { scope: "all", query: "organ" }).length, 2);
    assert.equal(catalogRows(null).length, 0);
});
