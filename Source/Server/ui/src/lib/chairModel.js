import { FAMILY_ORDER, canonicalFamily, compareChairsInScoreOrder } from "./orchestralOrder.js";

const familyLabel = (key) =>
    key ? key.charAt(0).toUpperCase() + key.slice(1) : "Other";

export function groupChairs(chairs) {
    const groups = new Map();
    for (const chair of chairs) {
        const key = canonicalFamily(chair.family) || "other";
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push(chair);
    }
    return [...groups.entries()]
        .sort(([a], [b]) => {
            const ai = FAMILY_ORDER.indexOf(a);
            const bi = FAMILY_ORDER.indexOf(b);
            return (ai < 0 ? 999 : ai) - (bi < 0 ? 999 : bi) || a.localeCompare(b);
        })
        .map(([key, entries]) => ({
            key,
            label: familyLabel(key),
            chairs: entries.slice().sort(compareChairsInScoreOrder),
        }));
}

export function searchDoricoInstruments(instruments, query, limit = 30) {
    const q = query.trim().toLowerCase();
    if (!q) return [];
    const relevance = (name) => {
        const n = name.toLowerCase();
        if (n === q) return 0;
        if (n.startsWith(q)) return 1;
        return 2;
    };
    return instruments
        .filter((instrument) =>
            instrument.name.toLowerCase().includes(q) ||
            instrument.entityID.toLowerCase().includes(q))
        .sort((a, b) =>
            relevance(a.name) - relevance(b.name) ||
            Number(!a.isDefault) - Number(!b.isDefault) ||
            (a.xmlIndex ?? 99999) - (b.xmlIndex ?? 99999))
        .slice(0, limit);
}

export function suggestedChairName(instrument, role, chairs) {
    const matching = chairs.filter((chair) =>
        chair.entityID === instrument.entityID && chair.role === role).length;
    const prefix = role === "solo" ? "Solo " : "";
    return `${prefix}${instrument.name} ${matching + 1}`;
}
