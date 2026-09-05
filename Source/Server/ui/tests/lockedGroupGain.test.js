import test from "node:test";
import assert from "node:assert/strict";

import { planLockedActivationChange } from "../src/lib/lockedGroupGain.js";

const powerFromDb = (db) => Math.pow(10, db / 10);
const totalPower = (strips) => strips.reduce(
    (sum, strip) => sum + ((strip.active !== false && !strip.muted)
        ? powerFromDb(strip.gainDb)
        : 0),
    0,
);

const applyPlan = (strips, plan) => {
    const gains = new Map(plan.map((change) => [change.id, change.gainDb]));
    return strips.map((strip) => gains.has(strip.id)
        ? { ...strip, gainDb: gains.get(strip.id) }
        : { ...strip });
};

test("deactivating a library redistributes its layer power within a locked chair", () => {
    const strips = [
        { id: "duality", library: "Duality", gainDb: -2.8, active: true, muted: false },
        { id: "elite", library: "Elite", gainDb: -4.9, active: true, muted: false },
        { id: "solo", library: "Solo", gainDb: -8.1, active: true, muted: false },
    ];
    const originalPower = totalPower(strips);

    const plan = planLockedActivationChange(strips, new Set(["elite"]), false);
    const result = applyPlan(strips, plan).map((strip) =>
        strip.id === "elite" ? { ...strip, active: false } : strip
    );

    assert.deepEqual(plan.map((change) => change.id), ["duality", "solo"]);
    assert.ok(Math.abs(totalPower(result) - originalPower) < 1e-10);
    assert.equal(result[1].gainDb, -4.9, "inactive layer keeps its remembered fader");
});

test("reactivating the library reverses the compensation", () => {
    const original = [
        { id: "duality", gainDb: -2.8, active: true, muted: false },
        { id: "elite", gainDb: -4.9, active: true, muted: false },
        { id: "solo", gainDb: -8.1, active: true, muted: false },
    ];
    const deactivatePlan = planLockedActivationChange(
        original,
        new Set(["elite"]),
        false,
    );
    const deactivated = applyPlan(original, deactivatePlan).map((strip) =>
        strip.id === "elite" ? { ...strip, active: false } : strip
    );

    const reactivatePlan = planLockedActivationChange(
        deactivated,
        new Set(["elite"]),
        true,
    );
    const restored = applyPlan(deactivated, reactivatePlan).map((strip) =>
        strip.id === "elite" ? { ...strip, active: true } : strip
    );

    assert.ok(Math.abs(restored[0].gainDb - original[0].gainDb) < 1e-10);
    assert.ok(Math.abs(restored[2].gainDb - original[2].gainDb) < 1e-10);
    assert.ok(Math.abs(totalPower(restored) - totalPower(original)) < 1e-10);
});

test("changes only audible siblings and respects mute and solo state", () => {
    const strips = [
        { id: "target", gainDb: -3, active: true, muted: false, soloed: true },
        { id: "solo-sibling", gainDb: -6, active: true, muted: false, soloed: true },
        { id: "not-soloed", gainDb: 0, active: true, muted: false, soloed: false },
        { id: "muted", gainDb: 0, active: true, muted: true, soloed: true },
    ];

    const plan = planLockedActivationChange(
        strips,
        new Set(["target"]),
        false,
        true,
    );

    assert.deepEqual(plan.map((change) => change.id), ["solo-sibling"]);
});

test("leaves gains alone when no sibling can compensate", () => {
    const strips = [
        { id: "target", gainDb: 0, active: true, muted: false },
        { id: "muted", gainDb: 0, active: true, muted: true },
    ];
    assert.deepEqual(
        planLockedActivationChange(strips, new Set(["target"]), false),
        [],
    );
});
