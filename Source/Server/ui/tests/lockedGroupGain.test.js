import test from "node:test";
import assert from "node:assert/strict";

import { planLockedActivationChange, captureChairLevel, planLockedLayerChange, planChairTargetChange } from "../src/lib/lockedGroupGain.js";

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

const applyChanges = (strips, changes) => strips.map(s => ({ ...s, ...changes.find(c => c.id === s.id) }));
const close = (a, b) => assert.ok(Math.abs(a - b) < 1e-9, `${a} != ${b}`);
const blend = () => [
    { id: "a", gainDb: 10 * Math.log10(0.5) },
    { id: "b", gainDb: 10 * Math.log10(0.3) },
    { id: "c", gainDb: 10 * Math.log10(0.2) },
];
const audiblePower = strips => strips.reduce((sum, s) => sum + (
    s.active !== false && !s.muted && s.gainDb > -120 ? powerFromDb(s.gainDb) : 0), 0);

test("exceeding a locked target does not ratchet it up; siblings recover their blend", () => {
    const initial = blend();
    let strips = initial;
    let level = captureChairLevel(strips);
    for (const db of [3, 5, 2, 0, -1, initial[0].gainDb]) {
        const plan = planLockedLayerChange(strips, [{ id: "a", gainDb: db }], level);
        strips = applyChanges(strips, plan.changes);
        // Simulate save/reopen between EVERY edit, including while siblings are silent.
        level = JSON.parse(JSON.stringify(plan.state));
        close(level.targetDb, 0);
        if (db >= 0) assert.deepEqual(strips.slice(1).map(s => s.gainDb), [-120, -120]);
        else {
            close(audiblePower(strips), 1);
            close(powerFromDb(strips[1].gainDb) / powerFromDb(strips[2].gainDb), 1.5);
        }
    }
    strips.forEach((s, i) => close(s.gainDb, initial[i].gainDb));
});

test("moving the chair changes its target and preserves the blend through target silence", () => {
    const initial = blend();
    const silent = planChairTargetChange(initial, -120, captureChairLevel(initial));
    assert.ok(silent.changes.every(c => c.gainDb === -120));
    const restored = planChairTargetChange(applyChanges(initial, silent.changes), -6, silent.state);
    close(restored.state.targetDb, -6);
    restored.changes.forEach((s, i) => close(s.gainDb, initial[i].gainDb - 6));
});

test("mute and activation changes preserve the original target after excess", () => {
    const initial = blend();
    const excess = planLockedLayerChange(initial, [{ id: "a", gainDb: 3 }], captureChairLevel(initial));
    const strips = applyChanges(initial, excess.changes);
    for (const control of [{ muted: true }, { active: false }]) {
        const plan = planLockedLayerChange(strips, [{ id: "a", ...control }], excess.state);
        const after = applyChanges(strips, plan.changes);
        close(audiblePower(after), 1);
        close(powerFromDb(after[1].gainDb) / powerFromDb(after[2].gainDb), 1.5);
    }
});

test("inaudible layers neither consume the target nor receive compensation", () => {
    const strips = [...blend().map(s => ({ ...s, soloed: true })),
        { id: "muted", gainDb: 0, muted: true },
        { id: "inactive", gainDb: 0, active: false },
        { id: "not-solo", gainDb: 0 }];
    const level = captureChairLevel(strips, true);
    const plan = planLockedLayerChange(strips, [{ id: "muted", gainDb: 4 }], level, true);
    assert.deepEqual(plan.changes.map(c => c.id), ["muted", "a", "b", "c"]);
    plan.changes.slice(1).forEach((s, i) => close(s.gainDb, strips[i].gainDb));
});

test("unattainable targets retain their value while real gains remain within limits", () => {
    const strips = [{ id: "a", gainDb: 0 }, { id: "b", gainDb: -30 }];
    const plan = planChairTargetChange(strips, 20, captureChairLevel(strips));
    assert.equal(plan.state.targetDb, 20);
    assert.ok(plan.changes.every(s => s.gainDb <= 6));
    const alone = planLockedLayerChange([{ id: "a", gainDb: 0 }], [{ id: "a", gainDb: -6 }],
        { targetDb: 0, weights: { a: 1 } });
    assert.equal(alone.state.targetDb, 0);
    assert.equal(alone.changes[0].gainDb, -6);
});
