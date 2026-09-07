/**
 * Shared orchestral ordering constants.
 * Used by InstrumentList (Setup tab), Timeline, and LibraryManager.
 */

/** Family ordering (top → bottom of score) — used for group headers */
export const FAMILY_ORDER = [
    "woodwinds",
    "brass",
    "percussion",
    "keys",
    "strings",
    "choir",
];

/** Map backend family names to canonical keys */
export const FAMILY_ALIASES = {
    wind: "woodwinds",
    woodwind: "woodwinds",
    woodwinds: "woodwinds",
    brass: "brass",
    drum: "percussion",
    drums: "percussion",
    percussion: "percussion",
    "pitched-percussion": "percussion",
    "unpitched-percussion": "percussion",
    pitchedpercussion: "percussion",
    keys: "keys",
    keyboard: "keys",
    keyboards: "keys",
    strings: "strings",
    string: "strings",
    choir: "choir",
    vocal: "choir",
    vocals: "choir",
    voice: "choir",
};

/** Normalize a backend family name to a canonical key */
export const canonicalFamily = (fam) =>
    FAMILY_ALIASES[fam?.toLowerCase()] || fam?.toLowerCase() || "";

/**
 * Score order map: entityID → position.
 * Populated from Dorico's instrumentScoreOrders.xml "Orchestral" order,
 * delivered via setDoricoInstruments with a scoreOrder field per instrument.
 *
 * This is a module-level mutable map updated by populateScoreOrder().
 * @type {Map<string, number>}
 */
const scoreOrderMap = new Map();

/**
 * Populate the score order map from the Dorico instruments array.
 * Called when setDoricoInstruments arrives from C++.
 * Each instrument object should have { entityID, scoreOrder }.
 * @param {Array<{entityID: string, scoreOrder: number}>} instruments
 */
export const populateScoreOrder = (instruments) => {
    scoreOrderMap.clear();
    let count = 0;
    for (const inst of instruments) {
        if (inst.entityID && inst.scoreOrder != null) {
            scoreOrderMap.set(inst.entityID, inst.scoreOrder);
            count++;
        }
    }
    console.log(`[orchestralOrder] Populated score order: ${count} entries`);
};

/**
 * Get the orchestral score-order position for an entity ID.
 * Lower value = higher in score. Returns 99999 for unknown instruments.
 * @param {string} entityID
 * @returns {number}
 */
export const instrumentScoreOrder = (entityID) =>
    scoreOrderMap.get(entityID) ?? 99999;

/**
 * Compare project chairs in conventional orchestral score order without
 * changing their persistent MIDI assignments. Repeated instances of one
 * instrument stay in ordinal order.
 */
export const compareChairsInScoreOrder = (a, b) => {
    // Mixer workflow is clearer when independent solo players are collected
    // before ensemble/section destinations. Preserve orchestral score order
    // within each of those two role groups.
    const roleA = a.role === "solo" ? 0 : 1;
    const roleB = b.role === "solo" ? 0 : 1;
    if (roleA !== roleB) return roleA - roleB;

    // Dorico places piccolo above concert flute in its built-in Orchestral
    // table. In the mixer, keep all numbered concert-flute chairs together
    // and place piccolo immediately after them.
    const isConcertFlute = (chair) =>
        chair.entityID === "instrument.wind.flute";
    const isPiccolo = (chair) =>
        chair.entityID === "instrument.wind.piccolo" ||
        chair.entityID?.startsWith("instrument.wind.piccolo.");
    if (isConcertFlute(a) && isPiccolo(b)) return -1;
    if (isPiccolo(a) && isConcertFlute(b)) return 1;

    const orderA = a.scoreOrder ?? instrumentScoreOrder(a.entityID);
    const orderB = b.scoreOrder ?? instrumentScoreOrder(b.entityID);
    if (orderA !== orderB) return orderA - orderB;
    if (a.entityID === b.entityID) {
        if ((a.ordinal ?? 0) !== (b.ordinal ?? 0))
            return (a.ordinal ?? 0) - (b.ordinal ?? 0);
    }
    return (a.displayOrder ?? 0) - (b.displayOrder ?? 0);
};
