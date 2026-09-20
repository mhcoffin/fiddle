import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";

import { availableEffects, clampMasterGain, groupEffects } from "../src/lib/masterAudioUi.js";

const panelSource = readFileSync(
    new URL("../src/lib/MasterAudioPanel.svelte", import.meta.url),
    "utf8",
);
const printSource = readFileSync(new URL("../src/lib/PrintPanel.svelte", import.meta.url), "utf8");

const plugins = [
    { uid: 3, name: "Hall", manufacturer: "Zed", category: "Reverb", valid: true, compatibleAsEffect: true },
    { uid: 2, name: "Bus Comp", manufacturer: "Able", category: "Dynamics", valid: true, compatibleAsEffect: true },
    { uid: 1, name: "Piano", manufacturer: "Able", valid: true, compatibleAsEffect: false },
    { uid: 4, name: "Broken", manufacturer: "Zed", valid: false, compatibleAsEffect: true },
];

test("only usable effect plug-ins appear and results are predictable", () => {
    assert.deepEqual(availableEffects(plugins).map((plugin) => plugin.uid), [2, 3]);
    assert.deepEqual(availableEffects(plugins, "reverb").map((plugin) => plugin.uid), [3]);
});

test("effect picker groups by manufacturer", () => {
    assert.deepEqual(groupEffects(plugins).map((group) => group.manufacturer), ["Able", "Zed"]);
});

test("master gain is finite and constrained", () => {
    assert.equal(clampMasterGain(-200), -120);
    assert.equal(clampMasterGain(9), 6);
    assert.equal(clampMasterGain("-3.5"), -3.5);
    assert.equal(clampMasterGain("nope"), 0);
});

test("printing lives in an independent pane rather than Master FX", () => {
    assert.doesNotMatch(panelSource, /startMixPrint|requestMixPrintState|print-section/);
    assert.match(panelSource, /resetMasterLoudness/);
    assert.match(panelSource, /Master inserts/);
    assert.match(printSource, /dispatchCpp\("hidePrintWindow"\)/);
    assert.match(printSource, /setToolWindowVisible/);
});

test("Print Audio exposes transport-gated WAV controls", () => {
    const panelSource = printSource;
    assert.match(panelSource, /Print audio/);
    assert.match(panelSource, /dispatchCpp\("startMixPrint", tailSeconds\)/);
    assert.match(panelSource, /dispatchCpp\("stopMixPrint"\)/);
    assert.match(panelSource, /requestMixPrintState/);
    assert.match(panelSource, /droppedBlocks/);
    assert.match(panelSource, /32-bit float/);
    assert.match(panelSource, /Choose WAV & arm/);
    assert.match(panelSource, /transport Stop finishes them together/);
    assert.match(panelSource, /dispatchCpp\("startStemPrint", liveBusIds, includeMaster, tailSeconds\)/);
    assert.match(panelSource, /<Knob label="Tail after Stop" unit="seconds" min=\{0\} max=\{60\}/);
    assert.match(panelSource, /value=\{tailSeconds\} disabled=\{printState.armed\}/);
    assert.match(panelSource, /Printing tail/);
    assert.match(panelSource, /let tailSeconds = \$state\(5\)/);
    assert.match(panelSource, /fieldset class="print-options" disabled=\{printState.armed\}/);
    assert.match(panelSource, /outputCount === 0 \|\| outputCount > 64/);
});
