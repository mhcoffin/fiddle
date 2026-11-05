import assert from "node:assert/strict";
import test from "node:test";
import {
    defaultImportSelection, filterCandidates, initialSearch, mapsFor, sourceFileName,
} from "../src/lib/libraryMaps.js";

const maps = [
    { libraryId: "vsl", kind: "expression", entityId: "x.b", name: "Strings" },
    { libraryId: "vsl", kind: "percussion", entityId: "p.a", name: "Snare" },
    { libraryId: "vsl", kind: "expression", entityId: "x.a", name: "Brass" },
    { libraryId: "ews", kind: "expression", entityId: "x.c", name: "Choir" },
];

test("a library's maps of one kind, by name", () => {
    assert.deepEqual(mapsFor(maps, "vsl", "expression").map((m) => m.entityId), ["x.a", "x.b"]);
    assert.deepEqual(mapsFor(maps, "vsl", "percussion").map((m) => m.entityId), ["p.a"]);
    assert.deepEqual(mapsFor(maps, "spare", "expression"), []);
});

test("imports preselect only new and newer maps", () => {
    const selection = defaultImportSelection([
        { status: "new", entityId: "a" }, { status: "newer", entityId: "b" },
        { status: "same", entityId: "c" }, { status: "changed", entityId: "d" },
    ]);
    assert.deepEqual([...selection].sort(), ["a", "b"]);
});

test("the Dorico search starts from the vendor, else the library name", () => {
    assert.equal(initialSearch({ name: "Synchron Strings", vendor: "VSL" }), "VSL");
    assert.equal(initialSearch({ name: "Duality Strings", vendor: "" }), "Duality Strings");
    assert.equal(initialSearch(null), "");
});

test("search matches every word against name, ID, creator and file", () => {
    const candidates = [
        { name: "SY Strings Pro - Violins", entityId: "xmap.user.vsl_sy_violins", creator: "VSL", file: "/a/VSL SY Strings.doricolib" },
        { name: "Hollywood Brass", entityId: "xmap.ewql.brass", creator: "EastWest", file: "/b/EWQL Brass.doricolib" },
    ];
    assert.equal(filterCandidates(candidates, "").length, 2);
    assert.deepEqual(filterCandidates(candidates, "vsl violins").map((c) => c.name), ["SY Strings Pro - Violins"]);
    assert.deepEqual(filterCandidates(candidates, "ewql").map((c) => c.name), ["Hollywood Brass"]);
    assert.deepEqual(filterCandidates(candidates, "EASTWEST brass").map((c) => c.name), ["Hollywood Brass"]);
    assert.equal(filterCandidates(candidates, "timpani").length, 0);
});

test("source file names come from the stored path", () => {
    assert.equal(sourceFileName({ sourcePath: "/Users/me/Dorico/VSL Snare (PM).doricolib" }), "VSL Snare (PM).doricolib");
    assert.equal(sourceFileName({ sourcePath: "" }), "");
});
