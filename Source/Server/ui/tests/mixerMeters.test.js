import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { parseMixerMeters } from "../src/lib/mixerMeters.js";

test("meter snapshots only contain levels, never control or plugin state", () => {
    const packet = {
        strips: { violin: [-12, -5], flute: [-24, -10] },
        buses: { strings: [-8, -3] }, masterPeakDb: -2,
        gainDb: 99, inserts: [{ name: "should not be copied" }],
    };
    const meters = parseMixerMeters(packet);
    assert.deepEqual(Object.keys(meters), ["strips", "buses", "masterPeakDb"]);
    assert.deepEqual(meters.strips.violin, [-12, -5]);
    assert.deepEqual(meters.buses.strings, [-8, -3]);
    assert.equal(meters.masterPeakDb, -2);
    assert.notEqual(meters.strips.violin, packet.strips.violin);
    assert.deepEqual(parseMixerMeters({ strips: {} }).strips, {}); // removal prunes old IDs
});

test("absent or invalid levels become silence without poisoning CSS values", () => {
    assert.deepEqual(parseMixerMeters(null), { strips: {}, buses: {}, masterPeakDb: -120 });
    const meters = parseMixerMeters({ strips: { a: [NaN, Infinity], b: "bad" }, masterPeakDb: "bad" });
    assert.deepEqual(meters.strips, { a: [-120, -120] });
    assert.equal(meters.masterPeakDb, -120);
});

test("high-rate handler leaves configuration and chair grouping untouched", () => {
    const source = readFileSync(new URL("../src/lib/MixerPanel.svelte", import.meta.url), "utf8");
    const handler = source.match(/onFromCpp\("setMixerMeters", \(data\) => \{([\s\S]*?)\}\)/)?.[1];
    assert.ok(handler);
    assert.match(handler, /meters = parseMixerMeters\(data\)/);
    assert.doesNotMatch(handler, /strips\s*=|groupBuses\s*=|masterAudio\s*=/);
    assert.match(source, /masterPeakDb=\{meters.masterPeakDb\}/);
    assert.match(source, /busMeters=\{meters.buses\}/);
    assert.match(source, /peakDb=\{meters.masterPeakDb\}/);
});

test("server meter tick is coalesced and does not serialize full state", () => {
    const source = readFileSync(new URL("../../MainComponent.cpp", import.meta.url), "utf8");
    const tick = source.match(/if \(\+\+meterCounter % 3 == 0\) \{([\s\S]*?)\n  \}/)?.[1];
    assert.ok(tick);
    assert.match(tick, /pushMixerMeters\(\)/);
    assert.doesNotMatch(tick, /pushMixerState|pushMasterAudioState|pushGroupBusState|safeCallAsync/);
    const publish = source.match(/void MainComponent::pushMixerMeters\(\) \{([\s\S]*?)\n\}/)?.[1];
    assert.match(publish, /meterUpdatePending_/);
    assert.match(publish, /meterLevels\(\)/);
    assert.doesNotMatch(publish, /db_|broadcastMessage|fromString|toJson\(/);
});
