export const auditStatuses = {
    witness: "Decoder witness",
    missing: "Missing identity",
    combination: "Encoding conflict",
    selection: "Selection needs review",
    uncertain: "Uncertain",
};

export function auditRows(audit, { chairId = "", status = "all", query = "" } = {}) {
    const search = query.toLowerCase().trim();
    return (audit?.chairs || []).filter(chair => !chairId || chair.id === chairId)
        .flatMap(chair => (chair.layers || []).flatMap(layer => (layer.rows || []).map(row => ({
            ...row, chairId: chair.id, chairName: chair.name, layerId: layer.id, layerName: layer.name,
            mapName: layer.mapName, mapId: layer.mapId, key: JSON.stringify([chair.id, layer.id, row.index]),
        }))))
        .filter(row => (status === "all" || (status === "attention" ? row.status !== "witness" : row.status === status)) &&
            (!search || [row.chairName, row.layerName, row.mapName, row.mapId, row.name, ...(row.techniques || [])].join(" ").toLowerCase().includes(search)));
}

export function auditCounts(rows) {
    const counts = Object.fromEntries(Object.keys(auditStatuses).map(key => [key, 0]));
    for (const row of rows) counts[row.status] = (counts[row.status] || 0) + 1;
    return counts;
}
