/**
 * Shared orchestral ordering constants.
 * Used by InstrumentList (Setup tab) and Timeline.
 */

/** Family ordering (top → bottom of score) */
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
