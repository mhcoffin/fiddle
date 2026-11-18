import assert from "node:assert/strict";
import test from "node:test";
import {
    applyMapUsage, coverageNote, defaultImportSelection, filterCandidates, mapMatches, mapSuggestion, mapsOfKind,
    newRequestId, pickerSections, sourceFileName, summarizeSourceCheck,
} from "../src/lib/libraryMaps.js";

const violin = "instrument.strings.violin";
const use = (type, patchId, libraryId, instrument) => ({ type, patchId, libraryId, instrument, label: patchId });
// Fiddle's maps (docs/maps.md): any library's patches and layers use them.
const maps = [
    { kind: "expression", entityId: "x.ac.violins", name: "AC Duality — 1st Violins", creator: "Babylonwaves",
      sourcePath: "/Art Conductor/VSL Duality.doricolib",
      uses: [use("patch", "duality-violin", "duality", violin), use("layer", "duality-violin", "duality", violin)] },
    { kind: "expression", entityId: "x.vsl.violins", name: "VSL SY Violins", creator: "VSL",
      uses: [use("patch", "elite-violin", "elite", violin)] },
    { kind: "expression", entityId: "x.ac.basses", name: "AC Duality — Double Basses",
      uses: [use("patch", "duality-bass", "duality", "instrument.strings.contrabass")] },
    { kind: "expression", entityId: "x.custom", name: "My custom strings", favorite: true, uses: [] },
    { kind: "expression", entityId: "x.brass", name: "Brass", uses: [] },
    { kind: "percussion", entityId: "p.snare", name: "Snare", instruments: ["instrument.unpitched.snare"], uses: [] },
];
const names = (sections) => sections.map((section) => [section.title, section.items.map((item) => item.map.entityId)]);

test("maps of one kind, by name", () => {
    assert.deepEqual(mapsOfKind(maps, "expression").map((m) => m.entityId),
        ["x.ac.violins", "x.ac.basses", "x.brass", "x.custom", "x.vsl.violins"]); // "1st" < "Double"
    assert.deepEqual(mapsOfKind(maps, "percussion").map((m) => m.entityId), ["p.snare"]);
    assert.deepEqual(mapsOfKind([], "expression"), []);
});

test("search matches every word against name, ID, creator, players and file", () => {
    assert.ok(mapMatches(maps[0], "duality violins"));
    assert.ok(mapMatches(maps[0], "BABYLONWAVES"));
    assert.ok(mapMatches(maps[0], "duality.doricolib")); // the source file's name
    assert.ok(!mapMatches(maps[0], "conductor")); // not its folder
    assert.ok(mapMatches(maps[0], ""));
    assert.ok(!mapMatches(maps[0], "duality basses"));
});

test("suggestions: this patch, then the same instrument, then percussion coverage", () => {
    const context = { patchId: "duality-violin", instrument: violin };
    assert.deepEqual(mapSuggestion(maps[0], context), { score: 3, reason: "This patch" });
    assert.equal(mapSuggestion(maps[1], context).reason, "Used for this instrument (1)");
    assert.equal(mapSuggestion(maps[2], context).score, 0);
    // Aliases count as the same instrument.
    assert.equal(mapSuggestion(maps[1], { instrument: `${violin}.alias.violin-1` }).score, 2);
    assert.equal(mapSuggestion(maps[5], { instrument: "instrument.unpitched.snare.alias.snare-2" }).score, 1);
    assert.equal(mapSuggestion(maps[5], { instrument: "instrument.unpitched.tom" }).score, 0);
});

test("the picker suggests maps from other libraries for a violin layer", () => {
    // Violin 1's first layer, from Duality Strings: VSL's violin map, from
    // another library, is suggested; the basses map is not.
    const context = { kind: "expression", patchId: "duality-violin", libraryId: "duality", instrument: violin };
    assert.deepEqual(names(pickerSections(maps, context)), [
        ["Suggested", ["x.ac.violins", "x.vsl.violins"]],
        ["Favorites", ["x.custom"]],
        ["All maps", ["x.ac.basses", "x.brass"]],
    ]);
    assert.deepEqual(pickerSections(maps, context)[0].items.map((item) => item.reason),
        ["This patch", "Used for this instrument (1)"]);
});

test("a search is one ranked list; filters narrow it", () => {
    const context = { kind: "expression", patchId: "duality-violin", libraryId: "duality", instrument: violin };
    assert.deepEqual(names(pickerSections(maps, context, { query: "duality" })),
        [["Matches", ["x.ac.violins", "x.ac.basses"]]]);
    assert.deepEqual(names(pickerSections(maps, context, { query: "strings" })), [["Matches", ["x.custom"]]]);
    assert.deepEqual(names(pickerSections(maps, context, { filter: "favorites" })), [["Favorites", ["x.custom"]]]);
    // This library: maps any of the library's patches or layers use.
    assert.deepEqual(names(pickerSections(maps, context, { filter: "library" })),
        [["This library", ["x.ac.violins", "x.ac.basses"]]]);
    assert.deepEqual(pickerSections(maps, { ...context, libraryId: "" }, { filter: "library" }), []);
    assert.deepEqual(pickerSections(maps, context, { query: "timpani" }), []);
    // Favorites rank above other matches with the same suggestion score.
    assert.deepEqual(names(pickerSections(maps, { kind: "expression" }, { query: "s" }))[0][1].slice(0, 1), ["x.custom"]);
});

test("percussion maps note a chair they don't list", () => {
    assert.equal(coverageNote(maps[5], "instrument.unpitched.snare"), "");
    assert.equal(coverageNote(maps[5], "instrument.unpitched.tom"), "other instrument");
    assert.equal(coverageNote({ kind: "percussion", instruments: ["a", "b"] }, "c"), "does not cover this instrument");
    assert.equal(coverageNote(maps[0], violin), "");
    assert.equal(coverageNote(maps[5], ""), "");
});

test("usage messages refresh each map's uses and labels", () => {
    const before = [
        { kind: "expression", entityId: "x.a", name: "A", uses: [use("patch", "old", "lib", "")], references: ["old"] },
        { kind: "percussion", entityId: "x.a", name: "Same ID, other kind" },
    ];
    const after = applyMapUsage(before, [
        { kind: "expression", entityId: "x.a", type: "layer", patchId: "p", libraryId: "lib", instrument: violin, label: "Layer Violin 1 / P" },
        { kind: "expression", entityId: "x.a", type: "patch", patchId: "p", libraryId: "lib", instrument: violin, label: "Lib / P" },
        { kind: "expression", entityId: "x.gone", type: "patch", patchId: "q", libraryId: "lib", instrument: "", label: "Lib / Q" },
    ]);
    assert.deepEqual(after[0].references, ["Lib / P", "Layer Violin 1 / P"]);
    assert.deepEqual(after[0].uses.map((u) => u.type), ["layer", "patch"]);
    assert.equal(after[0].name, "A");
    assert.deepEqual(after[1].uses, []);
    assert.deepEqual(after[1].references, []);
    assert.equal(after.length, 2);
});

test("request IDs are distinct", () => {
    assert.notEqual(newRequestId(), newRequestId());
    assert.match(newRequestId("import-expression"), /^import-expression-/);
});

test("imports preselect only new and newer maps", () => {
    const selection = defaultImportSelection([
        { status: "new", entityId: "a" }, { status: "newer", entityId: "b" },
        { status: "same", entityId: "c" }, { status: "changed", entityId: "d" },
    ]);
    assert.deepEqual([...selection].sort(), ["a", "b"]);
});

test("import search matches every word against name, ID, creator and file", () => {
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

test("a source check sums up as one line", () => {
    const of = (...statuses) => statuses.map((status) => ({ status }));
    assert.deepEqual(summarizeSourceCheck(of("current", "current", "current")), { text: "All 3 up to date", tone: "ok" });
    assert.deepEqual(summarizeSourceCheck(of("current")), { text: "Up to date", tone: "ok" });
    assert.deepEqual(summarizeSourceCheck(of("newer", "differs", "missing", "current")),
        { text: "2 updates available · 1 source missing · 1 up to date", tone: "warn" });
    assert.deepEqual(summarizeSourceCheck(of("newer")), { text: "1 update available", tone: "warn" });
    assert.deepEqual(summarizeSourceCheck([]), { text: "No imported maps to check", tone: "ok" });
});
