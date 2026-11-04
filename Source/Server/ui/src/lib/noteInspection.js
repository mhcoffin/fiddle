export const techniqueEntries = (record) =>
    Object.entries(record.receivedTechniques || {});

// Transport metadata from the bridge, not musical dimensions.
const hiddenDimensions = new Set(["dorico_length_category", "dorico_pinned_playback"]);

export const dimensionEntries = (record) =>
    Object.entries(record.receivedDimensions || {}).filter(
        ([name]) => !hiddenDimensions.has(name),
    );

export const formatDimensionValue = (value) => {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) return String(value);
    return Number.isInteger(numeric)
        ? String(numeric)
        : numeric.toFixed(2).replace(/0+$/, "").replace(/\.$/, "");
};

/** The library percussion map's decision for a note, or null. */
export const percussionState = (record) => {
    const p = record.percussion;
    if (!p || !p.mapName) return null;
    if (p.withheld) return { kind: "withheld", label: `Not played: ${p.withheld}` };
    const keys = (p.keyswitches || []).length ? ` + KS ${p.keyswitches.join(", ")}` : "";
    const fallback = p.instrumentFallback ? " (map for another instrument)" : "";
    return {
        kind: p.warning ? "warning" : "matched",
        label: `${p.entryName || (p.entryTechniques || []).join("+")} → ${p.pitch}${keys}${fallback}`,
        warning: p.warning || "",
        mapName: p.mapName,
    };
};

export const resolutionState = (record) => {
    if (!record.expressionMapAssigned)
        return { kind: "unmapped", label: "No expression map assigned" };
    if (record.baseSwitchName)
        return { kind: "matched", label: record.baseSwitchName };
    return { kind: "unmatched", label: "No match" };
};
