export const techniqueEntries = (record) =>
    Object.entries(record.receivedTechniques || {});

export const dimensionEntries = (record) =>
    Object.entries(record.receivedDimensions || {}).filter(
        ([name]) => name !== "dorico_length_category",
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
