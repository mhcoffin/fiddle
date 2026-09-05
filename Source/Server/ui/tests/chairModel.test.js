import test from "node:test";
import assert from "node:assert/strict";
import {
    groupChairs,
    searchDoricoInstruments,
    suggestedChairName,
} from "../src/lib/chairModel.js";

test("instrument search stays empty until the user types", () => {
    const instruments = [
        { entityID: "instrument.strings.violin", name: "Violin", family: "Strings" },
    ];
    assert.deepEqual(searchDoricoInstruments(instruments, ""), []);
    assert.equal(searchDoricoInstruments(instruments, "viol")[0].name, "Violin");
});

test("chair names distinguish repeated section and solo destinations", () => {
    const instrument = { entityID: "instrument.strings.violin", name: "Violin" };
    const chairs = [
        { entityID: instrument.entityID, role: "section" },
        { entityID: instrument.entityID, role: "solo" },
    ];
    assert.equal(suggestedChairName(instrument, "section", chairs), "Violin 2");
    assert.equal(suggestedChairName(instrument, "solo", chairs), "Solo Violin 2");
});

test("chairs are grouped in orchestral family order without changing MIDI data", () => {
    const chairs = [
        { id: "s", entityID: "strings.violin", family: "Strings", displayOrder: 2, flatIndex: 9 },
        { id: "w", entityID: "winds.flute", family: "Woodwinds", displayOrder: 1, flatIndex: 4 },
    ];
    const groups = groupChairs(chairs);
    assert.deepEqual(groups.map((group) => group.key), ["woodwinds", "strings"]);
    assert.equal(groups[1].chairs[0].flatIndex, 9);
});

test("chairs within a family follow score order before creation order", () => {
    const chairs = [
        { id: "f1", entityID: "instrument.wind.flute", family: "Woodwinds", scoreOrder: 4, ordinal: 1, displayOrder: 0 },
        { id: "p", entityID: "instrument.wind.piccolo", family: "Woodwinds", scoreOrder: 0, ordinal: 1, displayOrder: 1 },
        { id: "f2", entityID: "instrument.wind.flute", family: "Woodwinds", scoreOrder: 4, ordinal: 2, displayOrder: 2 },
    ];
    const [woodwinds] = groupChairs(chairs);
    assert.deepEqual(woodwinds.chairs.map((chair) => chair.id), ["f1", "f2", "p"]);
});
