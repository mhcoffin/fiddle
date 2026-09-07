import test from "node:test";
import assert from "node:assert/strict";
import {
    groupChairs,
    likelySectionRoleMistakes,
    searchDoricoInstruments,
    suggestedChairName,
    suggestedChairRole,
} from "../src/lib/chairModel.js";

test("instrument search stays empty until the user types", () => {
    const instruments = [
        { entityID: "instrument.strings.violin", name: "Violin", family: "Strings" },
    ];
    assert.deepEqual(searchDoricoInstruments(instruments, ""), []);
    assert.equal(searchDoricoInstruments(instruments, "viol")[0].name, "Violin");
});

test("chair names distinguish repeated section and solo destinations", () => {
    const instrument = { entityID: "instrument.strings.violin", name: "Violin", family: "strings" };
    const chairs = [
        { entityID: instrument.entityID, role: "section" },
        { entityID: instrument.entityID, role: "solo" },
    ];
    assert.equal(suggestedChairName(instrument, "section", chairs), "Violin 2");
    assert.equal(suggestedChairName(instrument, "solo", chairs), "Solo Violin 2");
});

test("chair roles default to Dorico's conventional orchestral player types", () => {
    assert.equal(suggestedChairRole({ family: "strings" }), "section");
    assert.equal(suggestedChairRole({ family: "voice" }), "section");
    assert.equal(suggestedChairRole({ family: "wind" }), "solo");
    assert.equal(suggestedChairRole({ family: "brass" }), "solo");
    assert.equal(suggestedChairRole({ family: "pitchedpercussion" }), "solo");
    assert.equal(suggestedChairRole({ family: "keyboard" }), "solo");
});

test("only likely accidental section roles are offered for bulk correction", () => {
    const chairs = [
        { id: "flute", family: "wind", role: "section" },
        { id: "horns", family: "brass", role: "section" },
        { id: "violin-section", family: "strings", role: "section" },
        { id: "violin-solo", family: "strings", role: "solo" },
        { id: "piano", family: "keyboard", role: "solo" },
    ];

    assert.deepEqual(
        likelySectionRoleMistakes(chairs).map((chair) => chair.id),
        ["flute", "horns"],
    );
});

test("conventional solo instruments do not receive a redundant Solo prefix", () => {
    const flute = { entityID: "instrument.wind.flute", name: "Flute", family: "wind" };
    assert.equal(suggestedChairName(flute, "solo", []), "Flute 1");
    assert.equal(suggestedChairName(flute, "section", []), "Section Flute 1");
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

test("solo chairs precede section chairs while each role keeps score order", () => {
    const chairs = [
        { id: "section-violin", entityID: "instrument.strings.violin", family: "Strings", role: "section", scoreOrder: 1, displayOrder: 0 },
        { id: "solo-cello", entityID: "instrument.strings.cello", family: "Strings", role: "solo", scoreOrder: 4, displayOrder: 1 },
        { id: "section-viola", entityID: "instrument.strings.viola", family: "Strings", role: "section", scoreOrder: 3, displayOrder: 2 },
        { id: "solo-violin", entityID: "instrument.strings.violin", family: "Strings", role: "solo", scoreOrder: 1, displayOrder: 3 },
    ];

    const [strings] = groupChairs(chairs);
    assert.deepEqual(
        strings.chairs.map((chair) => chair.id),
        ["solo-violin", "solo-cello", "section-violin", "section-viola"],
    );
    assert.deepEqual(
        strings.chairs.map((chair) => chair.displayOrder),
        [3, 1, 0, 2],
        "visual sorting must not rewrite persistent chair assignments",
    );
});
