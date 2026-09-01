/** Return strip IDs in the same order that grouped mixer strips are rendered. */
export const flattenVisualStripOrder = (groups) =>
    groups.flatMap((group) =>
        group.instrGroups.flatMap((instrumentGroup) =>
            instrumentGroup.strips.map((strip) => strip.id),
        ),
    );

/** Add the inclusive anchor-to-target range to an existing selection. */
export const addStripRange = (selectedIds, orderedIds, anchorId, targetId) => {
    const anchorIndex = orderedIds.indexOf(anchorId);
    const targetIndex = orderedIds.indexOf(targetId);
    if (anchorIndex < 0 || targetIndex < 0) return new Set(selectedIds);

    const first = Math.min(anchorIndex, targetIndex);
    const last = Math.max(anchorIndex, targetIndex);
    return new Set([...selectedIds, ...orderedIds.slice(first, last + 1)]);
};
