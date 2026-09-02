import assert from "node:assert/strict";
import test from "node:test";

import {
    dimensionEntries,
    formatDimensionValue,
    resolutionState,
    techniqueEntries,
} from "../src/lib/noteInspection.js";

test("incoming techniques and non-length dimensions remain visible", () => {
    const record = {
        receivedTechniques: {
            articulation: "Staccato",
            mute: "Con Sordino",
        },
        receivedDimensions: {
            dorico_length_category: 1,
            custom_amount: 0.75,
        },
    };

    assert.deepEqual(techniqueEntries(record), [
        ["articulation", "Staccato"],
        ["mute", "Con Sordino"],
    ]);
    assert.deepEqual(dimensionEntries(record), [["custom_amount", 0.75]]);
    assert.equal(formatDimensionValue(0.75), "0.75");
    assert.equal(formatDimensionValue(2), "2");
});

test("an absent map is distinct from an unsuccessful map resolution", () => {
    assert.deepEqual(resolutionState({ expressionMapAssigned: false }), {
        kind: "unmapped",
        label: "No expression map assigned",
    });
    assert.deepEqual(resolutionState({ expressionMapAssigned: true }), {
        kind: "unmatched",
        label: "No match",
    });
    assert.deepEqual(
        resolutionState({
            expressionMapAssigned: true,
            baseSwitchName: "Staccato Short",
        }),
        { kind: "matched", label: "Staccato Short" },
    );
});
