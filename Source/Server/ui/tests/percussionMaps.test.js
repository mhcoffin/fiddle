import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import {
    coversInstrument, defaultImportSelection, duplicatePercussionMap, formatTechniques, isUnpitchedInstrument,
    newPercussionMap, newPercussionMapId, parseKeyswitches, parseTechniques, pitchName, sortForInstrument,
    validatePercussionMap,
} from "../src/lib/percussionMaps.js";
import { percussionState } from "../src/lib/noteInspection.js";

const snare = "instrument.orchestral.unpitched.snare.drum";
const fixedRandom = () => "12345678-9abc-def0-1234-56789abcdef0";

test("techniques use Dorico's canonical + order", () => {
    assert.deepEqual(parseTechniques("pt.rim+pt.right"), ["pt.right", "pt.rim"]);
    assert.deepEqual(parseTechniques(" pt.rim, pt.rim "), ["pt.rim"]);
    assert.equal(formatTechniques(["pt.rim", "pt.bell"]), "pt.bell+pt.rim");
});

test("pitch names follow Dorico (middle C = C3)", () => {
    assert.equal(pitchName(60), "C3");
    assert.equal(pitchName(24), "C0");
    assert.equal(pitchName(128), "");
});

test("keyswitch fields accept lists and reject out-of-range values", () => {
    assert.deepEqual(parseKeyswitches("24, 26"), [24, 26]);
    assert.deepEqual(parseKeyswitches(""), []);
    assert.equal(parseKeyswitches("128"), null);
    assert.equal(parseKeyswitches("C1"), null);
});

test("new maps get a Fiddle entity ID and a Natural entry", () => {
    assert.equal(newPercussionMapId("VSL Snare!", fixedRandom), "drumkitmap.user.fiddle.vsl_snare_12345678");
    const map = newPercussionMap("Snare", snare, fixedRandom);
    assert.deepEqual(validatePercussionMap(map), []);
    assert.deepEqual(map.entries[0].techniques, ["pt.natural"]);
    const copy = duplicatePercussionMap(map, () => "ffffffff-0000");
    assert.notEqual(copy.entityId, map.entityId);
    assert.equal(copy.name, "Snare copy");
});

test("validation mirrors the C++ rules", () => {
    const map = newPercussionMap("Snare", snare, fixedRandom);
    map.entries.push({ ...map.entries[0] });
    assert.match(validatePercussionMap(map).join("\n"), /duplicate/);
    map.entries[1] = { ...map.entries[0], variant: "electric" };
    assert.deepEqual(validatePercussionMap(map), []);
    map.entries[1].pitch = 300;
    assert.match(validatePercussionMap(map).join("\n"), /pitch/);
    assert.match(validatePercussionMap({ entityId: "x", name: "X", entries: [] }).join("\n"), /at least one entry/);
});

test("unpitched instruments and instrument coverage", () => {
    assert.ok(isUnpitchedInstrument(snare));
    assert.ok(isUnpitchedInstrument("instrument.percussion.other.tambourine"));
    assert.ok(!isUnpitchedInstrument("instrument.pitchedpercussion.marimba"));
    const toms = { name: "Toms", instruments: ["instrument.band.unpitched.tomtom.high.alias.tom-high", "instrument.band.unpitched.tomtom.low"] };
    const snareMap = { name: "A snare", instruments: [snare] };
    assert.ok(coversInstrument(toms, "instrument.band.unpitched.tomtom.high"));
    assert.ok(!coversInstrument(toms, snare));
    assert.ok(coversInstrument(snareMap, "instrument.orchestral.unpitched.side.drum")); // single-instrument
    assert.deepEqual(sortForInstrument([snareMap, toms], "instrument.band.unpitched.tomtom.low").map((m) => m.name), ["Toms", "A snare"]);
});

test("imports preselect only new and newer maps", () => {
    const selection = defaultImportSelection([
        { status: "new", map: { entityId: "a" } }, { status: "newer", map: { entityId: "b" } },
        { status: "same", map: { entityId: "c" } }, { status: "changed", map: { entityId: "d" } },
    ]);
    assert.deepEqual([...selection].sort(), ["a", "b"]);
});

test("note inspector summarizes the percussion decision", () => {
    assert.equal(percussionState({}), null);
    const played = percussionState({ percussion: { mapName: "VSL", entryName: "Rim shot", pitch: 66, keyswitches: [24] } });
    assert.equal(played.label, "Rim shot → 66 + KS 24");
    const withheld = percussionState({ percussion: { mapName: "VSL", withheld: "no percussion-map entry" } });
    assert.equal(withheld.kind, "withheld");
});

test("layers on unpitched chairs offer a percussion map picker", () => {
    const details = readFileSync(new URL("../src/lib/LayerDetailsPanel.svelte", import.meta.url), "utf8");
    const mixer = readFileSync(new URL("../src/lib/MixerPanel.svelte", import.meta.url), "utf8");
    assert.match(details, /strip\.unpitchedPercussion \|\| strip\.percussionMapEntityID/);
    assert.match(mixer, /dispatchCpp\("loadPercussionMap", detailsStrip\.id, entityId\)/);
    assert.match(mixer, /dispatchCpp\("requestPercussionMaps"\)/);
    const manager = readFileSync(new URL("../src/lib/LibraryManager.svelte", import.meta.url), "utf8");
    assert.match(manager, /percMap:\s+patch\.percMap \|\| ""/);
});
