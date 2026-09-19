import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { parseMixerMeters, formatLufs } from "../src/lib/mixerMeters.js";

test("compact LUFS readings have one decimal and never show invalid measurements", () => {
    assert.equal(formatLufs({ valid: true, integrated: -18.44 }, "integrated"), "-18.4");
    assert.equal(formatLufs({ valid: true, shortTerm: -16.76 }, "shortTerm"), "-16.8");
    for (const invalid of [undefined, {}, { valid: false, integrated: -18 },
        { valid: true, incomplete: true, integrated: -18 }, { valid: true, integrated: null },
        { valid: true, integrated: NaN }, { valid: true, integrated: Infinity }]) {
        assert.equal(formatLufs(invalid, "integrated"), "—");
    }
});

test("meter snapshots only contain levels, never control or plugin state", () => {
    const packet = {
        strips: { violin: [-12, -5], flute: [-24, -10] },
        buses: { strings: [-8, -3] }, masterPeakDb: -2,
        gainDb: 99, inserts: [{ name: "should not be copied" }],
    };
    const meters = parseMixerMeters(packet);
    assert.deepEqual(Object.keys(meters), ["strips", "buses", "masterPeakDb", "stereoStrips", "stereoBuses", "master", "loudness"]);
    assert.deepEqual(meters.strips.violin, [-12, -5]);
    assert.deepEqual(meters.buses.strings, [-8, -3]);
    assert.equal(meters.masterPeakDb, -2);
    assert.notEqual(meters.strips.violin, packet.strips.violin);
    assert.deepEqual(parseMixerMeters({ strips: {} }).strips, {}); // removal prunes old IDs
});

test("absent or invalid levels become silence without poisoning CSS values", () => {
    assert.deepEqual(parseMixerMeters(null), { strips: {}, buses: {}, masterPeakDb: -120,
        stereoStrips: {}, stereoBuses: {}, master: [-120, -120, -120, -120, false, false],
        loudness: { valid: false, incomplete: false, seconds: 0, integrated: null, shortTerm: null, truePeakLeft: null, truePeakRight: null } });
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
    assert.match(source, /masterMeters=\{meters.master\}/);
    assert.match(source, /busMeters=\{meters.stereoBuses\}/);
    assert.match(source, /peakDb=\{meters.masterPeakDb\}/);
});

test("stereo and loudness values are sanitized independently of configuration", () => {
    const packet = { stereoStrips: { a: [-12, -10, -6, -1, true, false], b: "bad" },
        stereoBuses: "bad", master: [NaN, Infinity, -6, -3, 1, true],
        loudness: { valid: true, seconds: 12, integrated: -18, shortTerm: Infinity, truePeakLeft: -.5, truePeakRight: null } };
    const result = parseMixerMeters(packet);
    assert.deepEqual(result.stereoStrips, { a: [-12, -10, -6, -1, true, false] });
    assert.deepEqual(result.stereoBuses, {});
    assert.deepEqual(result.master, [-120, -120, -6, -3, false, true]);
    assert.equal(result.loudness.integrated, -18);
    assert.equal(result.loudness.shortTerm, null);
    assert.equal(result.loudness.truePeakRight, null);
    packet.loudness.valid = false;
    packet.loudness.incomplete = true;
    assert.equal(parseMixerMeters(packet).loudness.integrated, null);
    assert.equal(parseMixerMeters(packet).loudness.incomplete, true);
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
