import assert from "node:assert/strict";
import test from "node:test";

import {
    addStripRange,
    flattenVisualStripOrder,
    selectedChairBusIds,
    chairHasSettings,
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

test("each chair strip follows its layers; empty chairs have none", () => {
    const groups = [{
        instrGroups: [
            { chairBusId: "chair:fl", strips: [{ id: "flute-a" }, { id: "flute-b" }] },
            { chairBusId: "chair:ob", strips: [] },
            { chairBusId: "chair:cl", strips: [{ id: "clarinet" }] },
        ],
    }];
    assert.deepEqual(flattenVisualStripOrder(groups),
        ["flute-a", "flute-b", "chair:fl", "clarinet", "chair:cl"]);
});

test("a hidden chair strip drops out of the visual order", () => {
    const groups = [{
        instrGroups: [
            { chairBusId: "chair:fl", strips: [{ id: "flute-a" }, { id: "flute-b" }] },
            { chairBusId: "chair:cl", strips: [{ id: "clarinet" }] },
        ],
    }];
    assert.deepEqual(flattenVisualStripOrder(groups, (group) => group.strips.length > 1),
        ["flute-a", "flute-b", "chair:fl", "clarinet"]);
});

test("a chair has settings when anything audible differs from a plain 0 dB bus", () => {
    const plain = { gainDb: 0, muted: false, soloed: false, audio: { balance: 0, preFaderInserts: [], postFaderInserts: [] }, sends: [{ busId: "rev" }] };
    assert.equal(chairHasSettings(plain), false, "sends alone don't count");
    assert.equal(chairHasSettings({ ...plain, gainDb: 0.04 }), false);
    assert.equal(chairHasSettings({ ...plain, gainDb: -4 }), true);
    assert.equal(chairHasSettings({ ...plain, muted: true }), true);
    assert.equal(chairHasSettings({ ...plain, soloed: true }), true);
    assert.equal(chairHasSettings({ ...plain, audio: { ...plain.audio, balance: -0.3 } }), true);
    assert.equal(chairHasSettings({ ...plain, audio: { ...plain.audio, postFaderInserts: [{ id: "eq" }] } }), true);
    assert.equal(chairHasSettings(undefined), false);
});

test("a selected layer stands for its chair, once", () => {
    const strips = [
        { id: "flute-a", chairBusId: "chair:fl" },
        { id: "flute-b", chairBusId: "chair:fl" },
        { id: "clarinet", chairBusId: "chair:cl" },
    ];
    const chairs = new Set(["chair:fl", "chair:cl", "chair:ob"]);
    assert.deepEqual(selectedChairBusIds(new Set(["flute-a", "flute-b", "chair:fl"]), strips, chairs), ["chair:fl"]);
    assert.deepEqual(selectedChairBusIds(new Set(["chair:ob", "clarinet", "missing"]), strips, chairs),
        ["chair:ob", "chair:cl"]);
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
