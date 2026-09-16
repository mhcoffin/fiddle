import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import {
    expressionMapActionsText,
    expressionMapDynamicsText,
    filterExpressionMapCombinations,
    selectExpressionMapCombination,
} from "../src/lib/expressionMapViewer.js";

const viewerSource = readFileSync(
    new URL("../src/lib/ExpressionMapViewer.svelte", import.meta.url),
    "utf8",
);
const pickerSource = readFileSync(
    new URL("../src/lib/ExpressionMapPicker.svelte", import.meta.url),
    "utf8",
);

const combinations = [
    {
        index: 4,
        name: "Legato",
        techniqueIDs: ["pt.legato"],
        switchOnActions: [{ type: "keySwitch", param1: 24, param2: 100 }],
        switchOffActions: [],
        condition: "NoteLength > kShort",
    },
    {
        index: 9,
        name: "Muted",
        techniqueIDs: ["pt.muted"],
        switchOnActions: [{ type: "cc", param1: 32, param2: 2 }],
        switchOffActions: [{ type: "programChange", param1: 7, param2: 0 }],
        condition: "",
    },
];

test("viewer formats MIDI actions and dynamics", () => {
    assert.equal(expressionMapActionsText(combinations[0].switchOnActions), "Key 24 @ 100");
    assert.equal(expressionMapActionsText(combinations[1].switchOnActions), "CC 32 = 2");
    assert.equal(expressionMapActionsText(combinations[1].switchOffActions), "Program 7");
    assert.equal(expressionMapActionsText([]), "—");
    assert.equal(expressionMapDynamicsText("cc", 11), "CC 11");
    assert.equal(expressionMapDynamicsText("noteVelocity", 1), "Note velocity");
});

test("viewer searches names, techniques, actions, and conditions", () => {
    assert.deepEqual(filterExpressionMapCombinations(combinations, "legato"), [combinations[0]]);
    assert.deepEqual(filterExpressionMapCombinations(combinations, "CC 32"), [combinations[1]]);
    assert.deepEqual(filterExpressionMapCombinations(combinations, "kshort"), [combinations[0]]);
    assert.deepEqual(filterExpressionMapCombinations(combinations, "  "), combinations);
});

test("viewer selection follows the filtered rows and has a safe fallback", () => {
    assert.equal(selectExpressionMapCombination(combinations, 9), combinations[1]);
    assert.equal(selectExpressionMapCombination([combinations[0]], 9), combinations[0]);
    assert.equal(selectExpressionMapCombination([], 9), null);
});

test("picker opens the read-only viewer without assigning a map", () => {
    assert.match(pickerSource, /<ExpressionMapViewer entityId=\{viewerId\}/);
    assert.match(pickerSource, /onclick=\{\(\) => view\(map\.entityID\)\}/);
    assert.match(viewerSource, /onFromCpp\("setExpressionMapDetails"/);
    assert.match(viewerSource, /dispatchCpp\("requestExpressionMapDetails", entityId\)/);
    assert.doesNotMatch(viewerSource, /loadExpressionMap|setGroupExpressionMap/);
});
