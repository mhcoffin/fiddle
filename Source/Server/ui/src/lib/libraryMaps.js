// Fiddle's expression and percussion maps (docs/maps.md): one collection that
// any patch or layer can use, the picker's ranking and search, and the import
// and update-check helpers.

const canonical = (id) => String(id || "").split(".alias.")[0];
const listsInstrument = (map, instrument) =>
    (map.instruments || []).some((id) => canonical(id) === canonical(instrument));

/** A percussion map's note for a chair whose instrument it doesn't list,
 *  else "". A single-instrument map still plays on any chair. */
export function coverageNote(map, instrument) {
    if (map?.kind !== "percussion" || !instrument || listsInstrument(map, instrument)) return "";
    return (map.instruments || []).length === 1 ? "other instrument" : "does not cover this instrument";
}

export const MAP_KINDS = ["expression", "percussion"];

export const KIND_LABELS = {
    expression: { title: "Expression Maps", noun: "expression map", plural: "expression maps" },
    percussion: { title: "Percussion Maps", noun: "percussion map", plural: "percussion maps" },
};

export const IMPORT_STATUS_LABELS = {
    new: "New",
    newer: "Newer version",
    same: "Already in Fiddle",
    changed: "Differs from Fiddle's copy",
};

export const SOURCE_STATUS_LABELS = {
    current: "Up to date",
    newer: "Newer version",
    differs: "Differs",
    missing: "Source missing",
};

/**
 * One line summing up a source check: what is current, what has an update,
 * and what has lost its source. `tone` is "ok" when everything is current.
 */
export function summarizeSourceCheck(statuses) {
    const count = (...kinds) => (statuses || []).filter((s) => kinds.includes(s.status)).length;
    const current = count("current"), updates = count("newer", "differs"), missing = count("missing");
    if (current + updates + missing === 0) return { text: "No imported maps to check", tone: "ok" };
    if (updates === 0 && missing === 0)
        return { text: current === 1 ? "Up to date" : `All ${current} up to date`, tone: "ok" };
    const parts = [];
    if (updates) parts.push(`${updates} ${updates === 1 ? "update" : "updates"} available`);
    if (missing) parts.push(`${missing} ${missing === 1 ? "source" : "sources"} missing`);
    if (current) parts.push(`${current} up to date`);
    return { text: parts.join(" · "), tone: "warn" };
}

const byName = (a, b) =>
    String(a.name).localeCompare(String(b.name)) || String(a.entityId).localeCompare(String(b.entityId));

/** Fiddle's maps of one kind, sorted by name. */
export function mapsOfKind(maps, kind) {
    return (maps || []).filter((map) => map.kind === kind).sort(byName);
}

const fileName = (path) => String(path || "").split("/").pop();
const words = (query) => String(query || "").toLowerCase().split(/\s+/).filter(Boolean);

/** True when every word of query is in the map's name, ID, creator, player
 *  names or source file. */
export function mapMatches(map, query) {
    const wanted = words(query);
    if (!wanted.length) return true;
    const text = [map.name, map.entityId, map.creator, map.pluginNames, fileName(map.sourcePath)]
        .join(" ").toLowerCase();
    return wanted.every((word) => text.includes(word));
}

/**
 * Why a map suits a patch or layer, from how it is already used: by this
 * patch (or its layers), on the same Dorico instrument elsewhere, or (for
 * percussion) by covering the instrument. `score` ranks them, 0 = no reason.
 */
export function mapSuggestion(map, { patchId = "", instrument = "" } = {}) {
    const uses = map.uses || [];
    if (patchId && uses.some((use) => use.patchId === patchId))
        return { score: 3, reason: "This patch" };
    const sameInstrument = instrument
        ? uses.filter((use) => use.instrument && canonical(use.instrument) === canonical(instrument)).length : 0;
    if (sameInstrument)
        return { score: 2, count: sameInstrument, reason: `Used for this instrument (${sameInstrument})` };
    if (map.kind === "percussion" && instrument && listsInstrument(map, instrument))
        return { score: 1, reason: "Covers this instrument" };
    return { score: 0, reason: "" };
}

export const PICKER_FILTERS = [
    { key: "all", label: "All" },
    { key: "favorites", label: "Favorites" },
    { key: "library", label: "This library" },
];

/**
 * The picker's list. context: { kind, patchId, libraryId, instrument }.
 * With no query and the "all" filter: Suggested, Favorites, then All maps.
 * Otherwise one list of matches, suggested first, then favorites, then by
 * name. Returns sections of { map, reason } items.
 */
export function pickerSections(maps, context = {}, { query = "", filter = "all" } = {}) {
    const items = mapsOfKind(maps, context.kind || "expression")
        .filter((map) => filter !== "favorites" || map.favorite)
        .filter((map) => filter !== "library"
            || (context.libraryId && (map.uses || []).some((use) => use.libraryId === context.libraryId)))
        .filter((map) => mapMatches(map, query))
        .map((map) => ({ map, ...mapSuggestion(map, context) }));
    const ranked = (a, b) => b.score - a.score || (b.count || 0) - (a.count || 0)
        || Number(Boolean(b.map.favorite)) - Number(Boolean(a.map.favorite)) || byName(a.map, b.map);
    if (words(query).length || filter !== "all") {
        const title = filter === "favorites" ? "Favorites" : filter === "library" ? "This library" : "Matches";
        return items.length ? [{ key: "matches", title, items: items.sort(ranked) }] : [];
    }
    const suggested = items.filter((item) => item.score > 0).sort(ranked);
    const favorites = items.filter((item) => !item.score && item.map.favorite);
    const rest = items.filter((item) => !item.score && !item.map.favorite);
    return [
        { key: "suggested", title: "Suggested", items: suggested },
        { key: "favorites", title: "Favorites", items: favorites },
        { key: "all", title: "All maps", items: rest },
    ].filter((section) => section.items.length);
}

/**
 * Merge a setLibraryMapUsage message ([{kind, entityId, type, patchId,
 * libraryId, instrument, label}]) into the maps: each map's `uses` and its
 * `references` labels (patches first, then layers).
 */
export function applyMapUsage(maps, usage) {
    const byMap = new Map();
    for (const { kind, entityId, ...use } of usage || []) {
        const key = `${kind}\u0000${entityId}`;
        if (!byMap.has(key)) byMap.set(key, []);
        byMap.get(key).push(use);
    }
    return (maps || []).map((map) => {
        const uses = byMap.get(`${map.kind}\u0000${map.entityId}`) || [];
        const labels = (type) => uses.filter((use) => use.type === type).map((use) => use.label);
        return { ...map, uses, references: [...labels("patch"), ...labels("layer")] };
    });
}

/** New and newer maps are ticked when an import list opens. */
export function defaultImportSelection(candidates) {
    return new Set((candidates || [])
        .filter((c) => c.status === "new" || c.status === "newer")
        .map((c) => c.entityId));
}

/** Candidates whose name, ID, creator or file match every word of query. */
export function filterCandidates(candidates, query) {
    const wanted = words(query);
    if (!wanted.length) return candidates || [];
    return (candidates || []).filter((candidate) => {
        const text = [candidate.name, candidate.entityId, candidate.creator, candidate.pluginNames, fileName(candidate.file)]
            .join(" ").toLowerCase();
        return wanted.every((word) => text.includes(word));
    });
}

/** The file name a map was imported from, for display. */
export function sourceFileName(map) {
    return fileName(map?.sourcePath);
}

/** A short random ID that lets the panel which asked for an import list
 *  recognise the answer. */
export function newRequestId(prefix = "maps") {
    return `${prefix}-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 8)}`;
}
