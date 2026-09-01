import assert from "node:assert/strict";
import test from "node:test";

import {
    addStripRange,
    flattenVisualStripOrder,
} from "../src/lib/mixerSelection.js";

test("visual strip order follows rendered family and instrument groups", () => {
    const groups = [
        {
            instrGroups: [
                { strips: [{ id: "flute" }, { id: "oboe" }] },
                { strips: [{ id: "clarinet" }] },
            ],
        },
        {
            instrGroups: [{ strips: [{ id: "horn" }, { id: "trumpet" }] }],
        },
    ];

    assert.deepEqual(flattenVisualStripOrder(groups), [
        "flute",
        "oboe",
        "clarinet",
        "horn",
        "trumpet",
    ]);
});

test("shift range includes every visually intervening strip", () => {
    const visualOrder = ["flute", "oboe", "clarinet", "horn", "trumpet"];
    const selected = addStripRange(
        new Set(["flute"]),
        visualOrder,
        "flute",
        "horn",
    );

    assert.deepEqual([...selected], ["flute", "oboe", "clarinet", "horn"]);
});

test("reverse ranges and prior command selections are retained", () => {
    const visualOrder = ["flute", "oboe", "clarinet", "horn", "trumpet"];
    const selected = addStripRange(
        new Set(["trumpet"]),
        visualOrder,
        "horn",
        "oboe",
    );

    assert.deepEqual([...selected], ["trumpet", "oboe", "clarinet", "horn"]);
});

test("unknown anchors leave the selection unchanged", () => {
    const selected = addStripRange(
        new Set(["flute"]),
        ["flute", "oboe"],
        "missing",
        "oboe",
    );

    assert.deepEqual([...selected], ["flute"]);
});
