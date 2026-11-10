import test from "node:test";
import assert from "node:assert/strict";
import {
    chairUnits,
    isKitInstrument,
    kitChairNames,
    kitColumns,
    kitPayload,
    orderChairsWithKits,
    suggestedKitName,
} from "../src/lib/kitModel.js";
import { groupChairs } from "../src/lib/chairModel.js";

const block = (tail) => `instrument.percussion.other.wood.block.${tail}`;
const blocks = ["high", "medium.high", "medium", "medium.low", "low"].map(block);
const catalogue = [
    ...blocks.map((entityID) => ({ entityID, name: "Wood Block", family: "wood" })),
    { entityID: "instrument.band.unpitched.tomtom.medium-high", name: "Tom-tom", family: "drum" },
    { entityID: "instrument.band.unpitched.tomtom.medium-low", name: "Tom-tom", family: "drum" },
    { entityID: "instrument.orchestral.unpitched.snare.drum", name: "Snare Drum", family: "drum" },
    { entityID: "instrument.kit.wood.blocks", name: "Wood Block", family: "", kitInstruments: blocks },
];

test("Dorico's kit instruments are the ones with kit contents", () => {
    assert.equal(isKitInstrument(catalogue.at(-1)), true);
    assert.equal(isKitInstrument(catalogue[0]), false);
    assert.equal(isKitInstrument({ kitInstruments: [] }), false);
});

test("kit chairs with the same Dorico name are told apart by their IDs", () => {
    assert.deepEqual(kitChairNames(blocks, catalogue), [
        "Wood Block (High)", "Wood Block (Medium High)", "Wood Block (Medium)",
        "Wood Block (Medium Low)", "Wood Block (Low)",
    ]);
    assert.deepEqual(kitChairNames([
        "instrument.orchestral.unpitched.snare.drum",
        "instrument.band.unpitched.tomtom.medium-high",
        "instrument.band.unpitched.tomtom.medium-low",
    ], catalogue), ["Snare Drum", "Tom-tom (Medium High)", "Tom-tom (Medium Low)"]);
    // The same instrument twice (Dorico's marching snares list one twice).
    assert.deepEqual(kitChairNames([blocks[0], blocks[0]], catalogue), ["Wood Block 1", "Wood Block 2"]);
    // An instrument Fiddle doesn't know is named from its ID.
    assert.deepEqual(kitChairNames(["instrument.custom.thing.big"], []), ["Thing Big"]);
});

test("kit names don't repeat an existing kit's", () => {
    const chairs = [{ kitId: "a", kitName: "Wood Block" }, { kitId: "b", kitName: "Wood Block 2" }, { name: "Wood Block 3" }];
    assert.equal(suggestedKitName("Wood Block", chairs), "Wood Block 3");
    assert.equal(suggestedKitName("Tom-tom", chairs), "Tom-tom");
    assert.equal(suggestedKitName("  ", []), "Kit");
});

test("a kit payload is one named solo chair per instrument in kit order, filed under the first's family", () => {
    assert.deepEqual(kitPayload(" Blocks ", [blocks[4], blocks[0]], catalogue), {
        name: "Blocks",
        family: "wood",
        instruments: [
            { entityID: blocks[4], name: "Wood Block (Low)" },
            { entityID: blocks[0], name: "Wood Block (High)" },
        ],
    });
});

const byScore = (a, b) => a.scoreOrder - b.scoreOrder;

test("a kit's chairs stay together where its first chair sorts, in kit order", () => {
    const chairs = [
        { id: "low", kitId: "k", kitName: "Blocks", scoreOrder: 9, displayOrder: 2 },
        { id: "snare", scoreOrder: 5, displayOrder: 0 },
        { id: "high", kitId: "k", kitName: "Blocks", scoreOrder: 3, displayOrder: 1 },
        { id: "triangle", scoreOrder: 4, displayOrder: 3 },
    ];
    const units = chairUnits(chairs, byScore);
    assert.deepEqual(units.map((unit) => unit.kitId || unit.chair.id), ["k", "triangle", "snare"]);
    assert.deepEqual(units[0].chairs.map((chair) => chair.id), ["high", "low"]);
    assert.equal(units[0].kitName, "Blocks");
    assert.deepEqual(orderChairsWithKits(chairs, byScore).map((chair) => chair.id), ["high", "low", "triangle", "snare"]);
});

test("Chair Manager families carry kit units", () => {
    const groups = groupChairs([
        { id: "a", entityID: blocks[0], family: "wood", kitId: "k", kitName: "Blocks", displayOrder: 0 },
        { id: "b", entityID: blocks[1], family: "wood", kitId: "k", kitName: "Blocks", displayOrder: 1 },
    ]);
    assert.equal(groups.length, 1);
    assert.equal(groups[0].units.length, 1);
    assert.deepEqual(groups[0].units[0].chairs.map((chair) => chair.id), ["a", "b"]);
});

test("mixer columns gather a kit's consecutive chairs", () => {
    const column = (id, kitId = "") => ({ key: id, chair: { id, kitId, kitName: kitId ? "Kit A" : "" } });
    const columns = kitColumns([column("x"), column("a", "k"), column("b", "k"), column("y")]);
    assert.equal(columns.length, 3);
    assert.equal(columns[0].instrGroup.key, "x");
    assert.equal(columns[1].kitName, "Kit A");
    assert.deepEqual(columns[1].instrGroups.map((group) => group.key), ["a", "b"]);
    assert.equal(columns[2].instrGroup.key, "y");
});
