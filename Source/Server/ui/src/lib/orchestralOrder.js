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

/**
 * Within-family instrument ordering (standard orchestral score order).
 * Lower number = higher in score. Instruments not listed get 99.
 */
export const INSTRUMENT_ORDER = {
    // Woodwinds
    piccolo: 1,
    flute: 2,
    "alto flute": 3,
    oboe: 4,
    "cor anglais": 5,
    "english horn": 5,
    clarinet: 6,
    "bass clarinet": 7,
    bassoon: 8,
    contrabassoon: 9,
    // Brass
    horn: 1,
    "french horn": 1,
    trumpet: 2,
    cornet: 3,
    trombone: 4,
    "bass trombone": 5,
    tuba: 6,
    // Percussion
    timpani: 1,
    "snare drum": 2,
    "bass drum": 3,
    cymbals: 4,
    "tam-tam": 5,
    triangle: 6,
    tambourine: 7,
    glockenspiel: 8,
    xylophone: 9,
    vibraphone: 10,
    marimba: 11,
    "tubular bells": 12,
    celesta: 13,
    // Keys
    harp: 1,
    piano: 2,
    harpsichord: 3,
    organ: 4,
    // Strings
    violin: 1,
    viola: 2,
    violoncello: 3,
    cello: 3,
    "double bass": 4,
    contrabass: 4,
};

/** Get the score-order index for an instrument name (lower = higher in score) */
export const instrumentOrder = (name) =>
    INSTRUMENT_ORDER[name?.toLowerCase()] ?? 99;
