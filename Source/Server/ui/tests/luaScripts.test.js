import test from "node:test";
import assert from "node:assert/strict";
import { luaFileNote, useFileTitle } from "../src/lib/luaScripts.js";

test("an unchanged file says nothing", () => {
    assert.equal(luaFileNote({ loaded: true, fileStatus: "unchanged", fileName: "humanize.lua" }), null);
});

test("an unloaded script is reported by the row's error style, not a note", () => {
    assert.equal(luaFileNote({ loaded: false, fileStatus: "missing" }), null);
    assert.equal(luaFileNote(null), null);
});

test("a changed file offers to use it", () => {
    const note = luaFileNote({ loaded: true, fileStatus: "changed", fileName: "humanize.lua" });
    assert.equal(note.canUseFile, true);
    assert.match(note.text, /humanize\.lua has changed/);
    assert.match(note.text, /keeps playing the saved copy/);
});

test("a changed file that fails to load says why and offers nothing", () => {
    const note = luaFileNote({ loaded: true, fileStatus: "changed", fileName: "humanize.lua", fileError: "syntax error near 'end'" });
    assert.equal(note.canUseFile, false);
    assert.match(note.text, /syntax error near 'end'/);
});

test("a missing file plays the saved copy", () => {
    const note = luaFileNote({ loaded: true, fileStatus: "missing", fileName: "humanize.lua" });
    assert.equal(note.canUseFile, false);
    assert.match(note.text, /isn't on this Mac/);
});

test("the button says it changes every layer and can be undone", () => {
    assert.match(useFileTitle({ fileName: "humanize.lua" }), /Every layer that uses humanize\.lua.*undo/);
});
