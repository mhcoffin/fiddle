export const techniqueEntries = (record) =>
    Object.entries(record.receivedTechniques || {});

// Transport metadata from the bridge, not musical dimensions.
const hiddenDimensions = new Set(["dorico_length_category", "dorico_written_duration"]);

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

export const resolutionState = (record) => {
    if (!record.expressionMapAssigned)
        return { kind: "unmapped", label: "No expression map assigned" };
    if (record.baseSwitchName)
        return { kind: "matched", label: record.baseSwitchName };
    return { kind: "unmatched", label: "No match" };
};
