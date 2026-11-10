// Percussion kits (docs/percussion-kits.md): a kit is a group of ordinary
// solo chairs, one per instrument, that share a kit ID and name. Dorico routes
// a kit's instruments exactly like the same instruments outside a kit, so
// nothing about a kit reaches the playback template; it's how Fiddle shows
// and edits the chairs together.

/** Dorico's kit instruments (those with a kit definition), for presets. */
export const isKitInstrument = (instrument) =>
    Array.isArray(instrument?.kitInstruments) && instrument.kitInstruments.length > 0;

const words = (text) => text
    .split(/[.\-]/)
    .filter(Boolean)
    .map((word) => word.charAt(0).toUpperCase() + word.slice(1))
    .join(" ");

/** The part of each ID after the segments they all share. */
function distinguishingTails(ids) {
    const split = ids.map((id) => id.split("."));
    let shared = 0;
    while (split.every((parts) => shared < parts.length - 1 && parts[shared] === split[0][shared])) shared++;
    return split.map((parts) => parts.slice(shared).join("."));
}

/**
 * Chair names for a kit's instruments, in kit order. Dorico gives the five
 * wood blocks the same name, so repeated names get what tells their IDs apart:
 * "Wood Block (High)", "Wood Block (Medium High)"…
 */
export function kitChairNames(entityIds, instruments) {
    const byId = new Map((instruments || []).map((instrument) => [instrument.entityID, instrument]));
    const base = entityIds.map((id) => byId.get(id)?.name || words(id.split(".").slice(-2).join(".")));
    const names = base.slice();
    const groups = new Map();
    base.forEach((name, index) => groups.set(name, [...(groups.get(name) || []), index]));
    for (const [name, indices] of groups) {
        if (indices.length < 2) continue;
        const distinct = [...new Set(indices.map((index) => entityIds[index]))];
        const tails = distinguishingTails(distinct);
        const tailOf = new Map(distinct.map((id, index) => [id, words(tails[index])]));
        const seen = new Map();
        for (const index of indices) {
            const tail = distinct.length > 1 ? tailOf.get(entityIds[index]) : "";
            const label = tail ? `${name} (${tail})` : name;
            const count = (seen.get(label) || 0) + 1;
            seen.set(label, count);
            names[index] = count > 1 || (distinct.length === 1) ? `${label} ${count}` : label;
        }
    }
    return names;
}

/** A kit name not already used: "Wood Block", then "Wood Block 2"… */
export function suggestedKitName(name, chairs) {
    const used = new Set((chairs || []).filter((chair) => chair.kitId).map((chair) => chair.kitName));
    const base = (name || "Kit").trim() || "Kit";
    if (!used.has(base)) return base;
    for (let n = 2; ; n++) if (!used.has(`${base} ${n}`)) return `${base} ${n}`;
}

/** The family a kit is filed under: its first instrument's. */
export function kitFamily(entityIds, instruments) {
    const first = (instruments || []).find((instrument) => instrument.entityID === entityIds[0]);
    return first?.family || "percussion";
}

/** The createKit payload: one solo chair per instrument, in kit order. */
export function kitPayload(name, entityIds, instruments) {
    const names = kitChairNames(entityIds, instruments);
    return {
        name: name.trim(),
        family: kitFamily(entityIds, instruments),
        instruments: entityIds.map((entityID, index) => ({ entityID, name: names[index] })),
    };
}

/**
 * Chairs in display order, with each kit's chairs kept together: a kit sits
 * where its first chair would, and its chairs keep the order they were
 * created in (the kit's order). Returns units: { chair } or
 * { kitId, kitName, chairs }.
 */
export function chairUnits(chairs, compare) {
    const sorted = chairs.slice().sort(compare);
    const kits = new Map();
    for (const chair of chairs) {
        if (!chair.kitId) continue;
        if (!kits.has(chair.kitId)) kits.set(chair.kitId, []);
        kits.get(chair.kitId).push(chair);
    }
    const units = [];
    const placed = new Set();
    for (const chair of sorted) {
        if (!chair.kitId) { units.push({ chair }); continue; }
        if (placed.has(chair.kitId)) continue;
        placed.add(chair.kitId);
        const members = kits.get(chair.kitId).slice()
            .sort((a, b) => (a.displayOrder ?? 0) - (b.displayOrder ?? 0) || (a.flatIndex ?? 0) - (b.flatIndex ?? 0));
        units.push({ kitId: chair.kitId, kitName: members[0].kitName || "Kit", chairs: members });
    }
    return units;
}

/** Chairs in display order with kits kept together, as a flat list. */
export const orderChairsWithKits = (chairs, compare) =>
    chairUnits(chairs, compare).flatMap((unit) => unit.chairs || [unit.chair]);

/**
 * The mixer's columns in a family: each chair, with a kit's consecutive
 * chairs gathered under one kit header. Items carry instrGroup.chair.
 */
export function kitColumns(instrGroups) {
    const columns = [];
    for (const instrGroup of instrGroups) {
        const kitId = instrGroup.chair?.kitId || "";
        const last = columns[columns.length - 1];
        if (kitId && last?.kitId === kitId) last.instrGroups.push(instrGroup);
        else if (kitId) columns.push({ kitId, kitName: instrGroup.chair.kitName || "Kit", instrGroups: [instrGroup] });
        else columns.push({ instrGroup });
    }
    return columns;
}
