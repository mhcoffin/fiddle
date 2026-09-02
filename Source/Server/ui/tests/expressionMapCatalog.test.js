import assert from "node:assert/strict";
import test from "node:test";

import {
    filterExpressionMaps,
    groupExpressionMaps,
    splitExpressionMapName,
} from "../src/lib/expressionMapCatalog.js";

const maps = [
    {
        entityID: "oboe",
        name: "AC Syn Woodwinds — Oboe French 1",
        creator: "Babylonwaves Art Conductor",
    },
    {
        entityID: "clarinet",
        name: "AC Syn Woodwinds — Bass Clarinet",
        creator: "Babylonwaves Art Conductor",
    },
    {
        entityID: "timpani",
        name: "VSL SY Percussion - Timpani",
        creator: "VSL",
    },
    {
        entityID: "universal",
        name: "Fiddle_Universal",
        creator: "Fiddle",
    },
];

test("splits both em-dash and hyphen catalog conventions", () => {
    assert.deepEqual(splitExpressionMapName(maps[0].name), {
        prefix: "AC Syn Woodwinds",
        label: "Oboe French 1",
    });
    assert.deepEqual(splitExpressionMapName(maps[2].name), {
        prefix: "VSL SY Percussion",
        label: "Timpani",
    });
    assert.deepEqual(splitExpressionMapName("Fiddle_Universal"), {
        prefix: "",
        label: "Fiddle_Universal",
    });
});
test("search is case-insensitive and requires every term", () => {
    assert.deepEqual(
        filterExpressionMaps(maps, "syn bass").map((map) => map.entityID),
        ["clarinet"],
    );
    assert.deepEqual(
        filterExpressionMaps(maps, "babylon OBOE").map((map) => map.entityID),
        ["oboe"],
    );
});

test("groups by reusable prefix and falls back to creator", () => {
    const groups = groupExpressionMaps(maps);
    assert.deepEqual(
        groups.map((group) => group.name),
        ["AC Syn Woodwinds", "Fiddle", "VSL SY Percussion"],
    );
    assert.deepEqual(
        groups[0].items.map((map) => map.label),
        ["Bass Clarinet", "Oboe French 1"],
    );
    assert.equal(groups[1].items[0].label, "Fiddle_Universal");
});

test("grouping applies the same query filter and ignores malformed entries", () => {
    const groups = groupExpressionMaps([...maps, {}, null], "timpani");
    assert.equal(groups.length, 1);
    assert.equal(groups[0].name, "VSL SY Percussion");
    assert.equal(groups[0].items[0].entityID, "timpani");
});
