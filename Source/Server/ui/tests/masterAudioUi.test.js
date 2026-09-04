import assert from "node:assert/strict";
import test from "node:test";

import { availableEffects, clampMasterGain, groupEffects } from "../src/lib/masterAudioUi.js";

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
