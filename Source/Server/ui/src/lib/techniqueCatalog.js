export const sourceLabels = {
    builtin: "Dorico built-in", installed: "Dorico additions", assigned: "Fiddle assignment",
    reference: "Reference collection", additional: "Additional source", bridge: "Fiddle bridge",
};

export function mapsInScope(report, scope) {
    return new Set((report?.maps || []).flatMap((map, i) => {
        const kind = report.sources[map.source]?.kind;
        const include = scope === "all" || (scope === "installed" && kind === "installed") ||
            (scope === "reference" && ["reference", "additional"].includes(kind)) ||
            map.usage?.some(use => use.scope === scope);
        return include ? [i] : [];
    }));
}

export function catalogRows(report, { scope = "project", query = "", status = "all", mapIndex = "" } = {}) {
    if (!report) return [];
    const maps = mapsInScope(report, scope);
    const terms = query.toLowerCase().trim().split(/\s+/).filter(Boolean);
    return (report.techniques || []).filter(row => {
        const scopedMaps = row.maps.filter(index => maps.has(index));
        if (scope !== "all" && !scopedMaps.length) return false;
        if (mapIndex !== "" && !scopedMaps.includes(Number(mapIndex))) return false;
        if (status === "gap" && row.declared) return false;
        if (status === "declared" && !row.declared) return false;
        if (status === "missing" && row.definitionState !== "missing") return false;
        if (status === "variants" && !row.variants) return false;
        if (!terms.length) return true;
        const text = [row.id, ...row.names, ...row.appearances.flatMap(a => [a.name, a.text]),
            ...scopedMaps.map(i => report.maps[i].name)].join(" ").toLowerCase();
        return terms.every(term => text.includes(term));
    });
}

export function defaultCatalogScope(report) {
    if (mapsInScope(report, "project").size) return "project";
    if (mapsInScope(report, "library").size) return "library";
    return "installed";
}

export function definitionLabel(row) {
    if (row.definitionState === "missing") return "Definition not found";
    if (row.definitionState === "reference") return "Reference definition only";
    return row.localVariants ? "Local definition variants" : row.variants ? "Reference variants differ" : "Definition found";
}
