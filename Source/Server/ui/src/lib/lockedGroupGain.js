const MINUS_INFINITY_DB = -120;

const powerFromDb = (db) => {
    if (db <= MINUS_INFINITY_DB) return 0;
    const gain = Math.pow(10, db / 20);
    return gain * gain;
};

const dbFromPower = (power) => {
    if (power <= 0) return MINUS_INFINITY_DB;
    return 10 * Math.log10(power);
};

const isAudible = (strip, active, anySoloed) =>
    active && !strip.muted && (!anySoloed || strip.soloed);

const audible = (strip, anySoloed) => isAudible(strip, strip.active !== false, anySoloed);
const clampDb = (db) => Math.max(MINUS_INFINITY_DB, Math.min(6, db));

export function captureChairLevel(strips, anySoloed = false) {
    return {
        targetDb: dbFromPower(strips.reduce((sum, s) =>
            sum + (audible(s, anySoloed) ? powerFromDb(s.gainDb ?? 0) : 0), 0)),
        weights: Object.fromEntries(strips.map(s => [s.id, powerFromDb(s.gainDb ?? 0)])),
    };
}

// Keep the last non-silent proportions when automatic compensation reaches
// zero. Use real, clamped gains, never an unattainable internal shadow gain.
function distribute(strips, power, state) {
    const current = strips.map(s => powerFromDb(s.gainDb ?? 0));
    const currentSum = current.reduce((a, b) => a + b, 0);
    const weights = currentSum > 0 ? current.map(p => p / currentSum)
        : strips.map(s => state.weights[s.id] ?? 0);
    if (currentSum > 0)
        strips.forEach((s, i) => { state.weights[s.id] = weights[i]; });
    if (!weights.some(w => w > 0)) weights.fill(1);
    const assigned = new Array(strips.length).fill(0);
    let remaining = Math.max(0, power);
    let available = strips.map((_, i) => i).filter(i => weights[i] > 0);
    const maximum = powerFromDb(6);
    while (available.length && remaining > 0) {
        const totalWeight = available.reduce((sum, i) => sum + weights[i], 0);
        const capped = available.filter(i => remaining * weights[i] / totalWeight > maximum);
        if (!capped.length) {
            for (const i of available) assigned[i] = remaining * weights[i] / totalWeight;
            break;
        }
        for (const i of capped) { assigned[i] = maximum; remaining -= maximum; }
        available = available.filter(i => !capped.includes(i));
    }
    return strips.map((s, i) => ({ id: s.id, gainDb: dbFromPower(assigned[i]) }));
}

/** Keep explicit layer edits, compensate audible siblings toward the stored
 * target, and return updated blend memory in the SAME undoable transaction. */
export function planLockedLayerChange(strips, changes, level, anySoloed = false) {
    const state = { ...level, weights: { ...level.weights } };
    const edits = new Map(changes.map(change => [change.id, change]));
    const next = strips.map(s => ({ ...s, ...edits.get(s.id) }));
    let fixedPower = 0;
    for (const s of next) {
        if (!edits.has(s.id)) continue;
        if (audible(s, anySoloed)) fixedPower += powerFromDb(clampDb(s.gainDb ?? 0));
        if (edits.get(s.id).gainDb !== undefined)
            state.weights[s.id] = powerFromDb(clampDb(s.gainDb));
    }
    const others = next.filter(s => !edits.has(s.id) && audible(s, anySoloed));
    const compensation = distribute(others, powerFromDb(state.targetDb) - fixedPower, state);
    return { changes: [...changes, ...compensation], state };
}

export function planChairTargetChange(strips, targetDb, level, anySoloed = false) {
    const state = { ...level, targetDb, weights: { ...level.weights } };
    return { changes: distribute(strips.filter(s => audible(s, anySoloed)),
        powerFromDb(targetDb), state), state };
}

/**
 * Calculate sibling gain changes that preserve a locked chair's total power
 * when one or more layers change activation state together.
 *
 * The affected layers retain their remembered fader values. Every currently
 * audible, unaffected sibling is scaled proportionally to absorb (or release)
 * the affected layers' power.
 */
export function planLockedActivationChange(
    strips,
    affectedIds,
    nextActive,
    anySoloed = false,
) {
    const affected = affectedIds instanceof Set
        ? affectedIds
        : new Set(affectedIds);

    let oldPower = 0;
    let affectedPowerAfter = 0;
    let unaffectedPower = 0;
    const adjustable = [];

    for (const strip of strips) {
        const activeBefore = strip.active !== false;
        const db = strip.gainDb ?? 0;
        const power = powerFromDb(db);
        const isAffected = affected.has(strip.id);

        if (isAudible(strip, activeBefore, anySoloed))
            oldPower += power;

        if (isAffected) {
            if (isAudible(strip, nextActive, anySoloed))
                affectedPowerAfter += power;
        } else if (isAudible(strip, activeBefore, anySoloed)) {
            unaffectedPower += power;
            adjustable.push({ id: strip.id, power });
        }
    }

    if (adjustable.length === 0 || unaffectedPower <= 0)
        return [];

    const requiredUnaffectedPower = Math.max(0, oldPower - affectedPowerAfter);
    const scale = requiredUnaffectedPower / unaffectedPower;
    return adjustable.map((strip) => ({
        id: strip.id,
        gainDb: dbFromPower(strip.power * scale),
    }));
}
