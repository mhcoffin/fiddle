import assert from "node:assert/strict";
import test from "node:test";
import { readFileSync } from "node:fs";
import { audioCpuLabel, appendDiagnosticSample, diagnosticReport, pluginTimingRows } from "../src/lib/audioDiagnostics.js";

test("audio CPU is a budget percentage, with explicit stopped/stale states", () => {
    assert.equal(audioCpuLabel(null), "Audio CPU —");
    const data = { running: true, ageMs: 100, load: 125.4 };
    assert.equal(audioCpuLabel(data), "Audio CPU 125%");
    assert.equal(audioCpuLabel(data, 3500), "Audio CPU —");
    assert.equal(audioCpuLabel({ ...data, ageMs: 2200 }), "Audio CPU stale");
    assert.equal(audioCpuLabel({ ...data, running: false }), "Audio stopped");
});

test("history is capped at 120 observations and throttled independently of meter updates", () => {
    let history = [];
    for (let t = 0; t < 200000; t += 250) history = appendDiagnosticSample(history, { load: 25 }, t);
    assert.equal(history.length, 120);
    assert.equal(history[0].timeMs, 80000);
    assert.equal(history.at(-1).timeMs, 199000);
    const report = JSON.parse(diagnosticReport(history, [{ time: "heard crackle" }]));
    assert.equal(report.samples.length, 120);
    assert.equal(report.marks[0].time, "heard crackle");
});

test("panel uses native modal focus handling and offers marked, copyable diagnostics", () => {
    const source = readFileSync(new URL("../src/lib/AudioDiagnostics.svelte", import.meta.url), "utf8");
    assert.match(source, /dialog\.showModal\(\)/);
    assert.match(source, /Mark crackle/);
    assert.match(source, /copyAudioDiagnosticsReport/);
    assert.match(source, /unsubscribe\(\); clearInterval\(timer\)/);
    assert.match(source, /showAudioSettings/);
    assert.match(source, /buildConfiguration/);
    assert.match(source, /renderBeforeLongGapMs/);
});

test("plugin costs sort without mutating reports, and stale or bypassed costs are explicit", () => {
    const data = { running: true, plugins: [
        { owner: "Quiet", averageMs: 1, ageMs: 100 },
        { owner: "Bypassed", averageMs: 9, ageMs: 100, bypassed: true },
        { owner: "Heavy", averageMs: 4, ageMs: 100 },
        { owner: "Stopped", averageMs: 5, ageMs: 3000 },
    ] };
    const rows = pluginTimingRows(data);
    assert.deepEqual(rows.map(x => x.owner), ["Heavy", "Quiet", "Bypassed", "Stopped"]);
    assert.deepEqual(rows.map(x => x.fresh), [true, true, false, false]);
    assert.equal(data.plugins[0].owner, "Quiet");
    assert.ok(pluginTimingRows({ ...data, running: false }).every(x => !x.fresh));
    const report = JSON.parse(diagnosticReport([{ ...data, buildConfiguration: "Release", device: { bufferSize: 1024 } }], []));
    assert.equal(report.samples[0].plugins.length, 4);
    assert.equal(report.samples[0].device.bufferSize, 1024);
});
