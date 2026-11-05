// Per-library expression and percussion maps (docs/library-map-import.md).

export const MAP_KINDS = ["expression", "percussion"];

export const KIND_LABELS = {
    expression: { title: "Expression Maps", noun: "expression map", plural: "expression maps" },
    percussion: { title: "Percussion Maps", noun: "percussion map", plural: "percussion maps" },
};

export const IMPORT_STATUS_LABELS = {
    new: "New",
    newer: "Newer version",
    same: "Already in library",
    changed: "Differs from library",
};

export const SOURCE_STATUS_LABELS = {
    current: "Up to date",
    newer: "Newer version",
    differs: "Differs",
    missing: "Source missing",
};

/** One library's maps of one kind, sorted by name. */
export function mapsFor(maps, libraryId, kind) {
    return (maps || [])
        .filter((map) => map.libraryId === libraryId && map.kind === kind)
        .sort((a, b) => String(a.name).localeCompare(String(b.name)) || String(a.entityId).localeCompare(String(b.entityId)));
}

/** New and newer maps are ticked when an import list opens. */
export function defaultImportSelection(candidates) {
    return new Set((candidates || [])
        .filter((c) => c.status === "new" || c.status === "newer")
        .map((c) => c.entityId));
}

/** The search a Find in Dorico Library list starts with: the library's
 *  vendor, or its name when it has none. */
export function initialSearch(library) {
    return String(library?.vendor || library?.name || "").trim();
}

const fileName = (path) => String(path || "").split("/").pop();

/** Candidates whose name, ID, creator or file match every word of query. */
export function filterCandidates(candidates, query) {
    const words = String(query || "").toLowerCase().split(/\s+/).filter(Boolean);
    if (!words.length) return candidates || [];
    return (candidates || []).filter((candidate) => {
        const text = [candidate.name, candidate.entityId, candidate.creator, candidate.pluginNames, fileName(candidate.file)]
            .join(" ").toLowerCase();
        return words.every((word) => text.includes(word));
    });
}

/** The file name a map was imported from, for display. */
export function sourceFileName(map) {
    return fileName(map?.sourcePath);
}
