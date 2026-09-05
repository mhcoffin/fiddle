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
