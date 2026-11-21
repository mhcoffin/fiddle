import test from "node:test";
import assert from "node:assert/strict";
import { luaFileNote, useFileTitle, paramSteps, formatParamValue, parseParamText, isParamModified,
    hasModifiedParams, randomParamValue, paramMessages } from "../src/lib/luaScripts.js";

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

const timing = { name: "timing", label: "Timing", type: "float", min: 0, max: 50, step: 0.5, unit: "ms", default: 15, value: 22.5 };
const velocity = { name: "velocity", label: "Velocity", type: "int", min: 0, max: 40, step: 1, default: 8, value: 8 };
const length = { name: "length", type: "float", min: 0, max: 50, step: 0, default: 10, value: 10 };
const seed = { name: "seed", type: "int", min: 0, max: 99999, step: 1, control: "field", zeroLabel: "Auto", randomize: true, default: 0, value: 0 };

test("number parameters step by their declared step, or a round one", () => {
    assert.deepEqual(paramSteps(timing), { step: 0.5, fineStep: 0.5, decimals: 1 });
    assert.deepEqual(paramSteps(velocity), { step: 1, fineStep: 1, decimals: 0 });
    assert.deepEqual(paramSteps(length), { step: 0.5, fineStep: 0.5, decimals: 1 });
    assert.equal(paramSteps({ type: "float", min: 0, max: 1, step: 0 }).step, 0.01);
    assert.equal(paramSteps({ type: "float", min: 0, max: 1, step: 0.25 }).decimals, 2);
});

test("values show as their control does", () => {
    assert.equal(formatParamValue(timing, 22.5), "22.5");
    assert.equal(formatParamValue(velocity, 8), "8");
    assert.equal(formatParamValue(seed, 0), "Auto");
    assert.equal(formatParamValue(seed, 4321), "4321");
    assert.equal(formatParamValue({ type: "bool" }, true), "On");
    assert.equal(formatParamValue({ type: "enum" }, "soft"), "soft");
});

test("typed text accepts a unit and the zero label", () => {
    assert.equal(parseParamText(timing, " 12.5 ms"), 12.5);
    assert.equal(parseParamText(timing, "−3"), -3);
    assert.equal(parseParamText(timing, "fast"), null);
    assert.equal(parseParamText(seed, "auto"), 0);
    assert.equal(parseParamText(seed, ""), null);
});

test("a parameter is modified when it differs from its default", () => {
    assert.equal(isParamModified(timing), true);
    assert.equal(isParamModified(velocity), false);
    assert.equal(isParamModified({ type: "bool", default: false, value: true }), true);
    assert.equal(hasModifiedParams({ params: [velocity, timing] }), true);
    assert.equal(hasModifiedParams({ params: [velocity] }), false);
    assert.equal(hasModifiedParams({}), false);
});

test("a random value is whole, in range, and never Auto", () => {
    const values = [0, 0.5, 0.999999].map((r) => randomParamValue(seed, () => r));
    assert.deepEqual(values, [1, 50000, 99999]);
    assert.equal(randomParamValue({ ...seed, value: 1 }, () => 0), 1); // gives up after 8 tries
});

test("declaration mistakes and dropped values are shown", () => {
    assert.deepEqual(paramMessages({ paramErrors: ["a"], paramNotes: ["b"] }), ["a", "b"]);
    assert.deepEqual(paramMessages(null), []);
});
